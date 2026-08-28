#pragma once

#include <QByteArray>

namespace RuntimeEnvironment {

// The AppImage currently bundles Qt's XCB platform plugin. Empty/default and
// fallback-list requests should therefore select XCB directly; a single
// explicit platform remains an intentional user override.
QByteArray appImageQpaOverride(const QByteArray &appImagePath,
                               const QByteArray &requestedPlatform);

bool shouldEnableWidgetsRhi(const QByteArray &appImagePath,
                            bool explicitlyConfigured);

// Apply package-sensitive Qt environment defaults before QApplication exists.
void configureBeforeApplication();

} // namespace RuntimeEnvironment
