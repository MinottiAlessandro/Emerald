#pragma once

#include <QString>

class QRegularExpression;

// The one true definition of a [[wiki-link]]. Every place that recognises or
// follows a link — the highlighter, the editor, and the link index — shares
// this pattern and cleaning rule so they can never drift apart.
namespace WikiLink {

// Matches [[target]]. Capture group 1 is the inner text (which may still carry
// a |alias or #heading). The inner class excludes '[' and ']' so a stray,
// unclosed "[[" cannot swallow a later, well-formed link.
const QRegularExpression &pattern();

// Normalise a link's inner text to its target note title:
// "Foo|bar" -> "Foo", "Foo#section" -> "Foo", trimmed.
QString cleanTarget(const QString &inner);

// Navigation destination with only the optional display alias removed:
// "Foo#section|bar" -> "Foo#section". Local "#section" links are retained.
QString cleanDestination(const QString &inner);

// The trimmed heading qualifier after the first '#', or empty when absent.
QString heading(const QString &inner);

// Source position of the first matching ATX heading outside fenced code and
// author-only comments. Matching is case-insensitive; -1 means not found.
int headingPosition(const QString &markdown, const QString &headingTarget);

} // namespace WikiLink
