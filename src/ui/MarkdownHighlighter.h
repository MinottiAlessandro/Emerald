#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

// Inline live-preview highlighter.
//
// Headings, bold, italic, code, strikethrough, ==highlight==, quotes, task
// lists, fenced code blocks and [[wiki|links]] are rendered with real text
// formats. The syntax markers themselves (#, **, `, [[ ]] ...) are collapsed
// to near-zero width and made transparent on every line EXCEPT the one the
// cursor is on, where they reappear so editing stays natural. That is what
// produces the Obsidian-style "the markup melts away as you type" feel.
class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit MarkdownHighlighter(QTextDocument *document);

    // The block (paragraph) currently holding the cursor. Markers in this
    // block are shown; markers elsewhere are concealed.
    void setActiveBlock(int blockNumber);

    // The base point size headings scale from; call when the editor font size
    // changes so heading sizes track it.
    void setBaseSize(double pt);

protected:
    void highlightBlock(const QString &text) override;

private:
    enum BlockState { StateNormal = 0, StateCode = 1 };

    QTextCharFormat conceal() const; // tiny + transparent
    void applyInline(const QRegularExpression &re, const QString &text,
                     QList<bool> &consumed, const QTextCharFormat &contentFmt,
                     bool reveal);
    // Wiki links need bespoke handling so [[Note|alias]] hides "Note|" and
    // shows only "alias" when the cursor is elsewhere.
    void applyWikiLinks(const QString &text, QList<bool> &consumed, bool reveal);
    // Dim a marker off the active line, reveal it (dimmed) on it. Marks the
    // span consumed either way.
    void markup(int start, int len, QList<bool> &consumed, bool reveal);

    int m_activeBlock = 0;
    double m_baseSize = 12.0;

    QTextCharFormat m_heading;
    QTextCharFormat m_bold;
    QTextCharFormat m_italic;
    QTextCharFormat m_boldItalic;
    QTextCharFormat m_code;
    QTextCharFormat m_codeBlock;
    QTextCharFormat m_codeLang;
    QTextCharFormat m_strike;
    QTextCharFormat m_highlight;
    QTextCharFormat m_link;
    QTextCharFormat m_quote;
    QTextCharFormat m_rule;
    QTextCharFormat m_listMarker;
    QTextCharFormat m_taskDone;
    QTextCharFormat m_marker; // dimmed markers, shown on the active line

    QRegularExpression m_reHeading;
    QRegularExpression m_reFence;
    QRegularExpression m_reQuote;
    QRegularExpression m_reRule;
    QRegularExpression m_reTask;
    QRegularExpression m_reList;
    QRegularExpression m_reBoldItalic;   // ***x***
    QRegularExpression m_reBoldUnder;    // **_x_**
    QRegularExpression m_reUnderBold;    // _**x**_
    QRegularExpression m_reBold;
    QRegularExpression m_reItalicStar;
    QRegularExpression m_reItalicUnder;
    QRegularExpression m_reCode;
    QRegularExpression m_reStrike;
    QRegularExpression m_reHighlight;
};
