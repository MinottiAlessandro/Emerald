#include "core/MascotSeed.h"
#include "core/Perf.h"
#include "core/Vault.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QTemporaryDir>
#include <QTextStream>

Q_LOGGING_CATEGORY(emeraldPerf, "emerald.perf.tests")

namespace {
int failures = 0;

void check(bool condition, const QString &message) {
    if (condition)
        return;
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++failures;
}

bool writeFile(const QString &path, const QString &content) {
    QFile file(path);
    const QByteArray bytes = content.toUtf8();
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size();
}

void testBrokenLinkScan() {
    QTemporaryDir temp;
    check(temp.isValid(), QStringLiteral("broken-link temp vault exists"));
    if (!temp.isValid())
        return;

    const QString source = QStringLiteral(
        "[[Missing#Section|Shown]]\n"
        "[[Empty|Blank alias]]\n"
        "[[Whitespace]]\n"
        "[[Mascot only]]\n"
        "[[Comment only]]\n"
        "[[Filled]] and [[fIlLeD#Heading]]\n"
        "<!-- [[Commented missing]] -->\n"
        "<!--\n[[Block commented missing]]\n-->\n"
        "[[#Local heading]]\n"
        "`[[Inline code]]`\n"
        "```md\n"
        "[[Fenced code]]\n"
        "```\n");
    check(writeFile(temp.filePath(QStringLiteral("Source.md")), source),
          QStringLiteral("write source note"));
    check(writeFile(temp.filePath(QStringLiteral("Empty.md")), QString()),
          QStringLiteral("write empty target"));
    check(writeFile(temp.filePath(QStringLiteral("Whitespace.md")),
                    QStringLiteral("  \n\t\n")),
          QStringLiteral("write whitespace target"));
    check(writeFile(temp.filePath(QStringLiteral("Mascot only.md")),
                    MascotSeed::line(42) + QStringLiteral("\n  \n")),
          QStringLiteral("write mascot-only target"));
    check(writeFile(temp.filePath(QStringLiteral("Comment only.md")),
                    QStringLiteral("<!-- private note text -->\n")),
          QStringLiteral("write comment-only target"));
    check(writeFile(temp.filePath(QStringLiteral("Filled.md")),
                    MascotSeed::line(7) + QStringLiteral("\nActual body\n")),
          QStringLiteral("write populated target"));

    Vault vault(temp.path());
    vault.scan();
    const QVector<Vault::BrokenLink> issues = vault.brokenLinks();
    check(issues.size() == 5,
          QStringLiteral("only missing and empty semantic links are reported"));

    QHash<QString, Vault::BrokenLink> byTarget;
    for (const Vault::BrokenLink &issue : issues)
        byTarget.insert(issue.target, issue);

    check(byTarget.contains(QStringLiteral("Missing")),
          QStringLiteral("missing target is reported after cleaning heading/alias"));
    check(byTarget.value(QStringLiteral("Missing")).state ==
              Vault::BrokenLink::State::MissingNote,
          QStringLiteral("missing target has missing-note state"));
    for (const QString &target : {QStringLiteral("Empty"),
                                  QStringLiteral("Whitespace"),
                                  QStringLiteral("Mascot only"),
                                  QStringLiteral("Comment only")}) {
        check(byTarget.contains(target), target + QStringLiteral(" is reported"));
        check(byTarget.value(target).state ==
                  Vault::BrokenLink::State::EmptyNote,
              target + QStringLiteral(" has empty-note state"));
    }

    check(!byTarget.contains(QStringLiteral("Filled")),
          QStringLiteral("populated target is not reported"));
    check(!byTarget.contains(QStringLiteral("Inline code")) &&
              !byTarget.contains(QStringLiteral("Fenced code")) &&
              !byTarget.contains(QStringLiteral("Commented missing")) &&
              !byTarget.contains(QStringLiteral("Block commented missing")),
          QStringLiteral("code examples and comments are not treated as links"));

    const QString renameFixture = QStringLiteral(
        "[[Old title]] <!-- [[Old title|private]] --> [[Other]]");
    check(Vault::replaceLinkTargets(renameFixture, QStringLiteral("Old title"),
                                    QStringLiteral("New title")) ==
              QStringLiteral(
                  "[[New title]] <!-- [[Old title|private]] --> [[Other]]"),
          QStringLiteral("renaming a note should not rewrite links inside "
                         "comments"));

    for (const Vault::BrokenLink &issue : issues) {
        const QString raw = source.mid(issue.sourcePosition, issue.sourceLength);
        check(raw.startsWith(QStringLiteral("[[")) &&
                  raw.endsWith(QStringLiteral("]]")),
              issue.target + QStringLiteral(" preserves its source selection"));
        check(issue.sourcePath == temp.filePath(QStringLiteral("Source.md")),
              issue.target + QStringLiteral(" retains its source path"));
        check(issue.line >= 1 && issue.line <= 5,
              issue.target + QStringLiteral(" retains its source line"));
    }
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    testBrokenLinkScan();

    if (failures == 0)
        QTextStream(stdout) << "All broken-link tests passed.\n";
    return failures == 0 ? 0 : 1;
}
