#include "Updater.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <memory>

namespace {

constexpr char kReleaseApi[] =
    "https://api.github.com/repos/MinottiAlessandro/Emerald/releases/latest";

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

// Dotted numeric compare ("1.10.0" > "1.9.0"). >0 if a is newer than b.
int compareVersions(const QString &a, const QString &b) {
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int va = i < pa.size() ? pa[i].toInt() : 0;
        const int vb = i < pb.size() ? pb[i].toInt() : 0;
        if (va != vb)
            return va - vb;
    }
    return 0;
}

// The release-asset filename this build should download. Mirrors the
// version-less names produced by the Release workflow / CPack.
QString platformAssetName() {
#if defined(Q_OS_WIN)
    return QStringLiteral("Emerald-win64.zip");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Emerald-macOS.dmg");
#else
    const QString arch = QSysInfo::currentCpuArchitecture();
    return (arch.contains(QLatin1String("arm")) || arch.contains(QLatin1String("aarch")))
               ? QStringLiteral("Emerald-aarch64.AppImage")
               : QStringLiteral("Emerald-x86_64.AppImage");
#endif
}

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

void Updater::check() {
    if (m_busy)
        return;
    m_busy = true;

    QNetworkRequest req((QUrl(QString::fromLatin1(kReleaseApi))));
    req.setRawHeader("Accept", "application/vnd.github+json");
    prepare(req);

    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onReleaseReply(reply); });
}

void Updater::onReleaseReply(QNetworkReply *reply) {
    reply->deleteLater();
    m_busy = false;

    if (reply->error() != QNetworkReply::NoError) {
        QMessageBox::warning(m_window, tr("Check for Updates"),
                             tr("Couldn't check for updates:\n%1")
                                 .arg(reply->errorString()));
        return;
    }

    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = obj.value(QStringLiteral("tag_name")).toString();
    const QString latest = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
    const QString current = QApplication::applicationVersion();
    // Development builds add a channel suffix (for example "-smooth") so the
    // UI identifies their origin immediately. Release comparison remains based
    // on the numeric package version; otherwise QString::toInt would interpret
    // the suffixed patch component as zero and offer the current release again.
    const QString currentRelease = current.section(QLatin1Char('-'), 0, 0);

    if (latest.isEmpty() || compareVersions(latest, currentRelease) <= 0) {
        QMessageBox::information(
            m_window, tr("Check for Updates"),
            tr("You're on the latest version (v%1).").arg(current));
        return;
    }

    // Find the download URL for this platform's asset.
    const QString assetName = platformAssetName();
    QString url;
    QByteArray expectedSha256;
    qint64 expectedSize = -1;
    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
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
        const QString page = obj.value(QStringLiteral("html_url")).toString();
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
        QMessageBox::warning(m_window, tr("Update Verification Unavailable"),
                             tr("Emerald v%1 does not provide valid package verification "
                                "metadata. For your safety, it will not be downloaded or "
                                "installed automatically.")
                                 .arg(latest));
        const QUrl page(obj.value(QStringLiteral("html_url")).toString());
        if (isTrustedReleaseUrl(page))
            QDesktopServices::openUrl(page);
        return;
    }

    // Show the release notes and offer to install.
    QMessageBox box(QMessageBox::Information, tr("Update Available"),
                    tr("Emerald v%1 is available — you have v%2.")
                        .arg(latest, current),
                    QMessageBox::NoButton, m_window);
    QString notes = obj.value(QStringLiteral("body")).toString().trimmed();
    if (notes.size() > 1200)
        notes = notes.left(1200) + QStringLiteral("…");
    if (!notes.isEmpty())
        box.setDetailedText(notes);
#if defined(Q_OS_LINUX)
    const bool inPlace = !qEnvironmentVariableIsEmpty("APPIMAGE");
    QPushButton *go = box.addButton(
        inPlace ? tr("Install && Restart") : tr("Download"), QMessageBox::AcceptRole);
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

    // Linux AppImage stages beside the running file so the final rename is a
    // same-filesystem atomic replace. macOS stages in temp for the helper script.
    // Everything else goes to Downloads, to be opened when finished.
    QString savePath;
#if defined(Q_OS_LINUX)
    const QByteArray appimage = qgetenv("APPIMAGE");
    if (!appimage.isEmpty())
        savePath = QString::fromLocal8Bit(appimage) + QStringLiteral(".download");
#elif defined(Q_OS_MACOS)
    const QString stagePath = macUpdateStagePath();
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
             savePath, version, expectedSha256, expectedSize] {
                consume();
                if (!out->flush())
                    *writeFailed = true;
                out->close();
                out->deleteLater();
                reply->deleteLater();
                progress->deleteLater();
                m_busy = false;

                if (reply->error() != QNetworkReply::NoError) {
                    QFile::remove(savePath);
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
                    QFile::remove(savePath);
                    QMessageBox::warning(
                        m_window, tr("Download Failed"),
                        tr("Couldn't save the download to:\n%1").arg(savePath));
                    return;
                }

                const bool verified = hash->result() == expectedSha256 &&
                                      QFileInfo(savePath).size() == expectedSize;
                if (!verified) {
                    QFile::remove(savePath);
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
    const QByteArray appimage = qgetenv("APPIMAGE");
    if (!appimage.isEmpty() && savedPath.endsWith(QStringLiteral(".download"))) {
        const QString target = QString::fromLocal8Bit(appimage);
        QFile::setPermissions(savedPath,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                  QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                  QFileDevice::ExeOther);
        // Drop the old path (its inode lives on for this running process) and
        // move the freshly downloaded image into its place.
        QFile::remove(target);
        if (!QFile::rename(savedPath, target)) {
            QFile::remove(savedPath);
            QMessageBox::warning(m_window, tr("Update Failed"),
                                 tr("Couldn't replace the application file at:\n%1")
                                     .arg(target));
            return;
        }
        if (QMessageBox::question(
                m_window, tr("Update Ready"),
                tr("Emerald v%1 is installed. Restart now?").arg(version)) ==
            QMessageBox::Yes) {
            QProcess::startDetached(target, {});
            QApplication::quit();
        }
        return;
    }
#endif
#if defined(Q_OS_MACOS)
    if (savedPath.endsWith(QStringLiteral(".dmg"), Qt::CaseInsensitive) &&
        installMacUpdate(savedPath, version))
        return;
#endif
    // macOS / Windows / non-AppImage Linux: hand the installer to the OS.
    QMessageBox::information(
        m_window, tr("Download Complete"),
        tr("Emerald v%1 was downloaded to:\n%2\n\nIt will open now — follow the "
           "usual steps for your platform to finish updating.")
            .arg(version, savedPath));
    QDesktopServices::openUrl(QUrl::fromLocalFile(savedPath));
}
