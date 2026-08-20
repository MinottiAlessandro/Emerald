#include "core/MarkdownImage.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {
int failures = 0;

void check(bool condition, const QString &message) {
    if (condition)
        return;
    QTextStream(stderr) << "FAIL: " << message << '\n';
    ++failures;
}

MarkdownImage::Image one(const QString &line,
                         const MarkdownImage::References &references = {}) {
    const auto images = MarkdownImage::imagesInLine(line, references);
    check(images.size() == 1,
          QStringLiteral("expected one image in: %1").arg(line));
    return images.isEmpty() ? MarkdownImage::Image{} : images.first();
}

void testInlineDestinations() {
    auto image = one(QStringLiteral("![Alt](images/photo.png)"));
    check(image.valid && image.resolved &&
              image.syntax == MarkdownImage::Syntax::Inline &&
              image.description == QStringLiteral("Alt") &&
              image.target == QStringLiteral("images/photo.png"),
          QStringLiteral("parse a basic inline image"));

    image = one(QStringLiteral(
        "![A *formatted* image](<images/photo%20one.png> \"A title\")"));
    check(image.description == QStringLiteral("A formatted image") &&
              image.target == QStringLiteral("images/photo one.png") &&
              image.title == QStringLiteral("A title"),
          QStringLiteral("parse angle destinations, formatting, percent "
                         "encoding, and titles"));

    image = one(QStringLiteral("![Alt](images/a_(draft).png 'Draft')"));
    check(image.target == QStringLiteral("images/a_(draft).png") &&
              image.title == QStringLiteral("Draft"),
          QStringLiteral("parse balanced destination parentheses"));

    image = one(QStringLiteral("![Alt|320x180](photo.png)"));
    check(image.description == QStringLiteral("Alt") &&
              image.dimensions == MarkdownImage::Dimensions{320, 180},
          QStringLiteral("parse Markdown image dimensions"));

    image = one(QStringLiteral(
        "![Sized|160x80](wide.png \"inline title\")"));
    check(image.dimensions == MarkdownImage::Dimensions{160, 80} &&
              image.title == QStringLiteral("inline title"),
          QStringLiteral("parse dimensions together with an inline title"));

    image = one(QStringLiteral("![250](photo.png)"));
    check(image.description.isEmpty() &&
              image.dimensions == MarkdownImage::Dimensions{250, 0},
          QStringLiteral("parse Obsidian-compatible bare width"));
}

void testReferences() {
    const QString source = QStringLiteral(
        "![Full][Hero]\n"
        "![Collapsed][]\n"
        "![Shortcut]\n"
        "[hero]: <images/hero one.png> \"Hero title\"\n"
        "[collapsed]: images/collapsed.png\n"
        "[shortcut]: images/shortcut.png\n"
        "  'On the next line'\n");
    QSet<int> definitionLines;
    const auto references =
        MarkdownImage::collectReferences(source, &definitionLines);
    check(references.size() == 3 && definitionLines == QSet<int>({3, 4, 5, 6}),
          QStringLiteral("collect reference definitions and continuation rows"));

    auto image = one(QStringLiteral("![Full][HERO]"), references);
    check(image.syntax == MarkdownImage::Syntax::FullReference &&
              image.target == QStringLiteral("images/hero one.png") &&
              image.title == QStringLiteral("Hero title"),
          QStringLiteral("resolve a case-insensitive full reference"));

    image = one(QStringLiteral("![Collapsed][]"), references);
    check(image.syntax == MarkdownImage::Syntax::CollapsedReference &&
              image.target == QStringLiteral("images/collapsed.png"),
          QStringLiteral("resolve a collapsed reference"));

    image = one(QStringLiteral("![Shortcut]"), references);
    check(image.syntax == MarkdownImage::Syntax::ShortcutReference &&
              image.target == QStringLiteral("images/shortcut.png") &&
              image.title == QStringLiteral("On the next line"),
          QStringLiteral("resolve a shortcut reference and continued title"));

    image = one(QStringLiteral("![Shortcut|120]"), references);
    check(image.target == QStringLiteral("images/shortcut.png") &&
              image.dimensions == MarkdownImage::Dimensions{120, 0},
          QStringLiteral("resolve a sized shortcut reference by its alt label"));

    check(MarkdownImage::imagesInLine(QStringLiteral("![Missing]"), references)
              .isEmpty(),
          QStringLiteral("leave unresolved shortcut references literal"));
    check(MarkdownImage::imagesInLine(QStringLiteral("![Missing]"), references,
                                      true)
                  .size() == 1,
          QStringLiteral("allow syntax-only image masking for spelling"));
}

void testObsidianAndPlacement() {
    auto image = one(QStringLiteral("![[attachments/photo.png|300x200]]"));
    check(image.syntax == MarkdownImage::Syntax::ObsidianEmbed &&
              image.target == QStringLiteral("attachments/photo.png") &&
              image.dimensions == MarkdownImage::Dimensions{300, 200},
          QStringLiteral("parse an Obsidian image embed with dimensions"));

    image = one(QStringLiteral("![[attachments/photo.webp|240]]"));
    check(image.dimensions == MarkdownImage::Dimensions{240, 0},
          QStringLiteral("parse a width-only Obsidian image embed"));

    check(MarkdownImage::imagesInLine(QStringLiteral("![[A Note]]"), {})
              .isEmpty(),
          QStringLiteral("do not treat an embedded note as an image"));

    const QString inlineLine =
        QStringLiteral("Before ![one](a.png) and ![[b.jpg|80]] after");
    check(MarkdownImage::imagesInLine(inlineLine, {}).size() == 2,
          QStringLiteral("find multiple inline image forms"));
    check(!MarkdownImage::standaloneImage(inlineLine, {}).valid &&
              MarkdownImage::standaloneImage(
                  QStringLiteral("  ![[b.jpg|80]]  "), {})
                  .valid,
          QStringLiteral("distinguish standalone and inline placement"));
    check(MarkdownImage::imagesInLine(
              QStringLiteral("`![code](a.png)` \\![escaped](b.png)"), {})
              .isEmpty(),
          QStringLiteral("ignore code and escaped image markers"));
}

void testDefinitionTrustBoundaries() {
    const QString source = QStringLiteral(
        "<!-- [comment]: hidden.png -->\n"
        "```md\n"
        "[code]: hidden.png\n"
        "```\n"
        "[live]: shown.png\n"
        "[LIVE]: ignored.png\n");
    QSet<int> lines;
    const auto references = MarkdownImage::collectReferences(source, &lines);
    QStringList lineStrings;
    for (const int line : lines)
        lineStrings.append(QString::number(line));
    lineStrings.sort();
    check(references.size() == 1 &&
              references.value(QStringLiteral("live")).target ==
                  QStringLiteral("shown.png") &&
              lines == QSet<int>({4, 5}),
          QStringLiteral("ignore comment/fence definitions and let the first "
                         "duplicate win (refs=%1, target=%2, lines=%3)")
              .arg(references.size())
              .arg(references.value(QStringLiteral("live")).target)
              .arg(lineStrings.join(',')));
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    testInlineDestinations();
    testReferences();
    testObsidianAndPlacement();
    testDefinitionTrustBoundaries();
    if (failures == 0)
        QTextStream(stdout) << "All Markdown image tests passed.\n";
    return failures == 0 ? 0 : 1;
}
