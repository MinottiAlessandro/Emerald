#include "LinuxUpdate.h"

#include <QDir>
#include <QFileInfo>

namespace LinuxUpdate {

QString installTarget(const QString &appImagePath, const QString &homePath) {
    const QString image = appImagePath.trimmed();
    if (!image.isEmpty())
        return QFileInfo(image).absoluteFilePath();
    if (homePath.trimmed().isEmpty())
        return {};
    return QDir(homePath).absoluteFilePath(QStringLiteral(".local/bin/emerald"));
}

QByteArray installerScript() {
    return R"SH(#!/bin/sh
set -u

pid="$1"
asset="$2"
target="$3"
version="$4"
log="$5"

umask 077
exec >>"$log" 2>&1

parent="$(/usr/bin/dirname "$target")"
stage="$(/usr/bin/dirname "$asset")"
incoming="${target}.update-new"
backup="${target}.previous-update"
backup_moved=0

start_target() {
    if [ -x "$target" ]; then
        unset APPIMAGE APPDIR
        "$target" >/dev/null 2>&1 &
    fi
}

fail() {
    echo "ERROR: $*"
    /bin/rm -f "$incoming"
    if [ "$backup_moved" -eq 1 ] && [ -e "$backup" ]; then
        /bin/rm -f "$target"
        /bin/mv "$backup" "$target" || true
    fi
    start_target
    if command -v notify-send >/dev/null 2>&1; then
        notify-send "Emerald update failed" \
            "The previous version was restored. See $log for details." || true
    fi
    exit 1
}

echo "Installing Emerald $version to $target"
[ -f "$asset" ] || fail "Missing verified update: $asset"

while /bin/kill -0 "$pid" >/dev/null 2>&1; do
    /bin/sleep 0.2
done

/bin/mkdir -p "$parent" || fail "Could not create target directory: $parent"
/bin/rm -f "$incoming" "$backup" || fail "Could not clear update staging files"
/bin/cp "$asset" "$incoming" || fail "Could not copy update beside target"
/bin/chmod 755 "$incoming" || fail "Could not make update executable"

if [ -e "$target" ]; then
    /bin/mv "$target" "$backup" || fail "Could not back up existing Emerald"
    backup_moved=1
fi

if ! /bin/mv "$incoming" "$target"; then
    fail "Could not activate the downloaded update"
fi

backup_moved=0
start_target
/bin/rm -f "$backup" "$asset" "$0" "$log"
/bin/rmdir "$stage" >/dev/null 2>&1 || true
exit 0
)SH";
}

} // namespace LinuxUpdate
