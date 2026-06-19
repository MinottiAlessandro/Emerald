#include "MarkdownHighlighter.h"

#include "MathRender.h"
#include "core/MascotSeed.h"
#include "core/WikiLink.h"
#include <QFont>
#include <QFontMetricsF>
#include <QTextDocument>

namespace {
double headingScale(int level) {
    switch (level) {
    case 1:  return 1.9;
    case 2:  return 1.55;
    case 3:  return 1.3;
    case 4:  return 1.15;
    case 5:  return 1.05;
    default: return 1.0;
    }
}

// "monospace" is a fontconfig generic alias that only resolves on Linux; on
// Windows/macOS it falls back to a proportional font, which breaks code blocks
// and the pipe-table grid. List real per-platform families plus a Monospace
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

    m_italic.setForeground(QColor("#d7eee2"));
    m_italic.setFontItalic(true);

    m_boldItalic.setForeground(QColor("#d7eee2"));
    m_boldItalic.setFontWeight(QFont::Bold);
    m_boldItalic.setFontItalic(true);

    m_code.setForeground(QColor("#7ee0b0"));
    applyMono(m_code);
    m_code.setBackground(QColor("#16241c"));

    m_codeBlock.setForeground(QColor("#a9c8b8"));
    applyMono(m_codeBlock);
    m_codeBlock.setBackground(QColor("#13201a"));

    m_codeLang.setForeground(QColor("#7ee0b0"));
    applyMono(m_codeLang);
    m_codeLang.setFontItalic(true);
    m_codeLang.setBackground(QColor("#13201a"));

    m_strike.setForeground(QColor("#5e7d6d"));
    m_strike.setFontStrikeOut(true);

    m_highlight.setForeground(QColor("#101814"));
    m_highlight.setBackground(QColor("#7ee0a8"));

    m_link.setForeground(QColor("#2bbf74"));
    m_link.setFontUnderline(true);

    m_quote.setForeground(QColor("#92b3a2"));
    m_quote.setFontItalic(true);

    m_rule.setForeground(QColor("#4f7565"));

    m_listMarker.setForeground(QColor("#2bbf74"));
    m_listMarker.setFontWeight(QFont::Bold);

    m_taskDone.setForeground(QColor("#4f7565"));
    m_taskDone.setFontStrikeOut(true);

    m_table.setForeground(QColor("#a9c8b8"));
    applyMono(m_table);

    m_tableHeader = m_table;
    m_tableHeader.setForeground(QColor("#d7eee2"));
    m_tableHeader.setFontWeight(QFont::Bold);

    m_tablePipe = m_table;
    m_tablePipe.setForeground(QColor("#4f7565"));

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
    m_reQuote      = QRegularExpression(QStringLiteral("^(\\s*>+\\s?)(.*)$"));
    m_reRule       = QRegularExpression(
        QStringLiteral("^\\s*([-*_])\\s*(?:\\1\\s*){2,}$"));
    m_reTask       = QRegularExpression(
        QStringLiteral("^(\\s*[-*+]\\s+\\[)([ xX])(\\]\\s+)(.*)$"));
    m_reList       = QRegularExpression(
        QStringLiteral("^(\\s*)([-*+]|\\d+[.)])(\\s+)"));
    m_reBoldItalic = QRegularExpression(QStringLiteral("\\*\\*\\*([^*]+)\\*\\*\\*"));
    m_reBoldUnder  = QRegularExpression(QStringLiteral("\\*\\*_([^_]+)_\\*\\*"));
    m_reUnderBold  = QRegularExpression(QStringLiteral("_\\*\\*([^*]+)\\*\\*_"));
    m_reCode       = QRegularExpression(QStringLiteral("`([^`]+)`"));
    m_reBold       = QRegularExpression(QStringLiteral("\\*\\*([^*]+)\\*\\*"));
    m_reStrike     = QRegularExpression(QStringLiteral("~~([^~]+)~~"));
    m_reHighlight  = QRegularExpression(QStringLiteral("==([^=]+)=="));
    m_reItalicStar = QRegularExpression(QStringLiteral("\\*([^*]+)\\*"));
    m_reItalicUnder =
        QRegularExpression(QStringLiteral("(?<!\\w)_([^_]+)_(?!\\w)"));
    m_reTableSep =
        QRegularExpression(QStringLiteral("^\\s*\\|?[\\s:|-]*-[\\s:|-]*\\|?\\s*$"));
    m_reLink =
        QRegularExpression(QStringLiteral("\\[([^\\]\\[]+)\\]\\(([^)\\s]+)\\)"));
}

void MarkdownHighlighter::setActiveBlock(int caretBlock, int anchorBlock) {
    if (anchorBlock < 0)
        anchorBlock = caretBlock;
    const int newFirst = qMin(caretBlock, anchorBlock);
    const int newLast = qMax(caretBlock, anchorBlock);
    if (caretBlock == m_activeBlock && newFirst == m_selFirst &&
        newLast == m_selLast)
        return;
    const int oldCaret = m_activeBlock, oldFirst = m_selFirst,
              oldLast = m_selLast;
    m_activeBlock = caretBlock;
    m_selFirst = newFirst;
    m_selLast = newLast;
    // A $$ math region reveals/conceals as a whole, so rehighlight every line of
    // the region the caret left and the one it entered — not just the two lines.
    rehighlightAround(oldCaret);
    rehighlightAround(caretBlock);
    // Math also reveals when the selection covers it, so rehighlight every math
    // line in the union of the old and new selection spans (the lines whose
    // reveal state can have flipped). Plain lines are untouched — only the
    // caret's line reveals its markup, as before.
    QTextDocument *doc = document();
    if (!doc)
        return;
    for (int n = qMin(oldFirst, newFirst); n <= qMax(oldLast, newLast); ++n) {
        const QTextBlock b = doc->findBlockByNumber(n);
        if (!b.isValid())
            continue;
        if (b.userState() == StateMath || b.text().contains(QLatin1Char('$')))
            rehighlightBlock(b);
    }
}

void MarkdownHighlighter::rehighlightAround(int blockNumber) {
    QTextDocument *doc = document();
    if (!doc)
        return;
    QTextBlock b = doc->findBlockByNumber(blockNumber);
    if (!b.isValid())
        return;
    auto mathy = [](const QTextBlock &x) { return x.userState() == StateMath; };
    QTextBlock first, last;
    bool inRegion = false;
    if (mathy(b)) { // opening line or a middle line of a block
        first = last = b;
        while (first.previous().isValid() && mathy(first.previous()))
            first = first.previous();
        while (last.next().isValid() && mathy(last))
            last = last.next(); // ends on the closing line (first non-mathy)
        inRegion = true;
    } else if (b.previous().isValid() && mathy(b.previous())) {
        last = b; // b is the closing line
        first = b.previous();
        while (first.previous().isValid() && mathy(first.previous()))
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

void MarkdownHighlighter::markup(int start, int len, QList<bool> &consumed,
                                 bool reveal) {
    setFormat(start, len, reveal ? m_marker : conceal());
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
        setFormat(contentStart, contentEnd - contentStart, contentFmt);

        const QTextCharFormat markerFmt = reveal ? m_marker : conceal();
        setFormat(start, contentStart - start, markerFmt);
        setFormat(contentEnd, end - contentEnd, markerFmt);

        for (int i = start; i < end; ++i)
            consumed[i] = true;
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
            setFormat(start, 2, m_marker);            // [[
            setFormat(innerStart, inner.size(), m_link);
            setFormat(innerEnd, end - innerEnd, m_marker); // ]]
        } else {
            // Show only the alias (text after '|'); hide the target + brackets.
            const int pipe = inner.indexOf(QLatin1Char('|'));
            const int displayStart =
                pipe >= 0 ? innerStart + pipe + 1 : innerStart;
            setFormat(start, displayStart - start, conceal());
            setFormat(displayStart, innerEnd - displayStart, m_link);
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
        const QTextCharFormat wrap = reveal ? m_marker : conceal();
        setFormat(start, textStart - start, wrap); // "["
        setFormat(textStart, textEnd - textStart, m_link);
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
            setFormat(innerStart, innerEnd - innerStart, m_math);
            setFormat(start, innerStart - start, m_marker);
            setFormat(innerEnd, end - innerEnd, m_marker);
        } else {
            // Off the active line the editor paints the formula over this span
            // (MarkdownEditor::paintEvent). Hide the source but reserve its
            // rendered width via percentage letter-spacing so the surrounding
            // text flows around the painted formula. Glyphs stay full size (just
            // transparent) so the line keeps its normal height.
            const QFont base = document() ? document()->defaultFont() : QFont();
            const QFont f = MathRender::mathFont(base, false);
            const double mathW = MathRender::measure(m.captured(1), f).width();
            const QString src = text.mid(start, end - start);
            const double srcW = QFontMetricsF(f).horizontalAdvance(src);
            QTextCharFormat hide;
            hide.setForeground(QColor(0, 0, 0, 0));
            hide.setFontLetterSpacingType(QFont::PercentageSpacing);
            hide.setFontLetterSpacing(
                srcW > 0 ? qBound(1.0, 100.0 * mathW / srcW, 1000.0) : 100.0);
            setFormat(start, end - start, hide);
        }

        for (int i = start; i < end; ++i)
            consumed[i] = true;
    }
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    // The mascot seed lives on the first line (a hidden HTML comment). When the
    // user reveals and edits it, tint a *valid* seed so they can see it's being
    // interpreted correctly; a malformed line falls through to normal rendering.
    if (currentBlock().blockNumber() == 0 && MascotSeed::fromLine(text) != 0) {
        setFormat(0, text.size(), m_mascot);
        setCurrentBlockState(StateNormal);
        return;
    }

    const bool reveal = currentBlock().blockNumber() == m_activeBlock;
    // Math reveals its raw source not just on the caret's line but on any line
    // the selection covers, so a selected formula shows its source instead of
    // rendering under the highlight.
    const int bn = currentBlock().blockNumber();
    const bool mathReveal = bn >= m_selFirst && bn <= m_selLast;

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
            if (reveal)
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
        if (reveal) {
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
        if (mathReveal) {
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
    setCurrentBlockState(StateNormal);

    QList<bool> consumed(text.size(), false);

    // Headings own the whole line.
    const auto h = m_reHeading.match(text);
    if (h.hasMatch()) {
        const int level = h.capturedLength(1);
        const int contentStart = h.capturedStart(2);

        QTextCharFormat headingFmt = m_heading;
        headingFmt.setFontPointSize(m_baseSize * headingScale(level));
        setFormat(contentStart, h.capturedLength(2), headingFmt);

        if (reveal) {
            QTextCharFormat mk = m_marker;
            mk.setFontPointSize(m_baseSize * headingScale(level));
            setFormat(0, contentStart, mk);
        } else {
            setFormat(0, contentStart, conceal());
        }
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

    // Table row: |-delimited. Monospace cells, dim pipes, bold header row, dim
    // separator. (A lightweight render — no real cell grid in a plain editor.)
    const QString trimmed = text.trimmed();
    if (trimmed.size() > 1 && trimmed.startsWith(QLatin1Char('|')) &&
        trimmed.endsWith(QLatin1Char('|'))) {
        if (m_reTableSep.match(text).hasMatch()) {
            setFormat(0, text.size(), m_tablePipe);
            return;
        }
        const QTextBlock next = currentBlock().next();
        const bool header =
            next.isValid() && m_reTableSep.match(next.text()).hasMatch();
        setFormat(0, text.size(), header ? m_tableHeader : m_table);
        for (int i = 0; i < text.size(); ++i)
            if (text[i] == QLatin1Char('|'))
                setFormat(i, 1, m_tablePipe);
        return;
    }

    // Blockquote: dim the '>' marker, tint the rest, then fall through so
    // inline markup inside the quote still renders.
    const auto q = m_reQuote.match(text);
    if (q.hasMatch()) {
        markup(0, q.capturedLength(1), consumed, reveal);
        setFormat(q.capturedStart(2), q.capturedLength(2), m_quote);
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
            // Hide the "- [ ] " markup but reserve room for the painted box.
            // Conceal the glyphs (dash, brackets, status char) so they stay
            // invisible even under a selection — a full-size transparent glyph
            // would otherwise reappear in the selection colour, leaving a stray
            // "-" inside the rendered square. Spaces carry no glyph, so keep
            // them full width to reserve the box's space.
            QTextCharFormat space;
            space.setForeground(QColor(0, 0, 0, 0)); // full width, no glyph
            const QTextCharFormat hide = conceal();
            for (int i = 0; i < markerEnd && i < text.size(); ++i) {
                const QChar ch = text.at(i);
                const bool blank =
                    ch == QLatin1Char(' ') || ch == QLatin1Char('\t');
                setFormat(i, 1, blank ? space : hide);
            }
        }
        for (int i = 0; i < markerEnd && i < consumed.size(); ++i)
            consumed[i] = true;
        if (done)
            setFormat(task.capturedStart(4), task.capturedLength(4), m_taskDone);
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

    // Inline rules. Order matters: code first (its content must not be
    // re-parsed), then the bold+italic combos before plain bold/italic, then
    // strike, highlight, italics, and finally links.
    applyInline(m_reCode, text, consumed, m_code, reveal);
    // Math before bold/italic so a formula's '_' subscripts and '*' aren't
    // mistaken for emphasis; after code so $ inside `code` stays literal.
    applyMath(text, consumed, mathReveal);
    // Before bold/italic so a URL containing '_' or '*' isn't chewed up by them.
    applyInternetLinks(text, consumed, reveal);
    applyInline(m_reBoldItalic, text, consumed, m_boldItalic, reveal);
    applyInline(m_reBoldUnder, text, consumed, m_boldItalic, reveal);
    applyInline(m_reUnderBold, text, consumed, m_boldItalic, reveal);
    applyInline(m_reBold, text, consumed, m_bold, reveal);
    applyInline(m_reStrike, text, consumed, m_strike, reveal);
    applyInline(m_reHighlight, text, consumed, m_highlight, reveal);
    applyInline(m_reItalicStar, text, consumed, m_italic, reveal);
    applyInline(m_reItalicUnder, text, consumed, m_italic, reveal);
    applyWikiLinks(text, consumed, reveal);
}
