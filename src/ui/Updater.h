#pragma once

#include "core/UpdateChannel.h"

#include <QByteArray>
#include <QObject>

class QWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;

// Queries the selected Stable or Development GitHub release channel and, when
// a newer version exists, downloads its platform asset, verifies its SHA-256
// digest and byte size against GitHub's release metadata, and only then
// installs or opens it. Startup checks are quiet unless an update is available;
// manual checks also report errors and confirm when the app is current.
//   Linux: stage a verified AppImage, then use a detached helper to safely
//     replace the running AppImage or install it to ~/.local/bin/emerald. The
//     helper rolls back on failure and relaunches Emerald without sudo.
//   macOS: download the dmg to a staging directory, launch a small system-shell
//     helper, quit Emerald, replace the current .app bundle, and relaunch.
//   Windows: download the installer and open it for the user.
// All dialogs parent to the window passed in. One instance is reused for the
// app's lifetime; check() is a no-op while a check or download is in flight.
class Updater : public QObject {
    Q_OBJECT
public:
    enum class CheckMode {
        Manual,
        Startup,
    };

    explicit Updater(QWidget *window);

    void check(UpdateChannel::Channel channel,
               CheckMode mode = CheckMode::Manual);

private:
    void onReleaseReply(QNetworkReply *reply, UpdateChannel::Channel channel,
                        CheckMode mode);
    void processRelease(const QJsonObject &release,
                        UpdateChannel::Channel channel, CheckMode mode);
    void startDownload(const QString &url, const QString &assetName,
                       const QString &version, const QByteArray &expectedSha256,
                       qint64 expectedSize);
    bool installLinuxUpdate(const QString &imagePath, const QString &version);
    bool installMacUpdate(const QString &dmgPath, const QString &version);
    void finishDownload(const QString &savedPath, const QString &version);

    QWidget *m_window;
    QNetworkAccessManager *m_net;
    bool m_busy = false;
};
