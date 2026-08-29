#include "Updater.h"

#include "core/LinuxUpdate.h"
#include "core/UpdateChannel.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace {

constexpr char kStableReleaseApi[] =
    "https://api.github.com/repos/MinottiAlessandro/Emerald/releases/latest";
constexpr char kDevelopmentReleaseApi[] =
    "https://api.github.com/repos/MinottiAlessandro/Emerald/releases?per_page=100";
constexpr int kReleaseCheckTimeoutMs = 15000;

// GitHub's API rejects requests without a User-Agent; the asset URLs 302 to a
// CDN, so every request opts into following same-or-safer redirects.
void prepare(QNetworkRequest &req) {
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Emerald-Updater"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
}

QByteArray sha256FromDigest(const QString &digest) {
    constexpr auto prefix = "sha256:";
    if (!digest.startsWith(QLatin1String(prefix), Qt::CaseInsensitive))
        return {};
    const QByteArray hex = digest.mid(qstrlen(prefix)).toLatin1();
    if (hex.size() != 64)
        return {};
    for (const char c : hex) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return {};
    }
    const QByteArray value = QByteArray::fromHex(hex);
    if (value.size() !=
        QCryptographicHash::hashLength(QCryptographicHash::Sha256))
        return {};
    return value;
}

bool isTrustedReleaseUrl(const QUrl &url) {
    return url.isValid() && url.scheme() == QLatin1String("https") &&
           url.host().compare(QLatin1String("github.com"),
                              Qt::CaseInsensitive) == 0 &&
           url.path().startsWith(
               QLatin1String("/MinottiAlessandro/Emerald/releases/"));
}

bool startupNotificationBlocked(QWidget *window) {
    if (QApplication::activeModalWidget())
        return true;
    if (!window)
        return false;
    const auto dialogs =
        window->findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly);
    for (QDialog *dialog : dialogs) {
        if (dialog->isVisible())
            return true;
    }
    return false;
}

// The release-asset filename this build should download. Mirrors the
// version-less names produced by the Release workflow / CPack.
QString platformAssetName() {
#if defined(Q_OS_WIN)
    return QStringLiteral("Emerald-win64-setup.exe");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Emerald-macOS.dmg");
#else
    const QString arch = QSysInfo::currentCpuArchitecture();
    return (arch.contains(QLatin1String("arm")) || arch.contains(QLatin1String("aarch")))
               ? QStringLiteral("Emerald-aarch64.AppImage")
               : QStringLiteral("Emerald-x86_64.AppImage");
#endif
}

#if defined(Q_OS_LINUX)
QString linuxUpdateStagePath() {
    QTemporaryDir stage(QDir(QDir::tempPath()).filePath(
        QStringLiteral("EmeraldUpdate-XXXXXX")));
    if (!stage.isValid())
        return {};
    stage.setAutoRemove(false);
    return stage.path();
}
#endif

#if defined(Q_OS_MACOS)
QString currentMacAppBundlePath() {
    QDir dir(QCoreApplication::applicationDirPath());
    if (!dir.cdUp() || !dir.cdUp())
        return {};
    const QString bundlePath = dir.absolutePath();
    return bundlePath.endsWith(QStringLiteral(".app")) ? bundlePath : QString();
}

QString macUpdateStagePath() {
    const QString name = QStringLiteral("EmeraldUpdate-%1-%2")
                             .arg(QCoreApplication::applicationPid())
                             .arg(QDateTime::currentMSecsSinceEpoch());
    const QString path = QDir(QDir::tempPath()).filePath(name);
    return QDir().mkpath(path) ? path : QString();
}

QByteArray macInstallerScript() {
    return R"SH(#!/bin/sh
set -u

pid="$1"
dmg="$2"
target="$3"
version="$4"
log="$5"

exec >>"$log" 2>&1

fail() {
    echo "ERROR: $*"
    /usr/bin/osascript -e 'display dialog "Emerald update failed. Please install the downloaded update manually." buttons {"OK"} default button 1 with icon caution' >/dev/null 2>&1 || true
    exit 1
}

echo "Installing Emerald $version"
[ -f "$dmg" ] || fail "Missing dmg: $dmg"
[ -n "$target" ] || fail "Missing target app path"

while /bin/kill -0 "$pid" >/dev/null 2>&1; do
    /bin/sleep 0.2
done

mount_point="$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/emerald-update-mount.XXXXXX")" || fail "Could not create mount point"
attached=0
cleanup() {
    if [ "$attached" -eq 1 ]; then
        /usr/bin/hdiutil detach "$mount_point" -quiet || /usr/bin/hdiutil detach "$mount_point" -force -quiet || true
    fi
    /bin/rm -rf "$mount_point"
}
trap cleanup EXIT INT TERM

/usr/bin/hdiutil attach "$dmg" -nobrowse -readonly -mountpoint "$mount_point" || fail "Could not mount dmg"
attached=1

source_app="$mount_point/Emerald.app"
if [ ! -d "$source_app" ]; then
    source_app="$(/usr/bin/find "$mount_point" -maxdepth 1 -name "*.app" -type d -print -quit)"
fi
[ -n "$source_app" ] && [ -d "$source_app" ] || fail "No app bundle found in dmg"

parent="$(/usr/bin/dirname "$target")"
[ -d "$parent" ] || fail "Target parent does not exist: $parent"

backup="${target}.previous-update"
/bin/rm -rf "$backup" || fail "Could not clear previous backup"
if [ -e "$target" ]; then
    /bin/mv "$target" "$backup" || fail "Could not move existing app bundle"
fi

if /usr/bin/ditto "$source_app" "$target"; then
    /bin/rm -rf "$backup"
else
    copy_rc=$?
    /bin/rm -rf "$target"
    if [ -e "$backup" ]; then
        /bin/mv "$backup" "$target"
    fi
    fail "Could not copy new app bundle: $copy_rc"
fi

/usr/bin/xattr -dr com.apple.quarantine "$target" >/dev/null 2>&1 || true
/usr/bin/open "$target" || fail "Could not restart Emerald"

/bin/rm -f "$dmg"
/bin/rm -f "$0"
exit 0
)SH";
}
#endif

} // namespace

Updater::Updater(QWidget *window)
    : QObject(window), m_window(window), m_net(new QNetworkAccessManager(this)) {}

void Updater::check(UpdateChannel::Channel channel, CheckMode mode) {
    if (m_busy)
        return;
    m_busy = true;

    const char *api = channel == UpdateChannel::Channel::Development
                          ? kDevelopmentReleaseApi
                          : kStableReleaseApi;
    QNetworkRequest req((QUrl(QString::fromLatin1(api))));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(kReleaseCheckTimeoutMs);
    prepare(req);

    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, channel, mode] {
                onReleaseReply(reply, channel, mode);
            });
}

void Updater::onReleaseReply(QNetworkReply *reply,
                             UpdateChannel::Channel channel, CheckMode mode) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_busy = false;
        if (mode == CheckMode::Manual)
            QMessageBox::warning(m_window, tr("Check for Updates"),
                                 tr("Couldn't check for updates:\n%1")
                                     .arg(reply->errorString()));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        m_busy = false;
        if (mode == CheckMode::Manual)
            QMessageBox::warning(
                m_window, tr("Check for Updates"),
                tr("GitHub returned invalid release information."));
        return;
    }

    const QJsonObject obj = UpdateChannel::selectRelease(document, channel);
    if (obj.isEmpty()) {
        m_busy = false;
        if (mode == CheckMode::Manual)
            QMessageBox::warning(
                m_window, tr("Check for Updates"),
                tr("No published Emerald releases were found for the selected "
                   "release channel."));
        return;
    }

    processRelease(obj, channel, mode);
}

void Updater::processRelease(const QJsonObject &release,
                             UpdateChannel::Channel channel, CheckMode mode) {
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    const QString latest = UpdateChannel::normalizedVersion(tag);
    const QString current =
        UpdateChannel::normalizedVersion(QApplication::applicationVersion());
    if (current.isEmpty()) {
        m_busy = false;
        if (mode == CheckMode::Manual)
            QMessageBox::warning(
                m_window, tr("Check for Updates"),
                tr("This build has an invalid version and cannot be updated "
                   "automatically."));
        return;
    }

    if (latest.isEmpty() ||
        UpdateChannel::compareVersions(latest, current) <= 0) {
        m_busy = false;
        if (mode == CheckMode::Startup)
            return;
        const QString channelName =
            channel == UpdateChannel::Channel::Development
                ? tr("Development")
                : tr("Stable");
        QMessageBox status(QMessageBox::NoIcon, tr("Check for Updates"),
                           tr("No newer %1 release is available (installed v%2).")
                               .arg(channelName, current),
                           QMessageBox::Ok, m_window);
        status.setObjectName(QStringLiteral("updateStatusDialog"));
        status.setProperty("emeraldDialog", true);
        status.exec();
        return;
    }

    // A startup response can arrive while What's New, Settings, or another
    // window-owned dialog is open. Keep the result and wait instead of stacking
    // a second prompt on top of the user's current task.
    if (mode == CheckMode::Startup && startupNotificationBlocked(m_window)) {
        QTimer::singleShot(500, this, [this, release, channel, mode] {
            processRelease(release, channel, mode);
        });
        return;
    }
    m_busy = false;

    // Find the download URL for this platform's asset.
    const QString assetName = platformAssetName();
    QString url;
    QByteArray expectedSha256;
    qint64 expectedSize = -1;
    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &v : assets) {
        const QJsonObject a = v.toObject();
        if (a.value(QStringLiteral("name")).toString() == assetName) {
            url = a.value(QStringLiteral("browser_download_url")).toString();
            expectedSha256 =
                sha256FromDigest(a.value(QStringLiteral("digest")).toString());
            expectedSize = a.value(QStringLiteral("size")).toVariant().toLongLong();
            break;
        }
    }

    // No matching asset (unusual): fall back to the release page in the browser.
    if (url.isEmpty()) {
        const QString page =
            release.value(QStringLiteral("html_url")).toString();
        if (QMessageBox::information(
                m_window, tr("Update Available"),
                tr("Emerald v%1 is available — you have v%2.\n\n"
                   "Open the download page?").arg(latest, current),
                QMessageBox::Open | QMessageBox::Cancel) == QMessageBox::Open &&
            isTrustedReleaseUrl(QUrl(page)))
            QDesktopServices::openUrl(QUrl(page));
        return;
    }

    const QUrl assetUrl(url);
    if (!isTrustedReleaseUrl(assetUrl) || expectedSha256.isEmpty() ||
        expectedSize <= 0) {
        const QUrl page(
            release.value(QStringLiteral("html_url")).toString());
        QMessageBox warning(
            QMessageBox::Warning, tr("Update Verification Unavailable"),
            tr("Emerald v%1 does not provide valid package verification "
               "metadata. For your safety, it will not be downloaded or "
               "installed automatically.")
                .arg(latest),
            QMessageBox::NoButton, m_window);
        warning.setObjectName(QStringLiteral("updateVerificationDialog"));
        warning.setProperty("emeraldDialog", true);
        QPushButton *openPage = nullptr;
        if (isTrustedReleaseUrl(page)) {
            warning.setInformativeText(tr("You can open the release page and "
                                          "download it manually."));
            openPage = warning.addButton(QMessageBox::Open);
            warning.addButton(QMessageBox::Cancel);
        } else {
            warning.addButton(QMessageBox::Ok);
        }
        warning.exec();
        if (openPage && warning.clickedButton() == openPage)
            QDesktopServices::openUrl(page);
        return;
    }

    // Show the release notes and offer to install.
    QMessageBox box(QMessageBox::Information, tr("Update Available"),
                    tr("Emerald v%1 is available — you have v%2.")
                        .arg(latest, current),
                    QMessageBox::NoButton, m_window);
    box.setObjectName(QStringLiteral("updateDialog"));
    box.setProperty("emeraldDialog", true);
    QString notes =
        release.value(QStringLiteral("body")).toString().trimmed();
    if (notes.size() > 1200)
        notes = notes.left(1200) + QStringLiteral("…");
    if (!notes.isEmpty())
        box.setDetailedText(notes);
#if defined(Q_OS_LINUX)
    QPushButton *go =
        box.addButton(tr("Install && Restart"), QMessageBox::AcceptRole);
#elif defined(Q_OS_MACOS)
    QPushButton *go = box.addButton(tr("Install && Restart"), QMessageBox::AcceptRole);
#else
    QPushButton *go = box.addButton(tr("Download"), QMessageBox::AcceptRole);
#endif
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(go);
    box.exec();
    if (box.clickedButton() == go)
        startDownload(url, assetName, latest, expectedSha256, expectedSize);
}

void Updater::startDownload(const QString &url, const QString &assetName,
                            const QString &version,
                            const QByteArray &expectedSha256,
                            qint64 expectedSize) {
    m_busy = true;

    // Linux and macOS stage in a private temporary directory for a detached
    // installer helper. Windows keeps its installer in Downloads.
    QString savePath;
    QString stagePath;
#if defined(Q_OS_LINUX)
    stagePath = linuxUpdateStagePath();
    if (stagePath.isEmpty()) {
        m_busy = false;
        QMessageBox::warning(
            m_window, tr("Download Failed"),
            tr("Couldn't create a private directory to stage the Linux update."));
        return;
    }
    savePath = QDir(stagePath).filePath(assetName);
#elif defined(Q_OS_MACOS)
    stagePath = macUpdateStagePath();
    if (!stagePath.isEmpty())
        savePath = QDir(stagePath).filePath(assetName);
#endif
    if (savePath.isEmpty()) {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        savePath = QDir(dir).filePath(assetName);
    }

    auto *out = new QFile(savePath, this);
    if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete out;
        m_busy = false;
        QMessageBox::warning(
            m_window, tr("Download Failed"),
            tr("Couldn't save the download to:\n%1").arg(savePath));
        return;
    }

    auto *progress = new QProgressDialog(
        tr("Downloading Emerald v%1…").arg(version), tr("Cancel"), 0, 100, m_window);
    progress->setObjectName(QStringLiteral("updateProgressDialog"));
    progress->setProperty("emeraldDialog", true);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);

    QNetworkRequest req((QUrl(url)));
    prepare(req);
    QNetworkReply *reply = m_net->get(req);
    const auto writeFailed = std::make_shared<bool>(false);
    const auto sizeExceeded = std::make_shared<bool>(false);
    const auto receivedBytes = std::make_shared<qint64>(0);
    const auto hash =
        std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    const auto consume = [reply, out, writeFailed, sizeExceeded, receivedBytes,
                          hash, expectedSize] {
        const QByteArray bytes = reply->readAll();
        if (bytes.isEmpty())
            return;
        if (bytes.size() > expectedSize - *receivedBytes) {
            *sizeExceeded = true;
            reply->abort();
            return;
        }
        *receivedBytes += bytes.size();
        hash->addData(bytes);
        if (out->write(bytes) != bytes.size())
            *writeFailed = true;
    };

    connect(reply, &QNetworkReply::downloadProgress, progress,
            [progress](qint64 received, qint64 total) {
                if (total > 0)
                    progress->setValue(int(received * 100 / total));
            });
    connect(reply, &QNetworkReply::readyRead, out, consume);
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, progress, out, writeFailed, sizeExceeded, hash, consume,
             savePath, stagePath, version, expectedSha256, expectedSize] {
                consume();
                if (!out->flush())
                    *writeFailed = true;
                out->close();
                out->deleteLater();
                reply->deleteLater();
                progress->deleteLater();
                m_busy = false;

                const auto discardDownload = [&] {
                    QFile::remove(savePath);
                    if (!stagePath.isEmpty())
                        QDir().rmdir(stagePath);
                };

                if (reply->error() != QNetworkReply::NoError) {
                    discardDownload();
                    if (*sizeExceeded) {
                        QMessageBox::critical(
                            m_window, tr("Update Verification Failed"),
                            tr("The update exceeded the size published by GitHub. "
                               "The partial file was deleted and will not be "
                               "opened or installed."));
                    } else if (reply->error() !=
                               QNetworkReply::OperationCanceledError) {
                        QMessageBox::warning(m_window, tr("Download Failed"),
                                             reply->errorString());
                    }
                    return;
                }

                if (*writeFailed) {
                    discardDownload();
                    QMessageBox::warning(
                        m_window, tr("Download Failed"),
                        tr("Couldn't save the download to:\n%1").arg(savePath));
                    return;
                }

                const bool verified = hash->result() == expectedSha256 &&
                                      QFileInfo(savePath).size() == expectedSize;
                if (!verified) {
                    discardDownload();
                    QMessageBox::critical(
                        m_window, tr("Update Verification Failed"),
                        tr("The downloaded update did not match the SHA-256 "
                           "digest and size published by GitHub. The file was "
                           "deleted and will not be opened or installed."));
                    return;
                }
                finishDownload(savePath, version);
            });
}

bool Updater::installLinuxUpdate(const QString &imagePath,
                                 const QString &version) {
#if defined(Q_OS_LINUX)
    const QString appImagePath = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    QString target =
        LinuxUpdate::installTarget(appImagePath, QDir::homePath());
    if (target.isEmpty()) {
        QMessageBox::warning(
            m_window, tr("Update Failed"),
            tr("Emerald couldn't determine a safe per-user installation path."));
        return false;
    }

    const auto canPrepareTarget = [](const QString &path) {
        const QString parent = QFileInfo(path).absolutePath();
        return QDir().mkpath(parent) && QFileInfo(parent).isDir() &&
               QFileInfo(parent).isWritable();
    };
    // A read-only AppImage may live in a system directory. Keep automatic
    // updates privilege-free by installing it into the user's bin directory.
    if (!canPrepareTarget(target) && !appImagePath.trimmed().isEmpty())
        target = LinuxUpdate::installTarget({}, QDir::homePath());

    if (target.isEmpty() || !canPrepareTarget(target)) {
        QMessageBox::warning(
            m_window, tr("Update Failed"),
            tr("Emerald can't write to the installation folder:\n%1")
                .arg(QFileInfo(target).absolutePath()));
        return false;
    }

    const QDir stageDir(QFileInfo(imagePath).absolutePath());
    const QString scriptPath =
        stageDir.filePath(QStringLiteral("install-emerald-update.sh"));
    const QString logPath = stageDir.filePath(QStringLiteral("install.log"));

    const QByteArray installer = LinuxUpdate::installerScript();
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        script.write(installer) != installer.size()) {
        script.close();
        QFile::remove(scriptPath);
        QMessageBox::warning(
            m_window, tr("Update Failed"),
            tr("Couldn't stage the Linux installer helper.\n\nThe verified "
               "AppImage remains at:\n%1")
                .arg(imagePath));
        return false;
    }
    script.close();
    QFile::setPermissions(scriptPath,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner);

    const QStringList args{scriptPath,
                           QString::number(QCoreApplication::applicationPid()),
                           imagePath,
                           target,
                           version,
                           logPath};
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), args)) {
        QMessageBox::warning(
            m_window, tr("Update Failed"),
            tr("Couldn't start the Linux installer helper.\n\nThe verified "
               "AppImage remains at:\n%1")
                .arg(imagePath));
        return false;
    }

    if (m_window)
        m_window->close();
    QApplication::quit();
    return true;
#else
    Q_UNUSED(imagePath)
    Q_UNUSED(version)
    return false;
#endif
}

bool Updater::installMacUpdate(const QString &dmgPath, const QString &version) {
#if defined(Q_OS_MACOS)
    const QString appBundle = currentMacAppBundlePath();
    if (appBundle.isEmpty()) {
        QMessageBox::warning(
            m_window, tr("Update Downloaded"),
            tr("Emerald isn't running from a macOS app bundle, so it can't install "
               "this update automatically.\n\nThe disk image will open now."));
        QDesktopServices::openUrl(QUrl::fromLocalFile(dmgPath));
        return true;
    }

    const QFileInfo appInfo(appBundle);
    const QFileInfo parentInfo(appInfo.absolutePath());
    if (!parentInfo.isWritable()) {
        QMessageBox::warning(
            m_window, tr("Update Downloaded"),
            tr("Emerald can't write to:\n%1\n\nThe disk image will open now so you "
               "can install the update manually.")
                .arg(parentInfo.absoluteFilePath()));
        QDesktopServices::openUrl(QUrl::fromLocalFile(dmgPath));
        return true;
    }

    const QDir stageDir(QFileInfo(dmgPath).absolutePath());
    const QString scriptPath = stageDir.filePath(QStringLiteral("install-emerald-update.sh"));
    const QString logPath = stageDir.filePath(QStringLiteral("install.log"));

    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(
            m_window, tr("Update Failed"),
            tr("Couldn't stage the macOS installer helper.\n\nThe disk image will "
               "open now."));
        QDesktopServices::openUrl(QUrl::fromLocalFile(dmgPath));
        return true;
    }
    script.write(macInstallerScript());
    script.close();
    QFile::setPermissions(scriptPath,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                              QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                              QFileDevice::ExeGroup | QFileDevice::ReadOther |
                              QFileDevice::ExeOther);

    const QStringList args{scriptPath,
                           QString::number(QCoreApplication::applicationPid()),
                           dmgPath,
                           appBundle,
                           version,
                           logPath};
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), args)) {
        QMessageBox::warning(
            m_window, tr("Update Failed"),
            tr("Couldn't start the macOS installer helper.\n\nThe disk image will "
               "open now."));
        QDesktopServices::openUrl(QUrl::fromLocalFile(dmgPath));
        return true;
    }

    if (m_window)
        m_window->close();
    QApplication::quit();
    return true;
#else
    Q_UNUSED(dmgPath)
    Q_UNUSED(version)
    return false;
#endif
}

void Updater::finishDownload(const QString &savedPath, const QString &version) {
#if defined(Q_OS_LINUX)
    if (installLinuxUpdate(savedPath, version))
        return;
#endif
#if defined(Q_OS_MACOS)
    if (savedPath.endsWith(QStringLiteral(".dmg"), Qt::CaseInsensitive) &&
        installMacUpdate(savedPath, version))
        return;
#endif
    // Hand the verified package to the OS if managed installation is
    // unavailable, or when the platform normally uses an interactive installer.
    QMessageBox::information(
        m_window, tr("Download Complete"),
        tr("Emerald v%1 was downloaded to:\n%2\n\nIt will open now — follow the "
           "usual steps for your platform to finish updating.")
            .arg(version, savedPath));
    QDesktopServices::openUrl(QUrl::fromLocalFile(savedPath));
}
