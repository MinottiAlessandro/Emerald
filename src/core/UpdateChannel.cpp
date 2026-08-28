#include "UpdateChannel.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

namespace {

struct SemanticVersion {
    QString major;
    QString minor;
    QString patch;
    QStringList prerelease;
    bool valid = false;
};

bool isNumericIdentifier(const QString &value) {
    if (value.isEmpty())
        return false;
    for (const QChar character : value)
        if (!character.isDigit())
            return false;
    return true;
}

int compareNumericIdentifier(const QString &left, const QString &right) {
    if (left.size() != right.size())
        return left.size() < right.size() ? -1 : 1;
    const int order = QString::compare(left, right, Qt::CaseSensitive);
    return order < 0 ? -1 : order > 0 ? 1 : 0;
}

SemanticVersion parseVersion(QString value) {
    value = value.trimmed();
    if (value.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        value.remove(0, 1);

    static const QRegularExpression pattern(QStringLiteral(
        "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"
        "(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?"
        "(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?$"));
    const QRegularExpressionMatch match = pattern.match(value);
    if (!match.hasMatch())
        return {};

    SemanticVersion version;
    version.major = match.captured(1);
    version.minor = match.captured(2);
    version.patch = match.captured(3);
    const QString prerelease = match.captured(4);
    if (!prerelease.isEmpty()) {
        version.prerelease = prerelease.split(QLatin1Char('.'));
        for (const QString &identifier : version.prerelease) {
            if (isNumericIdentifier(identifier) && identifier.size() > 1 &&
                identifier.startsWith(QLatin1Char('0')))
                return {}; // SemVer numeric identifiers cannot have leading zeroes.
        }
    }
    version.valid = true;
    return version;
}

int compareParsedVersions(const SemanticVersion &left,
                          const SemanticVersion &right) {
    for (const auto member : {&SemanticVersion::major, &SemanticVersion::minor,
                              &SemanticVersion::patch}) {
        const int order = compareNumericIdentifier(left.*member, right.*member);
        if (order != 0)
            return order;
    }

    if (left.prerelease.isEmpty() || right.prerelease.isEmpty()) {
        if (left.prerelease.isEmpty() == right.prerelease.isEmpty())
            return 0;
        return left.prerelease.isEmpty() ? 1 : -1;
    }

    const int common = qMin(left.prerelease.size(), right.prerelease.size());
    for (int i = 0; i < common; ++i) {
        const QString &a = left.prerelease.at(i);
        const QString &b = right.prerelease.at(i);
        const bool aNumeric = isNumericIdentifier(a);
        const bool bNumeric = isNumericIdentifier(b);
        if (aNumeric && bNumeric) {
            const int order = compareNumericIdentifier(a, b);
            if (order != 0)
                return order;
        } else if (aNumeric != bNumeric) {
            return aNumeric ? -1 : 1;
        } else {
            const int order = QString::compare(a, b, Qt::CaseSensitive);
            if (order != 0)
                return order < 0 ? -1 : 1;
        }
    }
    if (left.prerelease.size() == right.prerelease.size())
        return 0;
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

bool isPrereleaseVersion(const QString &version) {
    const SemanticVersion parsed = parseVersion(version);
    return parsed.valid && !parsed.prerelease.isEmpty();
}

QString releaseTag(const QJsonObject &release) {
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    if (!tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        return {};
    return UpdateChannel::normalizedVersion(tag);
}

} // namespace

namespace UpdateChannel {

QString key(Channel channel) {
    return channel == Channel::Development ? QStringLiteral("development")
                                           : QStringLiteral("stable");
}

Channel fromKey(const QString &value) {
    return value.compare(QStringLiteral("development"), Qt::CaseInsensitive) == 0
               ? Channel::Development
               : Channel::Stable;
}

QString normalizedVersion(const QString &value) {
    QString normalized = value.trimmed();
    if (normalized.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        normalized.remove(0, 1);
    return parseVersion(normalized).valid ? normalized : QString();
}

int compareVersions(const QString &left, const QString &right) {
    const SemanticVersion a = parseVersion(left);
    const SemanticVersion b = parseVersion(right);
    if (!a.valid || !b.valid)
        return 0;
    return compareParsedVersions(a, b);
}

QJsonObject selectRelease(const QJsonDocument &document, Channel channel) {
    QJsonArray releases;
    if (document.isArray())
        releases = document.array();
    else if (document.isObject())
        releases.append(document.object());

    QJsonObject selected;
    QString selectedVersion;
    for (const QJsonValue &value : releases) {
        const QJsonObject release = value.toObject();
        if (release.isEmpty() ||
            release.value(QStringLiteral("draft")).toBool())
            continue;

        const QString version = releaseTag(release);
        if (version.isEmpty())
            continue;
        const bool versionIsPrerelease = isPrereleaseVersion(version);
        const bool markedPrerelease =
            release.value(QStringLiteral("prerelease")).toBool();
        if (channel == Channel::Stable &&
            (versionIsPrerelease || markedPrerelease))
            continue;

        if (selected.isEmpty() ||
            compareVersions(version, selectedVersion) > 0) {
            selected = release;
            selectedVersion = version;
        }
    }
    return selected;
}

} // namespace UpdateChannel
