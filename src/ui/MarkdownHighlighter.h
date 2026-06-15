#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

// Inline live-preview highlighter.
//
// Headings, bold, italic, code, strikethrough and [[links]] are rendered with
// real text formats. The syntax markers themselves (#, **, `, [[ ]] ...) are
// collapsed to near-zero width and made transparent on every line EXCEPT the
// one the cursor is on, where they reappear so editing stays natural. That is
// what produces the Obsidian-style "the markup melts away as you type" feel.
class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit MarkdownHighlighter(QTextDocument *document);

    // The block (paragraph) currently holding the cursor. Markers in this
    // block are shown; markers elsewhere are concealed.
    void setActiveBlock(int blockNumber);

protected:
    void highlightBlock(const QString &text) override;

private:
    QTextCharFormat conceal() const; // tiny + transparent
    void applyInline(const QRegularExpression &re, const QString &text,
                     QList<bool> &consumed, const QTextCharFormat &contentFmt,
                     bool reveal);

    int m_activeBlock = 0;
    double m_baseSize = 12.0;

    QTextCharFormat m_heading;
    QTextCharFormat m_bold;
    QTextCharFormat m_italic;
    QTextCharFormat m_code;
    QTextCharFormat m_strike;
    QTextCharFormat m_link;
    QTextCharFormat m_marker; // dimmed markers, shown on the active line

    QRegularExpression m_reHeading;
    QRegularExpression m_reBold;
    QRegularExpression m_reItalicStar;
    QRegularExpression m_reItalicUnder;
    QRegularExpression m_reCode;
    QRegularExpression m_reStrike;
    QRegularExpression m_reLink;
};
