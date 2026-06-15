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

    m_code.setForeground(QColor("#7dcfff"));
    m_code.setFontFamilies({QStringLiteral("monospace")});
    m_code.setBackground(QColor("#24283b"));

    m_strike.setForeground(QColor("#737aa2"));
    m_strike.setFontStrikeOut(true);

    m_link.setForeground(QColor("#7aa2f7"));
    m_link.setFontUnderline(true);

    m_marker.setForeground(QColor("#565f89"));

    m_reHeading    = QRegularExpression(QStringLiteral("^(#{1,6})\\s+(.+)$"));
    m_reCode       = QRegularExpression(QStringLiteral("`([^`]+)`"));
    m_reBold       = QRegularExpression(QStringLiteral("\\*\\*([^*]+)\\*\\*"));
    m_reStrike     = QRegularExpression(QStringLiteral("~~([^~]+)~~"));
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

void MarkdownHighlighter::highlightBlock(const QString &text) {
    const bool reveal = currentBlock().blockNumber() == m_activeBlock;
    QList<bool> consumed(text.size(), false);

    // Headings own the whole line, so handle them first and stop.
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

    // Order matters: code first (its content must not be re-parsed), then
    // bold (** before *), strikethrough, italics, and finally links.
    applyInline(m_reCode, text, consumed, m_code, reveal);
    applyInline(m_reBold, text, consumed, m_bold, reveal);
    applyInline(m_reStrike, text, consumed, m_strike, reveal);
    applyInline(m_reItalicStar, text, consumed, m_italic, reveal);
    applyInline(m_reItalicUnder, text, consumed, m_italic, reveal);
    applyInline(WikiLink::pattern(), text, consumed, m_link, reveal);
}
