#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Emerald"));
    QApplication::setOrganizationName(QStringLiteral("Emerald"));

    QFile qss(QStringLiteral(":/emerald.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    MainWindow window;
    window.resize(1100, 720);
    window.show();
    return app.exec();
}
