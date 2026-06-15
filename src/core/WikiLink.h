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

} // namespace WikiLink
