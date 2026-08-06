#include "core/VaultSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

namespace {
int failures = 0;

void check(bool condition, const QString &message) {
    if (condition)
        return;
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++failures;
}

void testVaultIsolation() {
    QSettings().clear();
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("vault settings temp directory exists"));
    if (!temp.isValid())
        return;

    const QString vaultA = temp.filePath(QStringLiteral("vault-a"));
    const QString vaultB = temp.filePath(QStringLiteral("vault-b"));
    check(QDir().mkpath(vaultA), QStringLiteral("create vault A"));
    check(QDir().mkpath(vaultB), QStringLiteral("create vault B"));

    VaultSettings::setValue(vaultA, QStringLiteral("homeNote"),
                            QStringLiteral("Home A.md"));
    VaultSettings::setValue(vaultA, QStringLiteral("newNoteFolder"),
                            QStringLiteral("Inbox A"));
    VaultSettings::setValue(vaultB, QStringLiteral("homeNote"),
                            QStringLiteral("Home B.md"));
    VaultSettings::setValue(vaultB, QStringLiteral("newNoteFolder"),
                            QStringLiteral("Inbox B"));
    VaultSettings::setValue(vaultA, QStringLiteral("readMode"),
                            QStringLiteral("true"));
    VaultSettings::setValue(vaultB, QStringLiteral("readMode"),
                            QStringLiteral("false"));

    check(VaultSettings::value(vaultA, QStringLiteral("homeNote")) ==
              QStringLiteral("Home A.md"),
          QStringLiteral("vault A keeps its Home note"));
    check(VaultSettings::value(vaultB, QStringLiteral("homeNote")) ==
              QStringLiteral("Home B.md"),
          QStringLiteral("vault B keeps its Home note"));
    check(VaultSettings::value(QDir(vaultA).filePath(QStringLiteral(".")),
                               QStringLiteral("newNoteFolder")) ==
              QStringLiteral("Inbox A"),
          QStringLiteral("equivalent vault paths share one settings group"));
    check(VaultSettings::value(vaultA, QStringLiteral("readMode")) ==
                  QStringLiteral("true") &&
              VaultSettings::value(vaultB, QStringLiteral("readMode")) ==
                  QStringLiteral("false"),
          QStringLiteral("Read Mode is isolated per vault"));

    VaultSettings::remove(vaultA, QStringLiteral("homeNote"));
    check(VaultSettings::value(vaultA, QStringLiteral("homeNote")).isEmpty(),
          QStringLiteral("removing vault A setting affects vault A"));
    check(VaultSettings::value(vaultB, QStringLiteral("homeNote")) ==
              QStringLiteral("Home B.md"),
          QStringLiteral("removing vault A setting leaves vault B untouched"));

    check(QDir(vaultA).entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
              .isEmpty(),
          QStringLiteral("vault A contains no Emerald metadata"));
    check(QDir(vaultB).entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
              .isEmpty(),
          QStringLiteral("vault B contains no Emerald metadata"));
}

void testLegacyMigration() {
    QSettings settings;
    settings.clear();
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("migration temp directory exists"));
    if (!temp.isValid())
        return;

    const QString vault = temp.filePath(QStringLiteral("legacy-vault"));
    const QString otherVault = temp.filePath(QStringLiteral("other-vault"));
    check(QDir().mkpath(vault), QStringLiteral("create legacy vault"));
    check(QDir().mkpath(otherVault), QStringLiteral("create other vault"));

    settings.setValue(QStringLiteral("lastVault"), vault);
    settings.setValue(QStringLiteral("homeNote"), QStringLiteral("Home.md"));
    settings.setValue(QStringLiteral("newNoteFolder"), QStringLiteral("Inbox"));
    settings.setValue(QStringLiteral("templatesFolder"),
                      QStringLiteral("Legacy Templates"));
    // A value already written by a newer build wins over its legacy global.
    VaultSettings::setValue(vault, QStringLiteral("templatesFolder"),
                            QStringLiteral("Current Templates"));

    VaultSettings::migrateLegacyForLastVault();

    check(VaultSettings::value(vault, QStringLiteral("homeNote")) ==
              QStringLiteral("Home.md"),
          QStringLiteral("legacy Home note migrates to the last vault"));
    check(VaultSettings::value(vault, QStringLiteral("newNoteFolder")) ==
              QStringLiteral("Inbox"),
          QStringLiteral("legacy default folder migrates to the last vault"));
    check(VaultSettings::value(vault, QStringLiteral("templatesFolder")) ==
              QStringLiteral("Current Templates"),
          QStringLiteral("migration preserves an existing per-vault value"));
    check(VaultSettings::value(otherVault, QStringLiteral("homeNote")).isEmpty(),
          QStringLiteral("legacy settings do not leak into another vault"));

    QSettings migrated;
    check(!migrated.contains(QStringLiteral("homeNote")) &&
              !migrated.contains(QStringLiteral("newNoteFolder")) &&
              !migrated.contains(QStringLiteral("templatesFolder")),
          QStringLiteral("legacy global keys are removed after migration"));
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("EmeraldTests"));
    QCoreApplication::setApplicationName(QStringLiteral("VaultSettingsTests"));

    QTemporaryDir settingsDir;
    if (!settingsDir.isValid())
        return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir.path());

    testVaultIsolation();
    testLegacyMigration();
    QSettings().clear();

    if (failures == 0)
        QTextStream(stdout) << "All vault settings tests passed.\n";
    return failures == 0 ? 0 : 1;
}
