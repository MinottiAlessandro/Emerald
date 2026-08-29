#include "ui/MainWindow.h"
#include "ui/AppTheme.h"
#include "core/RuntimeEnvironment.h"
#include "core/StandaloneFile.h"

#include <QApplication>
#include <QByteArray>
#include <QEvent>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QLoggingCategory>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <functional>

Q_LOGGING_CATEGORY(emeraldPerf, "emerald.perf")

namespace {
void scheduleStartupUpdateCheck() {
    static bool scheduled = false;
    if (scheduled)
        return;
    scheduled = true;

    // Let the first window finish painting and restoring its vault before the
    // asynchronous network request starts. Select a live window at delivery
    // time so closing the window that initiated startup cannot leave a stale
    // pointer behind.
    QTimer::singleShot(1500, qApp, [] {
        if (auto *active =
                qobject_cast<MainWindow *>(QApplication::activeWindow())) {
            active->checkForUpdatesOnStartup();
            return;
        }
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            auto *window = qobject_cast<MainWindow *>(widget);
            if (window && window->isVisible()) {
                window->checkForUpdatesOnStartup();
                return;
            }
        }
    });
}

class EmeraldApplication final : public QApplication {
public:
    EmeraldApplication(int &argc, char **argv) : QApplication(argc, argv) {}

    void setFileOpenHandler(std::function<void(const QString &)> handler) {
        m_fileOpenHandler = std::move(handler);
        const QStringList pending = std::move(m_pendingFiles);
        m_pendingFiles.clear();
        for (const QString &path : pending)
            m_fileOpenHandler(path);
    }

protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::FileOpen) {
            auto *openEvent = static_cast<QFileOpenEvent *>(event);
            QString path;
            if (openEvent->url().isLocalFile())
                path = openEvent->url().toLocalFile();
            else
                path = openEvent->file();
            if (!path.isEmpty()) {
                if (m_fileOpenHandler)
                    m_fileOpenHandler(path);
                else
                    m_pendingFiles.append(path);
            }
            event->accept();
            return true;
        }
        return QApplication::event(event);
    }

private:
    QStringList m_pendingFiles;
    std::function<void(const QString &)> m_fileOpenHandler;
};

bool openStandaloneWindow(const QString &requestedPath) {
    StandaloneFile::Document document;
    if (!StandaloneFile::load(requestedPath, &document)) {
        // MainWindow presents the detailed validation/read error in its normal
        // in-app notification surface; do not fail as an invisible GUI process.
        auto *window = new MainWindow(requestedPath);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->resize(1100, 720);
        window->show();
        scheduleStartupUpdateCheck();
        return true;
    }

    // macOS can deliver the same Finder request more than once. Focus the
    // existing document window rather than letting two editors autosave over
    // one another inside this process.
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        auto *window = qobject_cast<MainWindow *>(widget);
        if (window && window->isStandaloneFile() &&
            QFileInfo(window->standalonePath()).canonicalFilePath() ==
                QFileInfo(document.path).canonicalFilePath()) {
            window->showNormal();
            window->raise();
            window->activateWindow();
            return true;
        }
    }

    auto *window = new MainWindow(document.path);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->resize(1100, 720);
    window->show();
    scheduleStartupUpdateCheck();
    return true;
}

bool hasMainWindow() {
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (qobject_cast<MainWindow *>(widget))
            return true;
    return false;
}
} // namespace

int main(int argc, char *argv[]) {
    RuntimeEnvironment::configureBeforeApplication();

    EmeraldApplication app(argc, argv);
    // Show the shortcut label beside every menu action. macOS defaults this
    // attribute to true, which hides accelerators in non-menubar menus — and
    // Emerald's only menu is the gear popup, so without this its actions would
    // carry no visible shortcuts.
    QApplication::setAttribute(Qt::AA_DontShowShortcutsInContextMenus, false);
    QApplication::setApplicationName(QStringLiteral("Emerald"));
    QApplication::setOrganizationName(QStringLiteral("Emerald"));
    QApplication::setApplicationVersion(QStringLiteral(EMERALD_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/EmeraldClean.png")));

    const QSettings settings;
    AppTheme::apply(app,
                    settings.value(QStringLiteral("theme"),
                                   QStringLiteral("dark"))
                        .toString());

    app.setFileOpenHandler(openStandaloneWindow);

    // Linux and Windows file associations launch Emerald with a local path.
    // Accept more than one positional path for terminal use, although the
    // packaged desktop entry deliberately requests one process per file.
    bool openedArgument = false;
    const QStringList arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        if (argument.startsWith(QLatin1Char('-')))
            continue; // Qt/platform switches are not document paths
        if (!StandaloneFile::hasMarkdownExtension(argument) ||
            !QFileInfo(argument).isFile())
            continue;
        openedArgument = openStandaloneWindow(argument) || openedArgument;
    }

    // Finder may deliver QFileOpenEvent only once the event loop begins. Delay
    // the ordinary vault window for one turn so a document activation does not
    // briefly restore an unrelated vault beside the requested standalone file.
    QTimer::singleShot(0, &app, [openedArgument] {
        if (openedArgument || hasMainWindow())
            return;
        auto *window = new MainWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->resize(1100, 720);
        window->show();
        scheduleStartupUpdateCheck();
    });
    return app.exec();
}
