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
    const bool fenceHere = m_reFence.match(text).hasMatch();
    if (previousBlockState() == StateCode) {
        // Inside a code block: render verbatim, no inline parsing.
        setFormat(0, text.size(), m_codeBlock);
        if (fenceHere) {                       // closing fence
            if (reveal)
                setFormat(0, text.size(), m_marker);
            setCurrentBlockState(StateNormal);
        } else {
            setCurrentBlockState(StateCode);
        }
        return;
    }
    if (fenceHere) {                            // opening fence ```lang
        const auto m = m_reFence.match(text);
        setFormat(0, text.size(), m_codeBlock);
        if (m.capturedLength(2) > 0)           // language tag
            setFormat(m.capturedStart(2), m.capturedLength(2), m_codeLang);
        if (reveal)
            setFormat(0, m.capturedEnd(1), m_marker);
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

    // Horizontal rule: the whole line is the divider.
    if (m_reRule.match(text).hasMatch()) {
        setFormat(0, text.size(), m_rule);
        return;
    }

    // Blockquote: dim the '>' marker, tint the rest, then fall through so
    // inline markup inside the quote still renders.
    const auto q = m_reQuote.match(text);
    if (q.hasMatch()) {
        markup(0, q.capturedLength(1), consumed, reveal);
        setFormat(q.capturedStart(2), q.capturedLength(2), m_quote);
    }

    // Task list: "- [ ] ..." / "- [x] ..."; strike completed items.
    const auto task = m_reTask.match(text);
    if (task.hasMatch()) {
        const bool done = task.captured(2).trimmed().compare(
                              QStringLiteral("x"), Qt::CaseInsensitive) == 0;
        const int markerLen = task.capturedLength(1) + task.capturedLength(2) +
                              task.capturedLength(3);
        setFormat(0, markerLen, m_listMarker);
        for (int i = 0; i < markerLen && i < consumed.size(); ++i)
            consumed[i] = true;
        if (done)
            setFormat(task.capturedStart(4), task.capturedLength(4), m_taskDone);
    } else {
        // Plain bullet / ordered list marker.
        const auto list = m_reList.match(text);
        if (list.hasMatch()) {
            const int s = list.capturedStart(2);
            const int len = list.capturedLength(2);
            setFormat(s, len, m_listMarker);
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
