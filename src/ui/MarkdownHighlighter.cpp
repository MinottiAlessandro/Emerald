#include "MarkdownHighlighter.h"

#include "core/WikiLink.h"
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
} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
    if (document) {
        const double pt = document->defaultFont().pointSizeF();
        if (pt > 0)
            m_baseSize = pt;
    }

    m_heading.setForeground(QColor("#c0caf5"));
    m_heading.setFontWeight(QFont::Bold);

    m_bold.setForeground(QColor("#c0caf5"));
    m_bold.setFontWeight(QFont::Bold);

    m_italic.setForeground(QColor("#c0caf5"));
    m_italic.setFontItalic(true);

    m_boldItalic.setForeground(QColor("#c0caf5"));
    m_boldItalic.setFontWeight(QFont::Bold);
    m_boldItalic.setFontItalic(true);

    m_code.setForeground(QColor("#7dcfff"));
    m_code.setFontFamilies({QStringLiteral("monospace")});
    m_code.setBackground(QColor("#24283b"));

    m_codeBlock.setForeground(QColor("#a9b1d6"));
    m_codeBlock.setFontFamilies({QStringLiteral("monospace")});
    m_codeBlock.setBackground(QColor("#1f2335"));

    m_codeLang.setForeground(QColor("#7dcfff"));
    m_codeLang.setFontFamilies({QStringLiteral("monospace")});
    m_codeLang.setFontItalic(true);
    m_codeLang.setBackground(QColor("#1f2335"));

    m_strike.setForeground(QColor("#737aa2"));
    m_strike.setFontStrikeOut(true);

    m_highlight.setForeground(QColor("#1a1b26"));
    m_highlight.setBackground(QColor("#e0af68"));

    m_link.setForeground(QColor("#7aa2f7"));
    m_link.setFontUnderline(true);

    m_quote.setForeground(QColor("#9aa5ce"));
    m_quote.setFontItalic(true);

    m_rule.setForeground(QColor("#565f89"));

    m_listMarker.setForeground(QColor("#7aa2f7"));
    m_listMarker.setFontWeight(QFont::Bold);

    m_taskDone.setForeground(QColor("#565f89"));
    m_taskDone.setFontStrikeOut(true);

    m_table.setForeground(QColor("#a9b1d6"));
    m_table.setFontFamilies({QStringLiteral("monospace")});

    m_tableHeader = m_table;
    m_tableHeader.setForeground(QColor("#c0caf5"));
    m_tableHeader.setFontWeight(QFont::Bold);

    m_tablePipe = m_table;
    m_tablePipe.setForeground(QColor("#565f89"));

    m_marker.setForeground(QColor("#565f89"));

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
}

void MarkdownHighlighter::setActiveBlock(int blockNumber) {
    if (blockNumber == m_activeBlock)
        return;
    const int previous = m_activeBlock;
    m_activeBlock = blockNumber;
    if (QTextDocument *doc = document()) {
        rehighlightBlock(doc->findBlockByNumber(previous));
        rehighlightBlock(doc->findBlockByNumber(blockNumber));
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
    f.setFontPointSize(0.5);             // collapse glyph advance to ~nothing
    return f;
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

void MarkdownHighlighter::highlightBlock(const QString &text) {
    const bool reveal = currentBlock().blockNumber() == m_activeBlock;

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
            // Keep the dash + its spaces as invisible width for the painted
            // box; collapse "[ ] " to nothing so the label sits right after it.
            QTextCharFormat box;
            box.setForeground(QColor(0, 0, 0, 0)); // transparent, full width
            setFormat(0, bracketOpen, box);
            setFormat(bracketOpen, markerEnd - bracketOpen, conceal());
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
