#pragma once

#include <QRegularExpression>
#include <QSizeF>
#include <QString>
#include <QVector>

class QPainter;
class QFont;
class QRectF;
class QColor;

// A tiny TeX-subset math typesetter, dependency-free (just QPainter + Qt fonts).
//
// It backs Emerald's inline $…$ live preview: the highlighter conceals the
// source and reserves its rendered width, then the editor paints the formula
// over that gap. Supported: \commands → Unicode symbols (Greek, operators,
// relations, arrows), ^ / _ super/subscripts, {grouping}, \frac{a}{b} and
// \sqrt{x}. Anything else falls through as literal text. Full LaTeX (macros,
// environments, matrices) is intentionally out of scope.
namespace MathRender {

// The canonical inline-math delimiter pattern, shared by the highlighter
// (concealment + width reservation) and the editor (overlay painting) so both
// agree on exactly what is and isn't a formula.
const QRegularExpression &pattern();

// A whole-line display formula: $$ … $$ alone on its line. Capture 1 is the
// body. Shared so the highlighter (height reservation) and editor (centred
// painting) agree on what counts as a display block.
const QRegularExpression &displayPattern();

// Multi-line display-math blocks: a block opens on a line beginning with "$$"
// (that isn't a self-contained single-line $$…$$) and closes at the next "$$",
// which may both carry formula content, Obsidian-style:
//   $$\sum_{i=1}^{n}
//   \frac{a}{b}$$
// `opensBlock` recognises an opening line; `bodyAfterOpen`/`bodyBeforeClose`
// return the formula text such a line contributes (after the opening $$ / before
// the closing $$). Shared so the highlighter (block-state tracking, height
// reservation) and the editor (region painting) agree on the delimiters.
bool opensBlock(const QString &line);
QString bodyAfterOpen(const QString &line);
QString bodyBeforeClose(const QString &line);

// The font a formula is drawn with: the editor's base font, italicised; display
// math is enlarged so a $$ block reads as a centred display, not inline text.
// Both the highlighter (reserving space) and the editor (painting) call this so
// their sizes can never drift apart.
QFont mathFont(const QFont &base, bool display);

// Inline math sits on the surrounding text's baseline, left-aligned in its
// reserved gap; display math is centred both ways in its (taller) block.
enum class Align { Inline, Display };

// One $…$ occurrence within a single line: `start`/`length` cover the whole
// match (delimiters included, i.e. the region to paint over); `body` is the
// formula text between the dollars.
struct Span {
    int start;
    int length;
    QString body;
};
QVector<Span> spans(const QString &line);

// Natural size of a formula body rendered at `font` (no scaling applied).
// `display` enables display-style layout (stacked limits on big operators), so
// the highlighter reserves the right height for a $$…$$ block.
QSizeF measure(const QString &body, const QFont &font, bool display = false);

// Lay out `body` and draw it inside `rect`, scaled down only if it would not
// otherwise fit, using `color`. `align` chooses inline (left + baseline) vs
// display (centred) placement.
void paint(QPainter &p, const QRectF &rect, const QString &body,
           const QFont &font, const QColor &color,
           Align align = Align::Inline);

} // namespace MathRender
