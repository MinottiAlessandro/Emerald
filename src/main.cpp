#include "ui/MainWindow.h"
#include "ui/AppTheme.h"

#include <QApplication>
#include <QByteArray>
#include <QIcon>
#include <QLoggingCategory>
#include <QSettings>

Q_LOGGING_CATEGORY(emeraldPerf, "emerald.perf")

int main(int argc, char *argv[]) {
    // Composite the widget backing store on the GPU (Qt's RHI path). This makes
    // window resizing smooth on Wayland at zero dependency cost. Must be set
    // before QApplication is constructed; override with QT_WIDGETS_RHI=0.
    if (!qEnvironmentVariableIsSet("QT_WIDGETS_RHI"))
        qputenv("QT_WIDGETS_RHI", QByteArrayLiteral("1"));

    QApplication app(argc, argv);
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

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    return app.exec();
}
