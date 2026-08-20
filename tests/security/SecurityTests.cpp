#include "core/ContentSecurity.h"
#include "core/LegacyMascotMigration.h"
#include "core/MascotSeed.h"
#include "core/Perf.h"
#include "core/Vault.h"
#include "core/WikiLink.h"

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

void testWikiLinkCreationValidation() {
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("wiki-link temp directory is available"));
    if (!temp.isValid())
        return;

    const QString vaultRoot = temp.filePath(QStringLiteral("vault"));
    check(QDir().mkpath(vaultRoot), QStringLiteral("create wiki-link vault"));
    Vault vault(vaultRoot);
    vault.scan();

    const QString validTarget =
        WikiLink::cleanTarget(QStringLiteral("A safe note#Heading|Alias"));
    check(WikiLink::cleanDestination(
              QStringLiteral("A safe note#Heading|Alias")) ==
                  QStringLiteral("A safe note#Heading") &&
              WikiLink::heading(
                  QStringLiteral("A safe note#Heading|Alias")) ==
                  QStringLiteral("Heading"),
          QStringLiteral("wiki navigation retains a heading while removing its "
                         "display alias"));
    const QString headingSource = QStringLiteral(
        "```md\n# Title2\n```\n"
        "<!-- # Title2 -->\n"
        "# Other\n"
        "## Title2\n");
    check(WikiLink::headingPosition(headingSource,
                                    QStringLiteral("title2")) ==
              headingSource.lastIndexOf(QStringLiteral("Title2")),
          QStringLiteral("heading navigation ignores fenced/commented decoys "
                         "and matches case-insensitively"));
    const Note valid = vault.createNote(validTarget);
    check(!valid.path.isEmpty(), QStringLiteral("valid wiki-link creates a note"));
    check(QFileInfo::exists(valid.path),
          QStringLiteral("valid wiki-link note exists on disk"));

    const QStringList invalidTitles{
        QStringLiteral("../outside"), QStringLiteral("sub/note"),
        QStringLiteral("sub\\note"), QStringLiteral("/absolute"),
        QStringLiteral("C:\\outside"), QStringLiteral("NUL")};
    for (const QString &title : invalidTitles) {
        check(vault.createNote(title).path.isEmpty(),
              QStringLiteral("reject unsafe wiki-link title: %1").arg(title));
        check(vault.createNoteIn(vaultRoot, title).path.isEmpty(),
              QStringLiteral("reject unsafe folder-note title: %1").arg(title));
    }
    check(!QFileInfo::exists(temp.filePath(QStringLiteral("outside.md"))),
          QStringLiteral("wiki-link traversal created no outside file"));

    const QString blockedPath =
        QDir(vaultRoot).filePath(QStringLiteral("Blocked.md"));
    check(QDir().mkdir(blockedPath), QStringLiteral("create conflicting directory"));
    check(vault.createNoteIn(vaultRoot, QStringLiteral("Blocked")).path.isEmpty(),
          QStringLiteral("a directory cannot be treated as an existing note"));
}

void testMarkdownContentBoundaries() {
    const QUrl https =
        ContentSecurity::externalUrl(QStringLiteral("https://example.com/note"));
    check(https.isValid() && https.scheme() == QStringLiteral("https"),
          QStringLiteral("allow HTTPS links"));
    check(ContentSecurity::externalUrl(QStringLiteral("http://example.com")).isValid(),
          QStringLiteral("allow HTTP links"));
    check(ContentSecurity::externalUrl(
              QStringLiteral("mailto:writer@example.com")).isValid(),
          QStringLiteral("allow email links"));
    check(ContentSecurity::externalUrl(QStringLiteral("example.com")).isValid(),
          QStringLiteral("allow a web address without an explicit scheme"));

    const QStringList blockedUrls{
        QStringLiteral("file:///etc/passwd"),
        QStringLiteral("javascript:alert(1)"),
        QStringLiteral("emerald:open-settings"), QStringLiteral("../local.md")};
    for (const QString &url : blockedUrls)
        check(!ContentSecurity::externalUrl(url).isValid(),
              QStringLiteral("reject unsafe external URL: %1").arg(url));

    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("image-boundary temp directory is available"));
    if (!temp.isValid())
        return;

    const QString vaultRoot = temp.filePath(QStringLiteral("vault"));
    const QString noteDir = QDir(vaultRoot).filePath(QStringLiteral("notes"));
    const QString insideImage =
        QDir(noteDir).filePath(QStringLiteral("inside.png"));
    const QString rootImage =
        QDir(vaultRoot).filePath(QStringLiteral("root.png"));
    const QString outsideImage = temp.filePath(QStringLiteral("outside.png"));
    const QString outsideNested =
        temp.filePath(QStringLiteral("outside-dir/nested.png"));
    check(writeFile(insideImage, "inside"), QStringLiteral("write inside image"));
    check(writeFile(rootImage, "root"), QStringLiteral("write vault-root image"));
    check(writeFile(outsideImage, "outside"), QStringLiteral("write outside image"));
    check(writeFile(outsideNested, "outside nested"),
          QStringLiteral("write nested outside image"));

    check(ContentSecurity::resolveLocalImage(
              QStringLiteral("inside.png"), noteDir, vaultRoot) ==
              QFileInfo(insideImage).canonicalFilePath(),
          QStringLiteral("resolve a relative image inside the vault"));
    check(ContentSecurity::resolveLocalImage(
              QStringLiteral("../root.png"), noteDir, vaultRoot) ==
              QFileInfo(rootImage).canonicalFilePath(),
          QStringLiteral("allow relative image traversal that remains in the vault"));
    check(ContentSecurity::resolveLocalImage(
              insideImage, noteDir, vaultRoot) ==
              QFileInfo(insideImage).canonicalFilePath(),
          QStringLiteral("allow an absolute image that remains in the vault"));
    check(ContentSecurity::resolveLocalImage(
              QUrl::fromLocalFile(insideImage).toString(), noteDir, vaultRoot) ==
              QFileInfo(insideImage).canonicalFilePath(),
          QStringLiteral("allow an in-vault file URL"));

    check(ContentSecurity::resolveLocalImage(
              QStringLiteral("../../outside.png"), noteDir, vaultRoot).isEmpty(),
          QStringLiteral("reject image traversal outside the vault"));
    check(ContentSecurity::resolveLocalImage(
              outsideImage, noteDir, vaultRoot).isEmpty(),
          QStringLiteral("reject an absolute image outside the vault"));
    check(ContentSecurity::resolveLocalImage(
              QUrl::fromLocalFile(outsideImage).toString(), noteDir,
              vaultRoot).isEmpty(),
          QStringLiteral("reject an outside file URL"));
    check(ContentSecurity::resolveLocalImage(
              QStringLiteral("https://example.com/image.png"), noteDir,
              vaultRoot).isEmpty(),
          QStringLiteral("reject remote image previews"));
#if defined(Q_OS_UNIX)
    const QString escapedLink =
        QDir(noteDir).filePath(QStringLiteral("escaped.png"));
    check(QFile::link(outsideImage, escapedLink),
          QStringLiteral("create escaping image symlink"));
    check(ContentSecurity::resolveLocalImage(
              QStringLiteral("escaped.png"), noteDir, vaultRoot).isEmpty(),
          QStringLiteral("reject an image symlink escaping the vault"));
    const QString escapedDir =
        QDir(noteDir).filePath(QStringLiteral("escaped-dir"));
    check(QFile::link(QFileInfo(outsideNested).absolutePath(), escapedDir),
          QStringLiteral("create escaping image-directory symlink"));
    check(ContentSecurity::resolveLocalImage(
              QStringLiteral("escaped-dir/nested.png"), noteDir,
              vaultRoot).isEmpty(),
          QStringLiteral("reject an image path through an escaping symlink"));
#endif
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    testContainedMigration();
    testEscapingStoreIsRejected();
    testMalformedStoreIsPreserved();
    testWikiLinkCreationValidation();
    testMarkdownContentBoundaries();
    if (failures == 0)
        QTextStream(stdout) << "All security regression tests passed.\n";
    return failures == 0 ? 0 : 1;
}
