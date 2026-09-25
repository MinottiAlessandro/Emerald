#include "core/SearchIndex.h"
#include "core/Perf.h"

#include <QCoreApplication>
#include <QTextStream>

Q_LOGGING_CATEGORY(emeraldPerf, "emerald.perf.tests")

namespace {
int failures = 0;
void check(bool condition, const QString &message) {
    if (!condition) {
        ++failures;
        QTextStream(stderr) << "FAIL: " << message << '\n';
    }
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    SearchIndex index;
    const QString source =
        QStringLiteral("<!-- mascot: 123 -->\n<!-- needle hidden -->\n"
                       "café 😀 needle and needle\nneedles\nneedle");
    index.updateNote(QStringLiteral("/Alpha.md"), QStringLiteral("Alpha"),
                     source);
    index.updateNote(QStringLiteral("/Beta.md"), QStringLiteral("Beta"),
                     QStringLiteral("needle"));
    const auto results = index.search(QStringLiteral("needle"), 0);
    check(results.size() == 5,
          QStringLiteral("every occurrence, including repeated matches on one "
                         "line, is returned"));
    int alphaCount = 0;
    for (const auto &result : results) {
        if (result.path == QStringLiteral("/Alpha.md")) {
            ++alphaCount;
            check(source.mid(result.position, result.length) ==
                      QStringLiteral("needle"),
                  QStringLiteral(
                      "UTF-16 offsets survive comments, emoji and accents"));
            check(result.line >= 3 &&
                      !result.snippet.contains(QStringLiteral("hidden")),
                  QStringLiteral(
                      "hidden comments never leak into results or snippets"));
        }
    }
    check(alphaCount == 4 && results.at(3).path == QStringLiteral("/Beta.md") &&
              results.last().path == QStringLiteral("/Alpha.md"),
          QStringLiteral("exact words rank ahead of prefixes across notes, "
                         "without grouping"));
    check(index.search(QStringLiteral("hidden"), 0).isEmpty() &&
              index.search(QStringLiteral("123"), 0).isEmpty(),
          QStringLiteral("comments and mascot metadata are not indexed"));
    check(index.search(QStringLiteral("needle"), 2).size() == 2,
          QStringLiteral("bounded callers can still request the best results"));

    index.clear();
    const QString phrases =
        QStringLiteral("red distant green apple\nred apple\nred apple");
    index.updateNote(QStringLiteral("/Phrases.md"), QStringLiteral("Phrases"),
                     phrases);
    const auto ranked = index.search(QStringLiteral("red apple"), 0);
    check(ranked.size() == 3 && ranked.first().line == 2 &&
              ranked.at(1).line == 3 && ranked.last().line == 1 &&
              ranked.first().length == 9,
          QStringLiteral("exact phrases rank above separated query terms"));

    index.clear();
    index.updateNote(QStringLiteral("/Large.md"), QStringLiteral("Large"),
                     QStringLiteral("match\n").repeated(100));
    check(
        index.search(QStringLiteral("match"), 0).size() == 100,
        QStringLiteral("unlimited searches do not silently stop at 30 or 50"));
    index.updateNote(QStringLiteral("/Large.md"), QStringLiteral("Large"),
                     QStringLiteral("changed\r\nmatch"));
    const auto updated = index.search(QStringLiteral("match"), 0);
    check(
        updated.size() == 1 && updated.first().position == 8 &&
            updated.first().line == 2,
        QStringLiteral(
            "updates replace cached source and normalize editor line endings"));
    index.renamePath(QStringLiteral("/Large.md"), QStringLiteral("/Moved.md"));
    check(index.search(QStringLiteral("match")).first().path ==
              QStringLiteral("/Moved.md"),
          QStringLiteral(
              "renames retain occurrence locations under the new path"));
    index.removeNote(QStringLiteral("/Moved.md"));
    check(index.search(QStringLiteral("match")).isEmpty(),
          QStringLiteral("deleted notes leave no search results"));
    index.updateNote(QStringLiteral("/Title.md"), QStringLiteral("Title match"),
                     QStringLiteral("unrelated body"));
    const auto title = index.search(QStringLiteral("title match"), 0);
    check(title.size() == 1 && title.first().position == -1,
          QStringLiteral("title-only matches remain available"));
    index.clear();
    index.updateNote(QStringLiteral("/Lunar.md"), QStringLiteral("Lunar"),
                     QStringLiteral("maps\nmore maps"));
    check(index.search(QStringLiteral("lunar map"), 0).size() == 2,
          QStringLiteral("queries can combine title and body terms without "
                         "losing occurrences"));
    if (!failures)
        QTextStream(stdout) << "All search index tests passed.\n";
    return failures ? 1 : 0;
}
