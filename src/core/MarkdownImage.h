#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

// Shared Markdown image parsing for Edit Mode, Read Mode, spelling, and
// navigation. The parser deliberately produces data rather than HTML so note
// content never crosses a browser/rendering trust boundary.
namespace MarkdownImage {

enum class Syntax {
    Inline,
    FullReference,
    CollapsedReference,
    ShortcutReference,
    ObsidianEmbed,
};

struct Dimensions {
    int width = 0;
    int height = 0;

    bool isEmpty() const { return width <= 0 && height <= 0; }
    bool operator==(const Dimensions &) const = default;
};

struct Reference {
    QString target;
    QString title;

    bool operator==(const Reference &) const = default;
};

using References = QHash<QString, Reference>;

struct Image {
    bool valid = false;
    // Direct destinations and resolved references are renderable syntax even
    // when their file is currently missing. An unresolved reference remains
    // literal CommonMark and therefore has resolved == false.
    bool resolved = false;
    Syntax syntax = Syntax::Inline;
    int start = 0;
    int length = 0;
    int descriptionStart = 0;
    int descriptionLength = 0;
    QString description;
    QString target;
    QString title;
    Dimensions dimensions;
};

// CommonMark reference labels compare case-insensitively after collapsing
// internal whitespace.
QString normalizeReferenceLabel(const QString &label);

// Collect link reference definitions outside fenced code and HTML comments.
// definitionLines receives every source block occupied by a valid definition,
// including a continuation line holding its optional title.
References collectReferences(const QString &source,
                             QSet<int> *definitionLines = nullptr);

// Parse image occurrences on one source line. By default unresolved reference
// forms are omitted because CommonMark treats them as literal text. Callers
// such as the spell checker may request them so their path-like syntax can be
// excluded even while a definition is incomplete.
QVector<Image> imagesInLine(const QString &line, const References &references,
                            bool includeUnresolvedReferences = false);

// Parse an image beginning exactly at start, or return an invalid Image.
Image imageAt(const QString &line, int start, const References &references,
              bool includeUnresolvedReferences = false);

// Return the image when it is the line's only non-whitespace content.
Image standaloneImage(const QString &line, const References &references);

// Obsidian embeds are considered images only for known image suffixes, keeping
// note, PDF, audio, and canvas embeds outside this feature.
bool isLikelyImageTarget(const QString &target);

} // namespace MarkdownImage
