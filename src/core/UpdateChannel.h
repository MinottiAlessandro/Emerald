#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace UpdateChannel {

inline constexpr char SettingKey[] = "updateChannel";

enum class Channel {
    Stable,
    Development,
};

QString key(Channel channel);
Channel fromKey(const QString &value);

// Emerald release tags use SemVer (v2.3.0 or v2.3.0-dev.1). Build metadata is
// accepted for comparisons but does not affect precedence.
QString normalizedVersion(const QString &value);
int compareVersions(const QString &left, const QString &right);

// Pick the highest valid Emerald application release from either a single
// GitHub release object or the array returned by /releases. Drafts, unrelated
// tags (for example spelling packs), and prereleases outside the selected
// channel are ignored. Development includes later stable releases as well.
QJsonObject selectRelease(const QJsonDocument &document, Channel channel);

} // namespace UpdateChannel
