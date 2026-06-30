#pragma once

#include <QObject>

class QWidget;
class QNetworkAccessManager;
class QNetworkReply;

// Manual "Check for Updates": queries the latest GitHub release and, when a
// newer version exists, downloads its platform asset and installs it.
//   Linux (running as an AppImage): replace the AppImage in place, then offer to
//     relaunch. The running process keeps its open handle to the old inode, so
//     overwriting the file's path is safe.
//   macOS: download the dmg to a staging directory, launch a small system-shell
//     helper, quit Emerald, replace the current .app bundle, and relaunch.
//   Windows / non-AppImage Linux: download the .zip/AppImage and open it,
//     leaving the final extract/chmod step to the user.
// All dialogs parent to the window passed in. One instance is reused for the
// app's lifetime; check() is a no-op while a check or download is in flight.
class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QWidget *window);

    void check();

private:
    void onReleaseReply(QNetworkReply *reply);
    void startDownload(const QString &url, const QString &assetName,
                       const QString &version);
    bool installMacUpdate(const QString &dmgPath, const QString &version);
    void finishDownload(const QString &savedPath, const QString &version);

    QWidget *m_window;
    QNetworkAccessManager *m_net;
    bool m_busy = false;
};
