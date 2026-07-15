#pragma once

#include <QString>
#include <QUrl>

// Trust-boundary helpers for content controlled by Markdown notes.
namespace ContentSecurity {

// Convert a Markdown link target into a URL that is safe to hand to the
// operating system. Only web links and email links are accepted.
QUrl externalUrl(const QString &target);

// Resolve an image target against the current note folder. The returned path
// is canonical, names an existing regular file, and remains beneath the
// canonical vault root. Remote URLs and symlinks that escape are rejected.
QString resolveLocalImage(const QString &target, const QString &basePath,
                          const QString &vaultRoot);

} // namespace ContentSecurity
