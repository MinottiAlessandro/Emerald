#include "core/RuntimeEnvironment.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {
int failures = 0;

void check(bool condition, const QString &message) {
    if (condition)
        return;
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++failures;
}

struct EnvironmentValue {
    QByteArray name;
    bool wasSet;
    QByteArray value;

    explicit EnvironmentValue(const char *variable)
        : name(variable), wasSet(qEnvironmentVariableIsSet(variable)),
          value(qgetenv(variable)) {}

    ~EnvironmentValue() {
        if (wasSet)
            qputenv(name.constData(), value);
        else
            qunsetenv(name.constData());
    }
};

void testAppImagePlatformSelection() {
    using RuntimeEnvironment::appImageQpaOverride;

    check(appImageQpaOverride({}, {}).isEmpty(),
          QStringLiteral("native launches do not override the QPA platform"));
    check(appImageQpaOverride(QByteArrayLiteral("/tmp/Emerald.AppImage"), {}) ==
              QByteArrayLiteral("xcb"),
          QStringLiteral("AppImages default to their bundled XCB plugin"));
    check(appImageQpaOverride(QByteArrayLiteral("/tmp/Emerald.AppImage"),
                              QByteArrayLiteral("wayland;xcb")) ==
              QByteArrayLiteral("xcb"),
          QStringLiteral("desktop fallback lists select bundled XCB directly"));
    check(appImageQpaOverride(QByteArrayLiteral("/tmp/Emerald.AppImage"),
                              QByteArrayLiteral("wayland"))
              .isEmpty() &&
              appImageQpaOverride(QByteArrayLiteral("/tmp/Emerald.AppImage"),
                                  QByteArrayLiteral("xcb"))
                  .isEmpty(),
          QStringLiteral("single explicit platform choices are preserved"));
}

void testWidgetRhiDefault() {
    using RuntimeEnvironment::shouldEnableWidgetsRhi;

    check(shouldEnableWidgetsRhi({}, false),
          QStringLiteral("native builds retain GPU widget compositing"));
    check(!shouldEnableWidgetsRhi(QByteArrayLiteral("/tmp/Emerald.AppImage"),
                                  false),
          QStringLiteral("AppImages retain Qt's raster default"));
    check(!shouldEnableWidgetsRhi({}, true) &&
              !shouldEnableWidgetsRhi(
                  QByteArrayLiteral("/tmp/Emerald.AppImage"), true),
          QStringLiteral("explicit RHI configuration is always preserved"));
}

void testEnvironmentConfiguration() {
    EnvironmentValue restoreAppImage("APPIMAGE");
    EnvironmentValue restoreQpa("QT_QPA_PLATFORM");
    EnvironmentValue restoreRhi("QT_WIDGETS_RHI");

    qputenv("APPIMAGE", QByteArrayLiteral("/tmp/Emerald.AppImage"));
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("wayland;xcb"));
    qunsetenv("QT_WIDGETS_RHI");
    RuntimeEnvironment::configureBeforeApplication();

#if defined(Q_OS_LINUX)
    check(qgetenv("QT_QPA_PLATFORM") == QByteArrayLiteral("xcb"),
          QStringLiteral("AppImage fallback list is applied before Qt starts"));
#else
    check(qgetenv("QT_QPA_PLATFORM") == QByteArrayLiteral("wayland;xcb"),
          QStringLiteral("non-Linux platform configuration is untouched"));
#endif
    check(!qEnvironmentVariableIsSet("QT_WIDGETS_RHI"),
          QStringLiteral("AppImage does not force the widget RHI"));

    qunsetenv("APPIMAGE");
    qunsetenv("QT_WIDGETS_RHI");
    RuntimeEnvironment::configureBeforeApplication();
    check(qgetenv("QT_WIDGETS_RHI") == QByteArrayLiteral("1"),
          QStringLiteral("native launch enables the established RHI default"));
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    testAppImagePlatformSelection();
    testWidgetRhiDefault();
    testEnvironmentConfiguration();
    if (failures == 0)
        QTextStream(stdout) << "All runtime environment tests passed.\n";
    return failures == 0 ? 0 : 1;
}
