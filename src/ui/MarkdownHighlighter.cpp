#include "MarkdownHighlighter.h"

#include "MarkdownCallout.h"
#include "MarkdownStyle.h"

#include "MathRender.h"
#include "core/MascotSeed.h"
#include "core/SpellChecker.h"
#include "core/WikiLink.h"
#include <QFont>
#include <QFontMetricsF>
#include <QTextDocument>

namespace {
const QRegularExpression &imageLineRe() {
    static const QRegularExpression re(QStringLiteral(
        "^\\s*!\\[[^\\]\\n]*\\]\\((?:<([^>]+)>|([^\\)\\n]+))\\)\\s*$"));
    return re;
}

const QRegularExpression &inlineCodeRe() {
    static const QRegularExpression re(QStringLiteral("`([^`]+)`"));
    return re;
}

const QRegularExpression &internetLinkRe() {
    static const QRegularExpression re(
        QStringLiteral("\\[([^\\]\\[]+)\\]\\(([^)\\s]+)\\)"));
    return re;
}

double headingScale(int level) {
    switch (level) {
    case 1:  return 2.0;
    case 2:  return 1.6;
    case 3:  return 1.35;
    case 4:  return 1.15;
    case 5:  return 1.0;
    default: return 0.92;
    }
}

int headingWeight(int level) {
    return level <= 3 ? QFont::Bold : QFont::DemiBold;
}

// "monospace" is a fontconfig generic alias that only resolves on Linux; on
// Windows/macOS it falls back to a proportional font, which breaks code blocks.
// List real per-platform families plus a Monospace
// style hint so a fixed-width font is picked everywhere.
void applyMono(QTextCharFormat &fmt) {
    static const QStringList families{
        QStringLiteral("Menlo"),            // macOS
        QStringLiteral("Consolas"),         // Windows
        QStringLiteral("DejaVu Sans Mono"), // common on Linux
        QStringLiteral("monospace")};       // fontconfig generic fallback
    fmt.setFontFamilies(families);
    fmt.setFontStyleHint(QFont::Monospace);
}
} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
    if (document) {
        const double pt = document->defaultFont().pointSizeF();
        if (pt > 0)
            m_baseSize = pt;
    }

    m_heading.setForeground(QColor("#d7eee2"));
    m_heading.setFontWeight(QFont::Bold);

    m_bold.setForeground(QColor("#d7eee2"));
    m_bold.setFontWeight(QFont::Bold);

    m_code.setForeground(QColor("#7ee0b0"));
    applyMono(m_code);

    // No background on the fenced-code formats: the editor already paints the
    // block's dark body as one rounded rect (MarkdownEditor::paintEvent's `full`
    // rect, drawn in every state). A per-line background here would be a second
    // fill of the same colour stacked on top — redundant, it squares off the
    // rounded corners, and where it meets the green header it can leave a
    // hairline seam under GPU/fractional-scale compositing.
    m_codeBlock.setForeground(QColor("#a9c8b8"));
    applyMono(m_codeBlock);

    m_codeLang.setForeground(QColor("#7ee0b0"));
    applyMono(m_codeLang);
    m_codeLang.setFontItalic(true);

    m_strike.setForeground(QColor("#5e7d6d"));
    m_strike.setFontStrikeOut(true);

    m_highlight.setForeground(MarkdownStyle::highlightForeground());
    m_highlight.setBackground(MarkdownStyle::highlightBackground());

    m_link.setForeground(QColor("#2bbf74"));
    m_link.setFontUnderline(true);

    m_quote.setForeground(QColor("#92b3a2"));
    m_quote.setFontItalic(true);

    m_calloutTitle.setForeground(QColor("#2bbf74"));
    m_calloutTitle.setFontWeight(QFont::Bold);
    m_calloutTitle.setFontItalic(false);

    m_rule.setForeground(QColor("#4f7565"));

    m_listMarker.setForeground(QColor("#2bbf74"));
    m_listMarker.setFontWeight(QFont::Bold);

    m_taskDone.setForeground(QColor("#4f7565"));
    m_taskDone.setFontStrikeOut(true);

    m_marker.setForeground(QColor("#4f7565"));

    // A recognised mascot seed line: italic + the accent green so editing it
    // (after revealing it with Up) reads clearly as "this seed is understood".
    m_mascot.setForeground(QColor("#2bbf74"));
    m_mascot.setFontItalic(true);

    // Inline math: a soft teal italic so a formula reads as a distinct mode.
    m_math.setForeground(QColor("#6fcfc0"));
    m_math.setFontItalic(true);

    m_reHeading    = QRegularExpression(QStringLiteral("^(#{1,6})\\s+(.+)$"));
    m_reFence      = QRegularExpression(QStringLiteral("^\\s*(```|~~~)\\s*(\\S*).*$"));
    m_reQuote      = QRegularExpression(
        QStringLiteral("^(\\s*(?:>\\s*)+)(.*)$"));
    m_reRule       = QRegularExpression(
        QStringLiteral("^\\s*([-*_])\\s*(?:\\1\\s*){2,}$"));
    m_reTask       = QRegularExpression(
        QStringLiteral("^(\\s*[-*+]\\s+\\[)([ xX])(\\]\\s+)(.*)$"));
    m_reList       = QRegularExpression(
        QStringLiteral("^(\\s*)([-*+]|\\d+[.)])(\\s+)"));
    m_reCode       = inlineCodeRe();
    m_reLink = internetLinkRe();
}

void MarkdownHighlighter::setActiveBlock(int caretBlock, int anchorBlock,
                                         int caretColumn,
                                         bool hasSelection) {
    if (anchorBlock < 0)
        anchorBlock = caretBlock;
    const int newFirst = qMin(caretBlock, anchorBlock);
    const int newLast = qMax(caretBlock, anchorBlock);
    const bool sameBlockState =
        caretBlock == m_activeBlock && newFirst == m_selFirst &&
        newLast == m_selLast && hasSelection == m_hasSelection;
    if (sameBlockState && caretColumn == m_caretColumn)
        return;
    const int oldCaret = m_activeBlock, oldFirst = m_selFirst,
              oldLast = m_selLast;
    m_activeBlock = caretBlock;
    m_selFirst = newFirst;
    m_selLast = newLast;
    m_caretColumn = caretColumn;
    m_hasSelection = hasSelection;
    // Moving within one source line cannot change which Markdown markers are
    // revealed. It can only move into/out of the word spelling deliberately
    // suppresses under the caret, so revisit this one block exactly once.
    if (sameBlockState) {
        if (QTextDocument *doc = document())
            rehighlightBlock(doc->findBlockByNumber(caretBlock));
        return;
    }
    // A $$ math region reveals/conceals as a whole, so rehighlight every line of
    // the region the caret left and the one it entered — not just the two lines.
    rehighlightAround(oldCaret);
    rehighlightAround(caretBlock);
    // Every line reveals its raw markup when the selection covers it, so
    // rehighlight each line in the union of the old and new selection spans
    // whose membership changed. Math and fenced code blocks reveal as a whole,
    // so any region touched by the span is rehighlighted end to end.
    QTextDocument *doc = document();
    if (!doc)
        return;
    // A $$ math block and a ``` code block carry their state on the opening +
    // body lines (StateMath / StateCode), with the closing line StateNormal.
    auto regional = [](const QTextBlock &x) {
        return x.userState() == StateMath || x.userState() == StateCode;
    };
    for (int n = qMin(oldFirst, newFirst); n <= qMax(oldLast, newLast);) {
        const QTextBlock b = doc->findBlockByNumber(n);
        if (!b.isValid())
            break;
        QTextBlock first, last;
        bool inRegion = false;
        if (regional(b)) { // opening or body line
            first = last = b;
            while (first.previous().isValid() && regional(first.previous()))
                first = first.previous();
            while (last.next().isValid() && regional(last))
                last = last.next(); // ends on the closing line
            inRegion = true;
        } else if (b.previous().isValid() && regional(b.previous())) {
            first = b.previous(); // b is the closing line
            last = b;
            while (first.previous().isValid() && regional(first.previous()))
                first = first.previous();
            inRegion = true;
        }
        if (inRegion) {
            // Rehighlight the entire region so BOTH delimiters flip together,
            // even when one lies outside the changed span (e.g. selecting from
            // inside a block outward, then collapsing the selection).
            for (QTextBlock x = first; x.isValid(); x = x.next()) {
                rehighlightBlock(x);
                if (x == last)
                    break;
            }
            n = last.blockNumber() + 1;
        } else {
            // Plain line: rehighlight it when it entered or left the selection
            // span, so its markup reveals or re-renders to match.
            const bool was = n >= oldFirst && n <= oldLast;
            const bool now = n >= newFirst && n <= newLast;
            if (was != now)
                rehighlightBlock(b);
            ++n;
        }
    }
}

void MarkdownHighlighter::setSpellChecker(SpellChecker *checker) {
    if (m_spellChecker == checker)
        return;
    m_spellChecker = checker;
    rehighlight();
}

void MarkdownHighlighter::rehighlightAround(int blockNumber) {
    QTextDocument *doc = document();
    if (!doc)
        return;
    QTextBlock b = doc->findBlockByNumber(blockNumber);
    if (!b.isValid())
        return;
    // Both a $$ math block and a ``` fenced code block reveal/conceal their
    // delimiters as a whole region (the code block now shows both fences while
    // the caret is anywhere inside it), so when the caret enters or leaves one,
    // rehighlight every line of the region — not just the line it sat on. The
    // two states have the same shape: the opening + middle/body lines carry the
    // state, the closing line is StateNormal.
    auto regional = [](const QTextBlock &x) {
        return x.userState() == StateMath || x.userState() == StateCode;
    };
    QTextBlock first, last;
    bool inRegion = false;
    if (regional(b)) { // opening line or a middle/body line of a block
        first = last = b;
        while (first.previous().isValid() && regional(first.previous()))
            first = first.previous();
        while (last.next().isValid() && regional(last))
            last = last.next(); // ends on the closing line (first non-regional)
        inRegion = true;
    } else if (b.previous().isValid() && regional(b.previous())) {
        last = b; // b is the closing line
        first = b.previous();
        while (first.previous().isValid() && regional(first.previous()))
            first = first.previous();
        inRegion = true;
    }
    if (!inRegion) {
        rehighlightBlock(b);
        return;
    }
    for (QTextBlock x = first; x.isValid(); x = x.next()) {
        rehighlightBlock(x);
        if (x == last)
            break;
    }
}

void MarkdownHighlighter::setBaseSize(double pt) {
    if (pt > 0 && !qFuzzyCompare(pt, m_baseSize)) {
        m_baseSize = pt;
        if (!m_suspended)
            rehighlight();
    }
}

QTextCharFormat MarkdownHighlighter::conceal() const {
    QTextCharFormat f;
    f.setForeground(QColor(0, 0, 0, 0)); // transparent
    f.setFontPointSize(0.5);             // shrink the glyphs
    // Collapse each glyph's advance to ~1% of its width. At 0.5pt alone the
    // residual advance was negligible for short markers but accumulated into a
    // visible gap before a long hidden run — e.g. the "[[Hand drawn mascots|"
    // target preceding a wiki link's alias, or a long "](url)" tail. (A 0%
    // spacing is treated as "unset" and ignored, so use 1%.)
    f.setFontLetterSpacingType(QFont::PercentageSpacing);
    f.setFontLetterSpacing(1);
    return f;
}

QTextCharFormat
MarkdownHighlighter::inlineFormat(const QTextCharFormat &overlay) const {
    return overlay;
}

MarkdownHighlighter::EmphasisAnalysis MarkdownHighlighter::analyzeEmphasis(
    const QString &text, const QList<bool> &consumed, int seedStyle,
    int seedStart, int seedEnd) {
    const int n = text.size();
    EmphasisAnalysis result{QList<int>(n, 0), QList<bool>(n, false)};

    // Seed a pre-existing style over a range (a done task's strikethrough) so
    // emphasis inside it stacks rather than overwrites.
    if (seedStyle)
        for (int i = qMax(0, seedStart); i < seedEnd && i < n; ++i)
            result.mask[i] |= seedStyle;

    auto addStyle = [&](int s, int e, int style) {
        for (int i = qMax(0, s); i < e && i < n; ++i)
            result.mask[i] |= style;
    };
    auto markDelim = [&](int s, int e) {
        for (int i = qMax(0, s); i < e && i < n; ++i)
            result.delimiters[i] = true;
    };

    // Two-character delimiters (== highlight, ~~ strike): simple open/close
    // toggles. Different kinds can nest because each pass only adds flags.
    auto pairTwoChar = [&](QChar c, int style) {
        int open = -1;
        for (int i = 0; i + 1 < n;) {
            if (!consumed[i] && !consumed[i + 1] && text[i] == c &&
                text[i + 1] == c) {
                if (open < 0) {
                    open = i;
                } else {
                    addStyle(open + 2, i, style);
                    markDelim(open, open + 2);
                    markDelim(i, i + 2);
                    open = -1;
                }
                i += 2;
            } else {
                ++i;
            }
        }
    };
    pairTwoChar(QLatin1Char('='), SHighlight);
    pairTwoChar(QLatin1Char('~'), SStrike);

    // Asterisk / underscore runs. A run of length L offers L/2 bold units and
    // L%2 italic units; each kind pairs on its own stack. Underscores obey the
    // intraword rule so snake_case remains literal.
    struct Open {
        int contentStart;
        int delimStart;
        int delimEnd;
    };
    auto matchRuns = [&](QChar c, bool wordRule) {
        QList<Open> boldStack, italStack;
        for (int i = 0; i < n;) {
            if (consumed[i] || text[i] != c) {
                ++i;
                continue;
            }
            int j = i;
            while (j < n && text[j] == c && !consumed[j])
                ++j;
            const int len = j - i;
            const QChar before = i > 0 ? text[i - 1] : QLatin1Char(' ');
            const QChar after = j < n ? text[j] : QLatin1Char(' ');
            const bool canOpen = !wordRule || !before.isLetterOrNumber();
            const bool canClose = !wordRule || !after.isLetterOrNumber();
            int bold = len / 2, ital = len % 2;
            if (canClose) {
                while (bold > 0 && !boldStack.isEmpty()) {
                    const Open o = boldStack.takeLast();
                    addStyle(o.contentStart, i, SBold);
                    markDelim(o.delimStart, o.delimEnd);
                    markDelim(i, j);
                    --bold;
                }
                while (ital > 0 && !italStack.isEmpty()) {
                    const Open o = italStack.takeLast();
                    addStyle(o.contentStart, i, SItalic);
                    markDelim(o.delimStart, o.delimEnd);
                    markDelim(i, j);
                    --ital;
                }
            }
            if (canOpen) {
                for (int k = 0; k < bold; ++k)
                    boldStack.append({j, i, j});
                for (int k = 0; k < ital; ++k)
                    italStack.append({j, i, j});
            }
            i = j;
        }
    };
    matchRuns(QLatin1Char('*'), false);
    matchRuns(QLatin1Char('_'), true);
    return result;
}

int MarkdownHighlighter::inlinePreviewColumnCount(const QString &text) {
    QList<bool> consumed(text.size(), false);
    QList<bool> hidden(text.size(), false);

    auto available = [&](int start, int end) {
        if (start < 0 || end > text.size() || start >= end)
            return false;
        for (int i = start; i < end; ++i)
            if (consumed[i])
                return false;
        return true;
    };
    auto consume = [&](int start, int end) {
        for (int i = qMax(0, start); i < end && i < text.size(); ++i)
            consumed[i] = true;
    };
    auto hide = [&](int start, int end) {
        for (int i = qMax(0, start); i < end && i < text.size(); ++i)
            hidden[i] = true;
    };

    // Keep this order in lock-step with highlightBlock's exclusive inline
    // passes: code, math, internet links, wiki links, then emphasis.
    auto codeIt = inlineCodeRe().globalMatch(text);
    while (codeIt.hasNext()) {
        const auto match = codeIt.next();
        const int start = match.capturedStart(0), end = match.capturedEnd(0);
        if (!available(start, end))
            continue;
        hide(start, match.capturedStart(1));
        hide(match.capturedEnd(1), end);
        consume(start, end);
    }

    auto mathIt = MathRender::pattern().globalMatch(text);
    while (mathIt.hasNext()) {
        const auto match = mathIt.next();
        const int start = match.capturedStart(0), end = match.capturedEnd(0);
        if (!available(start, end))
            continue;
        hide(start, match.capturedStart(1));
        hide(match.capturedEnd(1), end);
        consume(start, end);
    }

    auto internetIt = internetLinkRe().globalMatch(text);
    while (internetIt.hasNext()) {
        const auto match = internetIt.next();
        const int start = match.capturedStart(0), end = match.capturedEnd(0);
        if (!available(start, end))
            continue;
        hide(start, match.capturedStart(1));
        hide(match.capturedEnd(1), end);
        consume(start, end);
    }

    auto wikiIt = WikiLink::pattern().globalMatch(text);
    while (wikiIt.hasNext()) {
        const auto match = wikiIt.next();
        const int start = match.capturedStart(0), end = match.capturedEnd(0);
        if (!available(start, end))
            continue;
        const int innerStart = match.capturedStart(1);
        const int innerEnd = match.capturedEnd(1);
        const int pipe = match.captured(1).indexOf(QLatin1Char('|'));
        const int displayStart = pipe >= 0 ? innerStart + pipe + 1 : innerStart;
        hide(start, displayStart);
        hide(innerEnd, end);
        consume(start, end);
    }

    const EmphasisAnalysis emphasis = analyzeEmphasis(text, consumed);
    for (int i = 0; i < emphasis.delimiters.size(); ++i)
        if (emphasis.delimiters[i])
            hidden[i] = true;

    int columns = 0;
    for (bool isHidden : hidden)
        if (!isHidden)
            ++columns;
    return columns;
}

QList<int> MarkdownHighlighter::tablePipePositions(const QString &text) {
    QList<bool> protectedSpan(text.size(), false);
    auto protectMatches = [&](const QRegularExpression &pattern) {
        auto it = pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            for (int i = match.capturedStart(0);
                 i < match.capturedEnd(0) && i < protectedSpan.size(); ++i)
                if (i >= 0)
                    protectedSpan[i] = true;
        }
    };
    protectMatches(inlineCodeRe());
    protectMatches(MathRender::pattern());
    protectMatches(internetLinkRe());
    protectMatches(WikiLink::pattern());

    QList<int> pipes;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] != QLatin1Char('|') || protectedSpan[i])
            continue;
        int slashes = 0;
        for (int j = i - 1; j >= 0 && text[j] == QLatin1Char('\\'); --j)
            ++slashes;
        if ((slashes % 2) == 0)
            pipes.append(i);
    }
    return pipes;
}

void MarkdownHighlighter::reserveDisplayHeight(int len, const QString &body) {
    const QFont base = document() ? document()->defaultFont() : QFont();
    const QFont f = MathRender::mathFont(base, true);
    const double formulaH = MathRender::measure(body, f, true).height();
    const double lineH = QFontMetricsF(base).lineSpacing();
    // Grow the line by enlarging the (transparent) glyphs' point size — the
    // lever headings use — to about the formula's height plus a little padding.
    // 1% letter-spacing collapses their advance so the tall glyphs never widen
    // the line or force a wrap.
    const double factor =
        lineH > 0 ? qBound(1.0, (formulaH + 0.5 * lineH) / lineH, 8.0) : 1.0;
    QTextCharFormat hide;
    hide.setForeground(QColor(0, 0, 0, 0));
    hide.setFontPointSize(base.pointSizeF() * factor);
    hide.setFontLetterSpacingType(QFont::PercentageSpacing);
    hide.setFontLetterSpacing(1);
    setFormat(0, len, hide);
}

void MarkdownHighlighter::reserveImageHeight(int len) {
    // Image height belongs to MarkdownEditor, which can resolve the safe local
    // file, inspect its aspect ratio and apply viewport-aware block geometry.
    // The highlighter only conceals the source span horizontally.
    setFormat(0, len, conceal());
}

bool MarkdownHighlighter::caretInMathRegion(const QTextBlock &block,
                                            bool openingHere) const {
    auto isFence = [](const QTextBlock &b) {
        return b.text().contains(QStringLiteral("$$"));
    };
    int openNum, closeNum;
    if (openingHere) {
        // Region runs from this opening fence down to the next "$$".
        openNum = closeNum = block.blockNumber();
        for (QTextBlock b = block.next(); b.isValid(); b = b.next()) {
            closeNum = b.blockNumber();
            if (isFence(b))
                break;
        }
    } else {
        // A body or closing line: the opening fence is the nearest "$$" above.
        openNum = -1;
        for (QTextBlock b = block.previous(); b.isValid(); b = b.previous())
            if (isFence(b)) {
                openNum = b.blockNumber();
                break;
            }
        if (openNum < 0)
            return false;
        closeNum = block.blockNumber();
        if (!isFence(block)) // a body line: the closing fence is below it
            for (QTextBlock b = block.next(); b.isValid(); b = b.next()) {
                closeNum = b.blockNumber();
                if (isFence(b))
                    break;
            }
    }
    // Reveal when the caret sits in the region or the selection overlaps it.
    return m_selLast >= openNum && m_selFirst <= closeNum;
}

bool MarkdownHighlighter::caretInCodeRegion(const QTextBlock &block,
                                            bool openingHere) const {
    auto isFence = [this](const QTextBlock &b) {
        return m_reFence.match(b.text()).hasMatch();
    };
    int openNum, closeNum;
    if (openingHere) {
        // Region runs from this opening fence down to the next fence (or, for an
        // unterminated block, to the end of the document).
        openNum = closeNum = block.blockNumber();
        for (QTextBlock b = block.next(); b.isValid(); b = b.next()) {
            closeNum = b.blockNumber();
            if (isFence(b))
                break;
        }
    } else {
        // A body or closing line: the opening fence is the nearest fence above.
        openNum = -1;
        for (QTextBlock b = block.previous(); b.isValid(); b = b.previous())
            if (isFence(b)) {
                openNum = b.blockNumber();
                break;
            }
        if (openNum < 0)
            return false;
        closeNum = block.blockNumber();
        if (!isFence(block)) // a body line: the closing fence is below it
            for (QTextBlock b = block.next(); b.isValid(); b = b.next()) {
                closeNum = b.blockNumber();
                if (isFence(b))
                    break;
            }
    }
    // Reveal both fences whenever the caret or the selection touches the region,
    // so selecting the whole block (or any part of it) shows the raw ``` source
    // instead of the rendered box. setActiveBlock rehighlights every code-region
    // line in the changed selection span, and the editor's rendered-box painter
    // (MarkdownEditor::forEachCodeBlock) uses this same selection test, so the
    // two stay in sync (no "raw fence + rendered box at once").
    return m_selLast >= openNum && m_selFirst <= closeNum;
}

void MarkdownHighlighter::markup(int start, int len, QList<bool> &consumed,
                                 bool reveal) {
    setFormat(start, len, reveal ? inlineFormat(m_marker) : conceal());
    for (int i = start; i < start + len && i < consumed.size(); ++i)
        consumed[i] = true;
}

void MarkdownHighlighter::applyInline(const QRegularExpression &re,
                                      const QString &text, QList<bool> &consumed,
                                      const QTextCharFormat &contentFmt,
                                      bool reveal) {
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const int start = m.capturedStart(0);
        const int end = m.capturedEnd(0);

        bool overlaps = false;
        for (int i = start; i < end; ++i) {
            if (consumed[i]) {
                overlaps = true;
                break;
            }
        }
        if (overlaps)
            continue;

        const int contentStart = m.capturedStart(1);
        const int contentEnd = m.capturedEnd(1);
        setFormat(contentStart, contentEnd - contentStart,
                  inlineFormat(contentFmt));

        const QTextCharFormat markerFmt =
            reveal ? inlineFormat(m_marker) : conceal();
        setFormat(start, contentStart - start, markerFmt);
        setFormat(contentEnd, end - contentEnd, markerFmt);

        for (int i = start; i < end; ++i)
            consumed[i] = true;
    }
}

QTextCharFormat MarkdownHighlighter::emphasisFormat(int mask) const {
    QTextCharFormat f;
    if (mask & SBold)
        f.setFontWeight(QFont::Bold);
    if (mask & SItalic)
        f.setFontItalic(true);
    if (mask & (SStrike | SDone))
        f.setFontStrikeOut(true);
    // Foreground precedence (most specific wins): highlight reads dark-on-green,
    // else a completed-task's dim, else strike's dim, else the emphasis accent.
    // Background only from highlight.
    if (mask & SHighlight) {
        f.setForeground(m_highlight.foreground());
        f.setBackground(m_highlight.background());
    } else if (mask & SDone) {
        f.setForeground(m_taskDone.foreground());
    } else if (mask & SStrike) {
        f.setForeground(m_strike.foreground());
    } else if (mask & (SBold | SItalic)) {
        f.setForeground(m_bold.foreground()); // == m_italic's accent green
    }
    return f;
}

void MarkdownHighlighter::applyEmphasis(const QString &text,
                                        QList<bool> &consumed, bool reveal,
                                        int seedStyle, int seedStart,
                                        int seedEnd) {
    const int n = text.size();
    const EmphasisAnalysis analysis =
        analyzeEmphasis(text, consumed, seedStyle, seedStart, seedEnd);
    const QList<int> &mask = analysis.mask;
    const QList<bool> &delim = analysis.delimiters;

    // Apply: delimiters get the marker format (dimmed when revealed, hidden
    // otherwise); styled content gets a single merged format. Coalesce equal
    // adjacent characters into one setFormat call.
    const QTextCharFormat markerFmt =
        reveal ? inlineFormat(m_marker) : conceal();
    for (int i = 0; i < n;) {
        if (delim[i]) {
            int j = i;
            while (j < n && delim[j])
                ++j;
            setFormat(i, j - i, markerFmt);
            for (int k = i; k < j; ++k)
                consumed[k] = true;
            i = j;
        } else if (mask[i] && !consumed[i]) {
            const int m = mask[i];
            int j = i;
            while (j < n && !delim[j] && !consumed[j] && mask[j] == m)
                ++j;
            setFormat(i, j - i, emphasisFormat(m));
            i = j;
        } else {
            ++i;
        }
    }
}

void MarkdownHighlighter::applyWikiLinks(const QString &text,
                                         QList<bool> &consumed, bool reveal) {
    auto it = WikiLink::pattern().globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const int start = m.capturedStart(0);
        const int end = m.capturedEnd(0);

        bool overlaps = false;
        for (int i = start; i < end; ++i)
            if (consumed[i]) { overlaps = true; break; }
        if (overlaps)
            continue;

        const QString inner = m.captured(1);
        const int innerStart = m.capturedStart(1);
        const int innerEnd = m.capturedEnd(1);

        if (reveal) {
            // On the active line keep the raw text editable, just dim markers.
            setFormat(start, 2, inlineFormat(m_marker)); // [[
            setFormat(innerStart, inner.size(), inlineFormat(m_link));
            setFormat(innerEnd, end - innerEnd, inlineFormat(m_marker)); // ]]
        } else {
            // Show only the alias (text after '|'); hide the target + brackets.
            const int pipe = inner.indexOf(QLatin1Char('|'));
            const int displayStart =
                pipe >= 0 ? innerStart + pipe + 1 : innerStart;
            setFormat(start, displayStart - start, conceal());
            setFormat(displayStart, innerEnd - displayStart,
                      inlineFormat(m_link));
            setFormat(innerEnd, end - innerEnd, conceal());
        }

        for (int i = start; i < end; ++i)
            consumed[i] = true;
    }
}

void MarkdownHighlighter::applyInternetLinks(const QString &text,
                                             QList<bool> &consumed, bool reveal) {
    auto it = m_reLink.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const int start = m.capturedStart(0);
        const int end = m.capturedEnd(0);

        bool overlaps = false;
        for (int i = start; i < end; ++i)
            if (consumed[i]) { overlaps = true; break; }
        if (overlaps)
            continue;

        const int textStart = m.capturedStart(1);
        const int textEnd = m.capturedEnd(1);

        // The link text is shown either way; only the surrounding "[" and
        // "](url)" differ — dimmed on the active line, hidden off it.
        const QTextCharFormat wrap =
            reveal ? inlineFormat(m_marker) : conceal();
        setFormat(start, textStart - start, wrap); // "["
        setFormat(textStart, textEnd - textStart, inlineFormat(m_link));
        setFormat(textEnd, end - textEnd, wrap);   // "](url)"

        for (int i = start; i < end; ++i)
            consumed[i] = true;
    }
}

void MarkdownHighlighter::applyMath(const QString &text, QList<bool> &consumed,
                                    bool reveal) {
    auto it = MathRender::pattern().globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const int start = m.capturedStart(0);
        const int end = m.capturedEnd(0);

        bool overlaps = false;
        for (int i = start; i < end; ++i)
            if (consumed[i]) { overlaps = true; break; }
        if (overlaps)
            continue;

        const int innerStart = m.capturedStart(1);
        const int innerEnd = m.capturedEnd(1);

        if (reveal) {
            // On the cursor's own line keep the raw source editable: tint the
            // body and just dim the $ delimiters.
            setFormat(innerStart, innerEnd - innerStart, inlineFormat(m_math));
            setFormat(start, innerStart - start, inlineFormat(m_marker));
            setFormat(innerEnd, end - innerEnd, inlineFormat(m_marker));
        } else {
            // Off the active line the editor paints the formula over this span
            // (MarkdownEditor::paintEvent). Hide the source and reserve the
            // formula's rendered box: its width via percentage letter-spacing,
            // and — for a tall formula (\frac, \sqrt) — its height by enlarging
            // the (transparent) glyphs' point size so the line grows to fit it.
            // Without the height reservation the painter would scale a tall
            // formula down to a single text line, shrinking it and leaving a gap
            // before the next word (the reserved width no longer matched).
            const QFont base = document() ? document()->defaultFont() : QFont();
            const QFont f = MathRender::mathFont(base, false);
            const QSizeF sz = MathRender::measure(m.captured(1), f);
            const double lineH = QFontMetricsF(base).lineSpacing();
            const double hFactor =
                lineH > 0 ? qBound(1.0, sz.height() / lineH, 2.5) : 1.0;
            QTextCharFormat hide;
            hide.setForeground(QColor(0, 0, 0, 0));
            hide.setFontPointSize(base.pointSizeF() * hFactor);
            hide.setFontLetterSpacingType(QFont::PercentageSpacing);
            // Letter-spacing is a percentage of the (now enlarged) glyph
            // advances, so measure the source at that same enlarged size.
            QFont hideFont = base;
            hideFont.setPointSizeF(base.pointSizeF() * hFactor);
            const QString src = text.mid(start, end - start);
            const double srcW = QFontMetricsF(hideFont).horizontalAdvance(src);
            hide.setFontLetterSpacing(
                srcW > 0 ? qBound(1.0, 100.0 * sz.width() / srcW, 2000.0) : 100.0);
            setFormat(start, end - start, hide);
        }

        for (int i = start; i < end; ++i)
            consumed[i] = true;
    }
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    if (m_suspended) {
        setCurrentBlockState(StateNormal);
        return;
    }

    // The mascot seed lives on the first line (a hidden HTML comment). When the
    // user reveals and edits it, tint a *valid* seed so they can see it's being
    // interpreted correctly; a malformed line falls through to normal rendering.
    if (currentBlock().blockNumber() == 0 && MascotSeed::fromLine(text) != 0) {
        setFormat(0, text.size(), m_mascot);
        setCurrentBlockState(StateNormal);
        return;
    }

    // Markup reveals its raw source on the caret's line and on every line a
    // selection covers — so selecting across rendered text (headings, emphasis,
    // links, lists, tables, formulas…) shows the actual characters instead of
    // the rendered form. With no selection the span is just the caret's line
    // (m_selFirst == m_selLast == m_activeBlock, set in setActiveBlock).
    const int bn = currentBlock().blockNumber();
    const bool reveal = bn >= m_selFirst && bn <= m_selLast;

    // --- Fenced code blocks: a multi-line construct tracked via block state.
    // The editor paints the block's rounded background + copy button; here we
    // just colour the text and hide the ``` fences off the active line (so the
    // fence lines collapse to almost nothing).
    const auto fence = m_reFence.match(text);
    const bool fenceHere = fence.hasMatch();
    if (previousBlockState() == StateCode) {
        // Inside a code block: render verbatim, no inline parsing.
        setFormat(0, text.size(), m_codeBlock);
        if (fenceHere) {                       // closing fence
            // Reveal the closing fence whenever the caret is anywhere inside the
            // block (not only on this line), so both fences show together while
            // editing the code.
            if (caretInCodeRegion(currentBlock(), false))
                setFormat(0, text.size(), m_marker);
            else
                setFormat(fence.capturedStart(1), fence.capturedLength(1),
                          conceal());
            setCurrentBlockState(StateNormal);
        } else {
            setCurrentBlockState(StateCode);
        }
        return;
    }
    if (fenceHere) {                            // opening fence ```lang
        if (caretInCodeRegion(currentBlock(), true)) {
            setFormat(0, text.size(), m_codeBlock);
            if (fence.capturedLength(2) > 0)   // language tag
                setFormat(fence.capturedStart(2), fence.capturedLength(2),
                          m_codeLang);
            setFormat(0, fence.capturedEnd(1), m_marker);
        } else {
            // Hide the header text but keep the line's normal height: the editor
            // paints the header bar (language + copy button) over it.
            QTextCharFormat hidden;
            hidden.setForeground(QColor(0, 0, 0, 0));
            setFormat(0, text.size(), hidden);
        }
        setCurrentBlockState(StateCode);
        return;
    }
    // --- $$ display-math blocks. A block opens with "$$" (alone or with content
    // after it) and closes at the next "$$" (alone or with content before it),
    // spanning one or more lines — rendered as one centred formula by the
    // editor. The whole block shows raw source whenever the caret/selection
    // touches it.
    if (previousBlockState() == StateMath) {
        // Inside a block: this line continues it, and closes it if it has "$$".
        const bool closes = text.contains(QStringLiteral("$$"));
        if (caretInMathRegion(currentBlock(), false))
            setFormat(0, text.size(), m_math); // raw, editable
        else
            setFormat(0, text.size(), conceal()); // collapse continuation lines
        setCurrentBlockState(closes ? StateNormal : StateMath);
        return;
    }

    // Display math on a single line: $$ … $$.
    const auto disp = MathRender::displayPattern().match(text);
    if (disp.hasMatch()) {
        const int bodyStart = disp.capturedStart(1);
        const int bodyEnd = disp.capturedEnd(1);
        if (reveal) {
            setFormat(0, bodyStart, m_marker);
            setFormat(bodyStart, bodyEnd - bodyStart, m_math);
            setFormat(bodyEnd, text.size() - bodyEnd, m_marker);
        } else {
            reserveDisplayHeight(text.size(), disp.captured(1));
        }
        setCurrentBlockState(StateNormal);
        return;
    }

    // The opening line of a multi-line block.
    if (MathRender::opensBlock(text)) {
        if (caretInMathRegion(currentBlock(), true)) {
            setFormat(0, text.size(), m_math);
        } else {
            // Grow this line to the whole formula's height (the body parts of
            // every line joined); the continuation lines collapse to nothing.
            QString body = MathRender::bodyAfterOpen(text);
            for (QTextBlock b = currentBlock().next(); b.isValid();
                 b = b.next()) {
                const QString t = b.text();
                if (t.contains(QStringLiteral("$$"))) {
                    body += QLatin1Char(' ') + MathRender::bodyBeforeClose(t);
                    break;
                }
                body += QLatin1Char(' ') + t;
            }
            reserveDisplayHeight(text.size(), body);
        }
        setCurrentBlockState(StateMath);
        return;
    }
    const MarkdownCallout::QuotePrefix quote =
        MarkdownCallout::quotePrefix(text);
    // Encode quote depth in the block state so changing the previous line's
    // quote prefix automatically makes QSyntaxHighlighter revisit this line.
    // Code/math retain their compact historic states used by the editor.
    setCurrentBlockState(quote.depth > 0 ? StateQuoteBase + quote.depth
                                         : StateNormal);

    QList<bool> consumed(text.size(), false);
    int doneStart = -1, doneEnd = -1; // a completed task's label, struck below

    // Headings own the whole line.
    const auto h = m_reHeading.match(text);
    if (h.hasMatch()) {
        const int level = h.capturedLength(1);
        const int contentStart = h.capturedStart(2);

        QTextCharFormat headingFmt = m_heading;
        headingFmt.setFontPointSize(m_baseSize * headingScale(level));
        headingFmt.setFontWeight(headingWeight(level));
        setFormat(contentStart, h.capturedLength(2), headingFmt);

        if (reveal) {
            QTextCharFormat mk = m_marker;
            mk.setFontPointSize(m_baseSize * headingScale(level));
            setFormat(0, contentStart, mk);
        } else {
            setFormat(0, contentStart, conceal());
        }
        applySpelling(text);
        return;
    }

    // Horizontal rule: the whole line is the divider. On the active line show
    // the raw dashes; elsewhere hide them (keeping the line height) so the
    // editor can paint a full-width rule across the block.
    if (m_reRule.match(text).hasMatch()) {
        if (reveal) {
            setFormat(0, text.size(), m_rule);
        } else {
            QTextCharFormat hidden;
            hidden.setForeground(QColor(0, 0, 0, 0));
            setFormat(0, text.size(), hidden);
        }
        return;
    }

    // Standalone image lines render as local image previews when inactive. On
    // the active line, keep the Markdown source visible and editable.
    if (imageLineRe().match(text).hasMatch()) {
        if (!reveal)
            reserveImageHeight(text.size());
        return;
    }

    // Blockquote: dim the '>' marker, tint the rest, then fall through so
    // inline markup inside the quote still renders.
    const auto q = m_reQuote.match(text);
    if (q.hasMatch()) {
        markup(0, q.capturedLength(1), consumed, reveal);
        if (!reveal) {
            // Preserve the source marker's advance while hiding its glyphs.
            // A stable prefix keeps the quote's hanging indent unchanged when
            // the caret enters or leaves the line.
            QTextCharFormat hiddenMarker;
            hiddenMarker.setForeground(QColor(0, 0, 0, 0));
            setFormat(0, q.capturedLength(1), hiddenMarker);
        }
        setFormat(q.capturedStart(2), q.capturedLength(2), m_quote);

        const int previousQuoteDepth =
            previousBlockState() > StateQuoteBase
                ? previousBlockState() - StateQuoteBase
                : 0;
        const MarkdownCallout::TitleLine callout =
            MarkdownCallout::titleLine(text, previousQuoteDepth);
        if (callout.valid()) {
            QTextCharFormat titleFormat = m_calloutTitle;
            titleFormat.setForeground(MarkdownCallout::accent(callout.type));
            if (reveal) {
                // Keep the exact marker visible and editable on the active
                // line, but tint it so recognition is immediate while typing.
                setFormat(callout.markerStart,
                          callout.markerEnd - callout.markerStart,
                          titleFormat);
            } else if (callout.hasCustomTitle()) {
                // Reserve one invisible source character for the custom-painted
                // emoji, then collapse the rest of the type marker and gap.
                QTextCharFormat iconReserve;
                iconReserve.setForeground(QColor(0, 0, 0, 0));
                const QFont base = document() ? document()->defaultFont()
                                              : QFont();
                const QFontMetricsF metrics(base);
                const qreal reserved = metrics.horizontalAdvance(
                                           MarkdownCallout::emoji(callout.type)) +
                                       5.0;
                const qreal glyph = metrics.horizontalAdvance(
                    text.at(callout.markerStart));
                iconReserve.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                iconReserve.setFontLetterSpacing(
                    qMax(qreal(0), reserved - glyph));
                setFormat(callout.markerStart, 1, iconReserve);
                if (callout.titleStart > callout.markerStart + 1)
                    setFormat(callout.markerStart + 1,
                              callout.titleStart - callout.markerStart - 1,
                              conceal());
            } else {
                // With no explicit title, reserve '[' for the emoji, retain the
                // type letters as the label, and collapse the other syntax.
                QTextCharFormat iconReserve;
                iconReserve.setForeground(QColor(0, 0, 0, 0));
                const QFont base = document() ? document()->defaultFont()
                                              : QFont();
                const QFontMetricsF metrics(base);
                const qreal reserved = metrics.horizontalAdvance(
                                           MarkdownCallout::emoji(callout.type)) +
                                       5.0;
                const qreal glyph = metrics.horizontalAdvance(
                    text.at(callout.markerStart));
                iconReserve.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                iconReserve.setFontLetterSpacing(
                    qMax(qreal(0), reserved - glyph));
                setFormat(callout.markerStart, 1, iconReserve);
                setFormat(callout.markerStart + 1, 1, conceal());
                QTextCharFormat typeFormat = titleFormat;
                typeFormat.setFontCapitalization(QFont::Capitalize);
                setFormat(callout.typeStart, callout.typeLength, typeFormat);
                setFormat(callout.markerEnd - 1,
                          text.size() - (callout.markerEnd - 1), conceal());
            }
            if (callout.hasCustomTitle()) {
                setFormat(callout.titleStart,
                          text.size() - callout.titleStart, titleFormat);
            }
            for (int i = callout.markerStart;
                 i < callout.markerEnd && i < consumed.size(); ++i)
                consumed[i] = true;
        }
    }

    // Task list: "- [ ] ..." / "- [x] ...". Off the active line the editor
    // paints a real checkbox over the (hidden) "- [ ] " markup; completed
    // items are struck through.
    const auto task = m_reTask.match(text);
    if (task.hasMatch()) {
        const bool done = task.captured(2).trimmed().compare(
                              QStringLiteral("x"), Qt::CaseInsensitive) == 0;
        const int bracketOpen = task.capturedEnd(1) - 1; // the '[' position
        const int markerEnd = task.capturedEnd(3);       // start of the label
        if (reveal) {
            setFormat(0, markerEnd, m_listMarker);
        } else {
            // Preserve indentation, replace the '-' advance with exactly one
            // checkbox plus a compact label gap, and collapse the remaining
            // " [ ] " source. MarkdownEditor paints the box at that dash cell.
            QTextCharFormat space;
            space.setForeground(QColor(0, 0, 0, 0));
            int markerStart = 0;
            while (markerStart < markerEnd && text.at(markerStart).isSpace()) {
                setFormat(markerStart, 1, space);
                ++markerStart;
            }
            if (markerStart < markerEnd) {
                const QFont base = document() ? document()->defaultFont() : QFont();
                const QFontMetricsF metrics(base);
                constexpr qreal LabelGap = 5.0;
                const qreal reserved = metrics.ascent() * 0.92 + LabelGap;
                const qreal glyph =
                    metrics.horizontalAdvance(text.at(markerStart));
                QTextCharFormat boxReserve = space;
                boxReserve.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                boxReserve.setFontLetterSpacing(qMax(qreal(0), reserved - glyph));
                setFormat(markerStart, 1, boxReserve);

                const QTextCharFormat collapsed = conceal();
                for (int i = markerStart + 1;
                     i < markerEnd && i < text.size(); ++i)
                    setFormat(i, 1, collapsed);
            } else {
                // Defensive fallback for malformed input matched by a future
                // relaxed task expression.
                for (int i = 0; i < markerEnd && i < text.size(); ++i)
                    setFormat(i, 1, space);
            }
        }
        for (int i = 0; i < markerEnd && i < consumed.size(); ++i)
            consumed[i] = true;
        if (done) {
            // Strike the label through the emphasis pass (seeded SDone) so any
            // bold/italic/highlight on a word inside stacks with the strike
            // instead of overwriting it.
            doneStart = task.capturedStart(4);
            doneEnd = task.capturedEnd(4);
        }
    } else {
        // Plain bullet / ordered list marker.
        const auto list = m_reList.match(text);
        if (list.hasMatch()) {
            const int s = list.capturedStart(2);
            const int len = list.capturedLength(2);
            const QChar c = text.at(s);
            const bool bullet = c == '-' || c == '*' || c == '+';
            if (bullet && !reveal) {
                // Hide the dash (but keep its width) so the editor can paint a
                // real bullet glyph in its place.
                QTextCharFormat hidden;
                hidden.setForeground(QColor(0, 0, 0, 0));
                setFormat(s, len, hidden);
            } else {
                setFormat(s, len, m_listMarker);
            }
            for (int i = s; i < s + len && i < consumed.size(); ++i)
                consumed[i] = true;
        }
    }

    // Inline rules. The "exclusive" constructs run first and mark their spans
    // consumed so emphasis never re-parses inside them: code (verbatim), math
    // (its '_'/'*' aren't emphasis), and both link kinds (a URL may hold '_'/'*',
    // and [[wiki|targets]] aren't emphasis). Emphasis runs last over whatever's
    // left, and unlike the others it *accumulates* (bold+italic+strike+highlight
    // can all land on the same character).
    applyInline(m_reCode, text, consumed, m_code, reveal);
    applyMath(text, consumed, reveal);
    applyInternetLinks(text, consumed, reveal);
    applyWikiLinks(text, consumed, reveal);
    applyEmphasis(text, consumed, reveal,
                  doneStart >= 0 ? SDone : 0, doneStart, doneEnd);

    // Fold strikethrough into inline code and math that sit inside a struck span
    // — a ~~…~~ run or a completed task's label. Both consume their span before
    // the emphasis pass, so without this they'd render un-struck inside struck
    // text. Code is real text here, so striking its format is enough; off the
    // active line the editor paints the formula, so it strikes the math itself
    // (MarkdownEditor::paintEvent) and here we only need the revealed source.
    strikeConsumedInline(text, reveal, doneStart, doneEnd);
    applySpelling(text);
}

void MarkdownHighlighter::applySpelling(const QString &text) {
    if (!m_spellChecker || !m_spellChecker->isEnabled() ||
        !m_spellChecker->isReady())
        return;

    const int blockNumber = currentBlock().blockNumber();
    for (const SpellChecker::WordRange &word :
         SpellChecker::wordsInMarkdown(text)) {
        // Avoid the distracting red flash beneath a word while it is still
        // being typed. It is checked as soon as the caret leaves its range.
        if (!m_hasSelection && blockNumber == m_activeBlock &&
            m_caretColumn >= word.start &&
            m_caretColumn <= word.start + word.length)
            continue;
        if (m_spellChecker->isCorrect(word.word))
            continue;

        // Preserve every Markdown property already applied to the character
        // (bold, link colour, highlight, strike, etc.) and add only Qt's native
        // spell-check underline. Per-character merging also handles a word that
        // crosses an emphasis-format boundary correctly.
        for (int i = word.start; i < word.start + word.length; ++i) {
            QTextCharFormat misspelled = format(i);
            misspelled.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            misspelled.setUnderlineColor(QColor(QStringLiteral("#ef6b73")));
            setFormat(i, 1, misspelled);
        }
    }
}

// Characters that should carry a strikethrough: a completed task's whole label,
// plus every ~~…~~ span (ignoring ~~ that sit inside inline `code`, where
// they're literal). Shared shape with the editor's painter so a struck formula
// drawn there matches a struck `code` coloured here.
QList<bool> MarkdownHighlighter::struckMask(const QString &text, int doneStart,
                                            int doneEnd) const {
    QList<bool> struck(text.size(), false);
    for (int i = qMax(0, doneStart); i < doneEnd && i < struck.size(); ++i)
        struck[i] = true;
    QList<bool> codeMask(text.size(), false);
    auto cit = m_reCode.globalMatch(text);
    while (cit.hasNext()) {
        const auto cm = cit.next();
        for (int i = cm.capturedStart(); i < cm.capturedEnd() && i < codeMask.size();
             ++i)
            codeMask[i] = true;
    }
    int open = -1;
    for (int i = 0; i + 1 < text.size();) {
        if (!codeMask[i] && text[i] == QLatin1Char('~') &&
            text[i + 1] == QLatin1Char('~')) {
            if (open < 0)
                open = i + 2;
            else {
                for (int k = open; k < i && k < struck.size(); ++k)
                    struck[k] = true;
                open = -1;
            }
            i += 2;
        } else {
            ++i;
        }
    }
    return struck;
}

void MarkdownHighlighter::strikeConsumedInline(const QString &text, bool reveal,
                                               int doneStart, int doneEnd) {
    const QList<bool> struck = struckMask(text, doneStart, doneEnd);
    auto spanStruck = [&](int s, int e) {
        for (int i = qMax(0, s); i < e && i < struck.size(); ++i)
            if (struck[i])
                return true;
        return false;
    };

    // Inline code: re-colour its content with the strike added.
    auto cit = m_reCode.globalMatch(text);
    while (cit.hasNext()) {
        const auto cm = cit.next();
        if (spanStruck(cm.capturedStart(0), cm.capturedEnd(0))) {
            QTextCharFormat cf = inlineFormat(m_code);
            cf.setFontStrikeOut(true);
            setFormat(cm.capturedStart(1), cm.capturedLength(1), cf);
        }
    }

    // Wiki links are rendered before (and therefore excluded from) the
    // emphasis pass. Reapply their visible label with the link styling plus a
    // strike whenever an enclosing ~~...~~ span crosses the link.
    QList<bool> wikiProtected(text.size(), false);
    auto protectForWiki = [&](const QRegularExpression &pattern) {
        auto it = pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            for (int i = qMax(0, match.capturedStart(0));
                 i < match.capturedEnd(0) && i < wikiProtected.size(); ++i)
                wikiProtected[i] = true;
        }
    };
    protectForWiki(m_reCode);
    protectForWiki(MathRender::pattern());
    protectForWiki(m_reLink);

    auto wikiIt = WikiLink::pattern().globalMatch(text);
    while (wikiIt.hasNext()) {
        const auto match = wikiIt.next();
        if (!spanStruck(match.capturedStart(0), match.capturedEnd(0)))
            continue;
        bool protectedOverlap = false;
        for (int i = match.capturedStart(0);
             i < match.capturedEnd(0) && i < wikiProtected.size(); ++i) {
            if (i >= 0 && wikiProtected.at(i)) {
                protectedOverlap = true;
                break;
            }
        }
        if (protectedOverlap)
            continue;
        const QString inner = match.captured(1);
        const int innerStart = match.capturedStart(1);
        const int pipe = inner.indexOf(QLatin1Char('|'));
        const int displayStart =
            reveal || pipe < 0 ? innerStart : innerStart + pipe + 1;
        const int displayEnd = match.capturedEnd(1);
        QTextCharFormat link = inlineFormat(m_link);
        link.setFontStrikeOut(true);
        setFormat(displayStart, displayEnd - displayStart, link);
    }

    // Inline math: only the revealed (active-line) source is real text here;
    // off the active line the editor paints it and strikes it there.
    if (reveal) {
        for (const auto &sp : MathRender::spans(text)) {
            if (sp.length >= 2 && spanStruck(sp.start, sp.start + sp.length)) {
                QTextCharFormat cf = inlineFormat(m_math);
                cf.setFontStrikeOut(true);
                setFormat(sp.start + 1, sp.length - 2, cf); // body, between $ $
            }
        }
    }
}
