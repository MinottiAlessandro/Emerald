#include "Updater.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDevice>
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

    if (latest.isEmpty() || compareVersions(latest, current) <= 0) {
        QMessageBox::information(
            m_window, tr("Check for Updates"),
            tr("You're on the latest version (v%1).").arg(current));
        return;
    }

    // Find the download URL for this platform's asset.
    const QString assetName = platformAssetName();
    QString url;
    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &v : assets) {
        const QJsonObject a = v.toObject();
        if (a.value(QStringLiteral("name")).toString() == assetName) {
            url = a.value(QStringLiteral("browser_download_url")).toString();
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
            !page.isEmpty())
            QDesktopServices::openUrl(QUrl(page));
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
#else
    QPushButton *go = box.addButton(tr("Download"), QMessageBox::AcceptRole);
#endif
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(go);
    box.exec();
    if (box.clickedButton() == go)
        startDownload(url, assetName, latest);
}

void Updater::startDownload(const QString &url, const QString &assetName,
                            const QString &version) {
    m_busy = true;

    // Linux AppImage: stage the download beside the running file so the final
    // rename is a same-filesystem atomic replace. Everything else goes to the
    // user's Downloads folder, to be opened when finished.
    QString savePath;
#if defined(Q_OS_LINUX)
    const QByteArray appimage = qgetenv("APPIMAGE");
    if (!appimage.isEmpty())
        savePath = QString::fromLocal8Bit(appimage) + QStringLiteral(".download");
#endif
    if (savePath.isEmpty()) {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        savePath = QDir(dir).filePath(assetName);
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

    connect(reply, &QNetworkReply::downloadProgress, progress,
            [progress](qint64 received, qint64 total) {
                if (total > 0)
                    progress->setValue(int(received * 100 / total));
            });
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, progress, savePath, version] {
                reply->deleteLater();
                progress->deleteLater();
                m_busy = false;

                if (reply->error() != QNetworkReply::NoError) {
                    if (reply->error() != QNetworkReply::OperationCanceledError)
                        QMessageBox::warning(m_window, tr("Download Failed"),
                                             reply->errorString());
                    return;
                }

                QFile f(savePath);
                if (!f.open(QIODevice::WriteOnly) ||
                    f.write(reply->readAll()) < 0) {
                    QMessageBox::warning(
                        m_window, tr("Download Failed"),
                        tr("Couldn't save the download to:\n%1").arg(savePath));
                    return;
                }
                f.close();
                finishDownload(savePath, version);
            });
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
    // macOS / Windows / non-AppImage Linux: hand the installer to the OS.
    QMessageBox::information(
        m_window, tr("Download Complete"),
        tr("Emerald v%1 was downloaded to:\n%2\n\nIt will open now — follow the "
           "usual steps for your platform to finish updating.")
            .arg(version, savedPath));
    QDesktopServices::openUrl(QUrl::fromLocalFile(savedPath));
}
