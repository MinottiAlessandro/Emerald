#pragma once

#include <QByteArray>
#include <QString>

namespace LinuxUpdate {

// AppImages update at their current path. Native/user builds are promoted to
// the conventional per-user executable so package-manager-owned files are
// never overwritten and no elevated privileges are required.
QString installTarget(const QString &appImagePath, const QString &homePath);

// A detached helper must perform the final replacement after Emerald exits.
// It installs through a same-directory temporary file, keeps a rollback copy,
// and relaunches the installed AppImage.
QByteArray installerScript();

} // namespace LinuxUpdate
