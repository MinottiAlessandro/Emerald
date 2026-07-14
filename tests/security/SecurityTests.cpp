#include "core/LegacyMascotMigration.h"
#include "core/MascotSeed.h"
#include "core/Perf.h"
#include "core/Vault.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

Q_LOGGING_CATEGORY(emeraldPerf, "emerald.perf")

namespace {
int failures = 0;

void check(bool condition, const QString &message) {
    if (condition)
        return;
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++failures;
}

bool writeFile(const QString &path, const QByteArray &content) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(content) == content.size();
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject seedEntry(quint64 seed) {
    return {{QStringLiteral("seed"), QString::number(seed)}};
}

bool writeStore(const QString &path, const QJsonObject &mascots) {
    const QJsonObject root{{QStringLiteral("mascots"), mascots}};
    return writeFile(path, QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void testContainedMigration() {
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("temporary directory is available"));
    if (!temp.isValid())
        return;

    const QString vaultRoot = temp.filePath(QStringLiteral("vault"));
    const QString validNote =
        QDir(vaultRoot).filePath(QStringLiteral("sub/note.md"));
    const QString textFile =
        QDir(vaultRoot).filePath(QStringLiteral("not-a-note.txt"));
    const QString outsideNote = temp.filePath(QStringLiteral("outside.md"));
    const QString outsideNested =
        temp.filePath(QStringLiteral("outside-dir/nested.md"));
    const QString storePath =
        QDir(vaultRoot).filePath(QStringLiteral(".emerald/mascots.json"));
    check(writeFile(validNote, "inside\n"), QStringLiteral("write valid note"));
    check(writeFile(textFile, "text\n"), QStringLiteral("write non-Markdown file"));
    check(writeFile(outsideNote, "outside\n"), QStringLiteral("write outside note"));
    check(writeFile(outsideNested, "outside nested\n"),
          QStringLiteral("write nested outside note"));

    QJsonObject mascots;
    mascots.insert(QStringLiteral("sub/note.md"), seedEntry(42));
    mascots.insert(QStringLiteral("not-a-note.txt"), seedEntry(43));
    mascots.insert(QStringLiteral("../outside.md"), seedEntry(44));
    mascots.insert(outsideNote, seedEntry(45));
#if defined(Q_OS_UNIX)
    const QString escapedLink =
        QDir(vaultRoot).filePath(QStringLiteral("escaped-link.md"));
    check(QFile::link(outsideNote, escapedLink),
          QStringLiteral("create escaping note symlink"));
    mascots.insert(QStringLiteral("escaped-link.md"), seedEntry(46));
    check(QFile::link(QFileInfo(outsideNested).absolutePath(),
                      QDir(vaultRoot).filePath(QStringLiteral("escaped-dir"))),
          QStringLiteral("create escaping directory symlink"));
    mascots.insert(QStringLiteral("escaped-dir/nested.md"), seedEntry(47));
#endif
    check(writeStore(storePath, mascots), QStringLiteral("write legacy store"));

    Vault vault(vaultRoot);
    check(vault.resolveExistingFileWithinRoot(QStringLiteral("sub/note.md")) ==
              QFileInfo(validNote).canonicalFilePath(),
          QStringLiteral("resolve an in-vault note"));
    check(vault.resolveExistingFileWithinRoot(QStringLiteral("../outside.md")).isEmpty(),
          QStringLiteral("reject parent traversal"));
    check(vault.resolveExistingFileWithinRoot(outsideNote).isEmpty(),
          QStringLiteral("reject absolute path"));
    check(vault.resolveExistingFileWithinRoot(QStringLiteral("sub")).isEmpty(),
          QStringLiteral("reject directory"));
#if defined(Q_OS_UNIX)
    check(vault.resolveExistingFileWithinRoot(
              QStringLiteral("escaped-link.md")).isEmpty(),
          QStringLiteral("reject symlink escaping the vault"));
    check(vault.resolveExistingFileWithinRoot(
              QStringLiteral("escaped-dir/nested.md")).isEmpty(),
          QStringLiteral("reject path through an escaping directory symlink"));
#endif

    const LegacyMascotMigration::Result result =
        LegacyMascotMigration::run(vault);
    const QByteArray expected =
        (MascotSeed::line(42) + QStringLiteral("\ninside\n")).toUtf8();
    check(result.storeFound, QStringLiteral("legacy store was found"));
    check(result.storeRemoved, QStringLiteral("valid legacy store was removed"));
    check(result.migratedEntries == 1,
          QStringLiteral("only the contained Markdown note was migrated"));
    check(readFile(validNote) == expected,
          QStringLiteral("valid note received its mascot header"));
    check(readFile(textFile) == QByteArray("text\n"),
          QStringLiteral("non-Markdown file was unchanged"));
    check(readFile(outsideNote) == QByteArray("outside\n"),
          QStringLiteral("outside file was unchanged"));
    check(readFile(outsideNested) == QByteArray("outside nested\n"),
          QStringLiteral("nested outside file was unchanged"));
    check(!QFileInfo::exists(storePath),
          QStringLiteral("legacy store no longer exists"));
}

void testEscapingStoreIsRejected() {
#if defined(Q_OS_UNIX)
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("symlink-store temp directory is available"));
    if (!temp.isValid())
        return;

    const QString vaultRoot = temp.filePath(QStringLiteral("vault"));
    const QString externalStoreDir = temp.filePath(QStringLiteral("external-store"));
    const QString victim = QDir(vaultRoot).filePath(QStringLiteral("victim.md"));
    const QString externalStore =
        QDir(externalStoreDir).filePath(QStringLiteral("mascots.json"));
    check(writeFile(victim, "victim\n"), QStringLiteral("write store-escape victim"));
    check(writeStore(externalStore,
                     {{QStringLiteral("victim.md"), seedEntry(77)}}),
          QStringLiteral("write external legacy store"));
    check(QFile::link(externalStoreDir,
                      QDir(vaultRoot).filePath(QStringLiteral(".emerald"))),
          QStringLiteral("create escaping metadata-directory symlink"));

    Vault vault(vaultRoot);
    const LegacyMascotMigration::Result result =
        LegacyMascotMigration::run(vault);
    check(!result.storeFound,
          QStringLiteral("escaping metadata store was not accepted"));
    check(readFile(victim) == QByteArray("victim\n"),
          QStringLiteral("escaping store could not modify a note"));
    check(QFileInfo::exists(externalStore),
          QStringLiteral("escaping store was not deleted"));
#endif
}

void testMalformedStoreIsPreserved() {
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("malformed-store temp directory is available"));
    if (!temp.isValid())
        return;

    const QString vaultRoot = temp.filePath(QStringLiteral("vault"));
    const QString storePath =
        QDir(vaultRoot).filePath(QStringLiteral(".emerald/mascots.json"));
    check(writeFile(storePath, "not-json"),
          QStringLiteral("write malformed legacy store"));
    Vault vault(vaultRoot);
    const LegacyMascotMigration::Result result =
        LegacyMascotMigration::run(vault);
    check(result.storeFound, QStringLiteral("malformed store was found"));
    check(!result.storeRemoved,
          QStringLiteral("malformed store was preserved for recovery"));
    check(QFileInfo::exists(storePath),
          QStringLiteral("malformed store remains on disk"));
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    testContainedMigration();
    testEscapingStoreIsRejected();
    testMalformedStoreIsPreserved();
    if (failures == 0)
        QTextStream(stdout) << "All security regression tests passed.\n";
    return failures == 0 ? 0 : 1;
}
