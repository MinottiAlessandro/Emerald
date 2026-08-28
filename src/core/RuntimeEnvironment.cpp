#include "RuntimeEnvironment.h"

#include <QtGlobal>

namespace RuntimeEnvironment {

QByteArray appImageQpaOverride(const QByteArray &appImagePath,
                               const QByteArray &requestedPlatform) {
    if (appImagePath.isEmpty())
        return {};
    return requestedPlatform.isEmpty() ||
                   requestedPlatform.contains(';')
               ? QByteArrayLiteral("xcb")
               : QByteArray();
}

bool shouldEnableWidgetsRhi(const QByteArray &appImagePath,
                            bool explicitlyConfigured) {
    return appImagePath.isEmpty() && !explicitlyConfigured;
}

void configureBeforeApplication() {
    const QByteArray appImagePath = qgetenv("APPIMAGE");

#if defined(Q_OS_LINUX)
    const QByteArray qpaOverride =
        appImageQpaOverride(appImagePath, qgetenv("QT_QPA_PLATFORM"));
    if (!qpaOverride.isEmpty())
        qputenv("QT_QPA_PLATFORM", qpaOverride);
#endif

    // Qt Widgets normally uses its reliable raster backing store. Keep the GPU
    // path for native builds, where the matching platform integration is
    // present, but do not force it across the AppImage's XWayland boundary.
    if (shouldEnableWidgetsRhi(
            appImagePath, qEnvironmentVariableIsSet("QT_WIDGETS_RHI")))
        qputenv("QT_WIDGETS_RHI", QByteArrayLiteral("1"));
}

} // namespace RuntimeEnvironment
