#include "core/LinkGraphIndex.h"
#include "core/MarkdownComment.h"
#include "core/MarkdownWikiLinkScanner.h"
#include "core/WikiLink.h"

#include <QCoreApplication>
#include <QDir>
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

int nodeForTitle(const LinkGraphIndex::Snapshot &graph, const QString &title,
                 bool unresolved = false) {
  for (int i = 0; i < graph.nodes.size(); ++i) {
    if (graph.nodes.at(i).title == title &&
        graph.nodes.at(i).unresolved == unresolved)
      return i;
  }
  return -1;
}

const LinkGraphIndex::Edge *edge(const LinkGraphIndex::Snapshot &graph,
                                 int from, int to) {
  for (const LinkGraphIndex::Edge &candidate : graph.edges)
    if (candidate.from == from && candidate.to == to)
      return &candidate;
  return nullptr;
}

void testSemanticScanner() {
  const QString content =
      QStringLiteral("[[Target|label]] and [[Other#Heading]] and [[#Local]]\n"
                     "`[[inline]]` ``[[also inline]]`` [[Visible]]\n"
                     "<!-- [[inline comment]] -->\n"
                     "before <!-- [[hidden comment link]] --> after\n"
                     "<!--\n[[block comment link]]\n-->\n"
                     "```md\n[[fenced]]\n```\n"
                     "~~~\n[[tilde fenced]]\n~~~~\n");
  const QVector<MarkdownWikiLinkScanner::Link> links =
      MarkdownWikiLinkScanner::scan(content);
  check(links.size() == 4,
        QStringLiteral("scanner keeps semantic links and excludes code"));
  check(links.value(0).target == QStringLiteral("Target") &&
            links.value(1).target == QStringLiteral("Other") &&
            links.value(2).target.isEmpty() &&
            links.value(3).target == QStringLiteral("Visible"),
        QStringLiteral("scanner cleans aliases/headings and keeps local link"));
  for (const auto &link : links) {
    const QString raw = content.mid(link.position, link.length);
    check(raw.startsWith(QStringLiteral("[[")) &&
              raw.endsWith(QStringLiteral("]]")),
          QStringLiteral("scanner preserves exact source span"));
  }

  const QString commentFixture = QStringLiteral(
      "before <!-- hidden words --> after\n"
      "<!--\n# hidden heading\n-->\n"
      "`<!-- inline code -->`\n"
      "```md\n<!-- fenced code -->\n```\n"
      "``` still code\n<!-- still fenced code -->\n```\n"
      "$$ <!-- formula text --> $$\n"
      "$$\n<!-- block formula text -->\n$$\n"
      "stray $$ <!-- hidden after stray display marker -->\n"
      "$5 and <!-- hidden between currency --> $10\n"
      "<!-- unclosed comment\n[[hidden to end]]");
  const QString stripped = MarkdownComment::strip(commentFixture);
  check(stripped.contains(QStringLiteral("before  after")) &&
            !stripped.contains(QStringLiteral("hidden words")) &&
            !stripped.contains(QStringLiteral("hidden heading")) &&
            stripped.contains(QStringLiteral("`<!-- inline code -->`")) &&
            stripped.contains(QStringLiteral("<!-- fenced code -->")) &&
            stripped.contains(QStringLiteral("<!-- still fenced code -->")) &&
            stripped.contains(QStringLiteral("<!-- formula text -->")) &&
            stripped.contains(QStringLiteral("<!-- block formula text -->")) &&
            !stripped.contains(QStringLiteral("hidden after stray")) &&
            !stripped.contains(QStringLiteral("hidden between currency")) &&
            !stripped.contains(QStringLiteral("hidden to end")),
        QStringLiteral("comment stripping hides author comments while keeping "
                       "code and math literal"));
}

void testHeadingOutline() {
  const QString markdown = QStringLiteral(
      "# Introduction\n"
      "Text\n"
      "   ### Details ###\n"
      "<!-- ## Hidden comment -->\n"
      "```md\n# Hidden code\n```\n"
      "## Details\n");
  const QList<WikiLink::Heading> outline =
      WikiLink::headingOutline(markdown);
  check(outline.size() == 3,
        QStringLiteral("note outline excludes comments and fenced code"));
  check(outline.value(0).text == QStringLiteral("Introduction") &&
            outline.value(0).level == 1 &&
            outline.value(0).position ==
                markdown.indexOf(QStringLiteral("Introduction")),
        QStringLiteral("outline records the first heading level and position"));
  check(outline.value(1).text == QStringLiteral("Details") &&
            outline.value(1).level == 3 &&
            outline.value(2).text == QStringLiteral("Details") &&
            outline.value(2).level == 2 &&
            outline.value(1).position != outline.value(2).position,
        QStringLiteral("outline retains exact duplicate heading occurrences"));
  check(WikiLink::headings(markdown) ==
            QStringList({QStringLiteral("Introduction"),
                         QStringLiteral("Details")}),
        QStringLiteral("link completion continues to deduplicate headings"));
}

void testGraphIndex() {
  QTemporaryDir temp;
  check(temp.isValid(), QStringLiteral("graph-index temp vault exists"));
  if (!temp.isValid())
    return;
  QDir().mkpath(temp.filePath(QStringLiteral("Projects")));
  const QVector<Note> notes{
      {temp.filePath(QStringLiteral("Alpha.md")), QStringLiteral("Alpha")},
      {temp.filePath(QStringLiteral("Beta.md")), QStringLiteral("Beta")},
      {temp.filePath(QStringLiteral("Projects/Gamma.md")),
       QStringLiteral("Gamma")}};

  LinkGraphIndex index;
  index.setNotes(temp.path(), notes);
  index.updateNote(notes[0].path, notes[0].title,
                   QStringLiteral("[[Beta]] [[beta|again]] [[Missing]] "
                                  "[[#Local]] `[[Hidden]]`"));
  index.updateNote(notes[1].path, notes[1].title,
                   QStringLiteral("[[Alpha]] [[Beta]]"));
  index.updateNote(notes[2].path, notes[2].title, QString());

  LinkGraphIndex::Snapshot graph = index.snapshot();
  const int alpha = nodeForTitle(graph, QStringLiteral("Alpha"));
  const int beta = nodeForTitle(graph, QStringLiteral("Beta"));
  const int gamma = nodeForTitle(graph, QStringLiteral("Gamma"));
  const int missing = nodeForTitle(graph, QStringLiteral("Missing"), true);
  check(
      graph.nodes.size() == 4 && graph.edges.size() == 4,
      QStringLiteral("snapshot contains notes, unresolved target, and edges"));
  check(alpha >= 0 && beta >= 0 && gamma >= 0 && missing >= 0,
        QStringLiteral("snapshot exposes every expected node"));
  check(edge(graph, alpha, beta) && edge(graph, alpha, beta)->occurrences == 2,
        QStringLiteral("parallel wiki links aggregate case-insensitively"));
  check(edge(graph, alpha, missing) && edge(graph, beta, alpha) &&
            edge(graph, beta, beta),
        QStringLiteral("missing, incoming, and self links remain represented"));
  check(graph.nodes.at(gamma).folder == QStringLiteral("Projects"),
        QStringLiteral("nodes retain their top-level folder"));

  // Adding the formerly missing note resolves stored targets without
  // reparsing Alpha.
  QVector<Note> expanded = notes;
  const Note resolved{temp.filePath(QStringLiteral("Missing.md")),
                      QStringLiteral("Missing")};
  expanded.push_back(resolved);
  index.setNotes(temp.path(), expanded);
  graph = index.snapshot();
  const int resolvedId = nodeForTitle(graph, QStringLiteral("Missing"));
  check(
      graph.nodes.size() == 4 && resolvedId >= 0 &&
          edge(graph, nodeForTitle(graph, QStringLiteral("Alpha")), resolvedId),
      QStringLiteral("note-set changes re-resolve targets without a reparse"));

  index.renamePath(notes[1].path,
                   temp.filePath(QStringLiteral("Beta Moved.md")),
                   QStringLiteral("Beta Moved"));
  graph = index.snapshot();
  check(nodeForTitle(graph, QStringLiteral("Beta Moved")) >= 0 &&
            nodeForTitle(graph, QStringLiteral("Beta"), true) >= 0,
        QStringLiteral(
            "rename updates the node and leaves stale targets unresolved"));
}
} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  testSemanticScanner();
  testHeadingOutline();
  testGraphIndex();
  if (failures == 0)
    QTextStream(stdout) << "All link-graph tests passed.\n";
  return failures == 0 ? 0 : 1;
}
