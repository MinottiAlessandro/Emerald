#include "core/UpdateChannel.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace {
int failures = 0;

void check(bool condition, const QString &message) {
    if (condition)
        return;
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++failures;
}

QJsonObject release(const QString &tag, bool prerelease = false,
                    bool draft = false) {
    return {{QStringLiteral("tag_name"), tag},
            {QStringLiteral("prerelease"), prerelease},
            {QStringLiteral("draft"), draft}};
}

QString selectedTag(const QJsonArray &releases,
                    UpdateChannel::Channel channel) {
    return UpdateChannel::selectRelease(QJsonDocument(releases), channel)
        .value(QStringLiteral("tag_name"))
        .toString();
}

void testChannelKeys() {
    using UpdateChannel::Channel;
    check(UpdateChannel::key(Channel::Stable) == QStringLiteral("stable") &&
              UpdateChannel::key(Channel::Development) ==
                  QStringLiteral("development"),
          QStringLiteral("release channels have stable settings keys"));
    check(UpdateChannel::fromKey(QStringLiteral("development")) ==
                  Channel::Development &&
              UpdateChannel::fromKey(QStringLiteral("unknown")) ==
                  Channel::Stable &&
              UpdateChannel::fromKey(QString()) == Channel::Stable,
          QStringLiteral("unknown and missing settings safely default to Stable"));
}

void testSemanticVersions() {
    check(UpdateChannel::normalizedVersion(QStringLiteral(" v2.3.0-dev.2 ")) ==
              QStringLiteral("2.3.0-dev.2"),
          QStringLiteral("release versions normalize their tag prefix"));
    check(UpdateChannel::normalizedVersion(QStringLiteral("2.3-dev"))
                  .isEmpty() &&
              UpdateChannel::normalizedVersion(QStringLiteral("2.3.0-01"))
                  .isEmpty(),
          QStringLiteral("invalid SemVer values are rejected"));
    check(UpdateChannel::compareVersions(QStringLiteral("2.3.0-dev.10"),
                                         QStringLiteral("2.3.0-dev.2")) > 0,
          QStringLiteral("numeric prerelease identifiers use numeric order"));
    check(UpdateChannel::compareVersions(QStringLiteral("2.3.0-dev.2"),
                                         QStringLiteral("2.3.0")) < 0,
          QStringLiteral("a stable release follows its development builds"));
    check(UpdateChannel::compareVersions(QStringLiteral("2.4.0-dev.1"),
                                         QStringLiteral("2.3.9")) > 0,
          QStringLiteral("a newer development series follows an older stable"));
    check(UpdateChannel::compareVersions(QStringLiteral("2.3.0+build.7"),
                                         QStringLiteral("2.3.0+build.2")) == 0,
          QStringLiteral("build metadata does not change precedence"));
}

void testReleaseSelection() {
    using UpdateChannel::Channel;
    const QJsonArray releases{
        release(QStringLiteral("v2.3.0-dev.10"), true),
        release(QStringLiteral("spell-dictionaries-v1.2.0")),
        release(QStringLiteral("v2.2.2+build-linux")),
        release(QStringLiteral("v2.2.1")),
        release(QStringLiteral("v99.0.0"), false, true),
        release(QStringLiteral("v2.3.0-dev.2"), true),
        release(QStringLiteral("2.9.0")),
    };
    check(selectedTag(releases, Channel::Stable) ==
              QStringLiteral("v2.2.2+build-linux"),
          QStringLiteral("Stable ignores development, draft, and unrelated tags"));
    check(selectedTag(releases, Channel::Development) ==
              QStringLiteral("v2.3.0-dev.10"),
          QStringLiteral("Development selects the highest application release"));

    QJsonArray withStable = releases;
    withStable.prepend(release(QStringLiteral("v2.3.0")));
    check(selectedTag(withStable, Channel::Development) ==
              QStringLiteral("v2.3.0"),
          QStringLiteral("Development advances to the completed stable release"));

    const QJsonObject single = release(QStringLiteral("v2.4.0"));
    check(UpdateChannel::selectRelease(QJsonDocument(single), Channel::Stable) ==
              single,
          QStringLiteral("the Stable latest-release object is accepted"));

    const QJsonArray mislabeled{
        release(QStringLiteral("v2.5.0-dev.1"), false),
        release(QStringLiteral("v2.4.1")),
    };
    check(selectedTag(mislabeled, Channel::Stable) == QStringLiteral("v2.4.1"),
          QStringLiteral("a prerelease tag cannot enter Stable by metadata error"));
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    testChannelKeys();
    testSemanticVersions();
    testReleaseSelection();
    if (failures == 0)
        QTextStream(stdout) << "All update channel tests passed.\n";
    return failures == 0 ? 0 : 1;
}
