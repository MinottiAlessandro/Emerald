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

    // The block (paragraph) currently holding the cursor (and, when text is
    // selected, the anchor's block — pass it so a math formula spanning the
    // selection reveals its raw source instead of rendering under the
    // highlight). Markers in the caret's block are shown, concealed elsewhere.
    // The editor calls this on cursor moves *and* on content changes — some
    // edits (e.g. Ctrl+Backspace joining two lines) relocate the caret without a
    // cursorPositionChanged. `anchorBlock < 0` means "no selection" (anchor =
    // caret).
    void setActiveBlock(int caretBlock, int anchorBlock = -1);

    // The base point size headings scale from; call when the editor font size
    // changes so heading sizes track it.
    void setBaseSize(double pt);

protected:
    void highlightBlock(const QString &text) override;

private:
    // StateMath marks a line that is inside a multi-line $$…$$ block and whose
    // block continues onto the next line (the opening line and any middle
    // lines). The closing line is StateNormal (like a code block's closing
    // fence), so a region is "StateMath… then a normal line". The editor reads
    // these states to paint the formula and to know which lines to leave alone.
    enum BlockState { StateNormal = 0, StateCode = 1, StateMath = 2 };

    QTextCharFormat conceal() const; // tiny + transparent
    void applyInline(const QRegularExpression &re, const QString &text,
                     QList<bool> &consumed, const QTextCharFormat &contentFmt,
                     bool reveal);
    // Wiki links need bespoke handling so [[Note|alias]] hides "Note|" and
    // shows only "alias" when the cursor is elsewhere.
    void applyWikiLinks(const QString &text, QList<bool> &consumed, bool reveal);
    // [text](url) inline links: off the active line show only "text" (styled as
    // a link) and hide the "](url)" + brackets; on it dim the markup but keep
    // the raw text editable, like the wiki-link handling above.
    void applyInternetLinks(const QString &text, QList<bool> &consumed,
                            bool reveal);
    // Inline math $…$: on the active line dim the $ and tint the raw body so it
    // stays editable; off it, hide the source but reserve the formula's rendered
    // width (the editor paints the formula over the gap in paintEvent).
    void applyMath(const QString &text, QList<bool> &consumed, bool reveal);
    // Dim a marker off the active line, reveal it (dimmed) on it. Marks the
    // span consumed either way.
    void markup(int start, int len, QList<bool> &consumed, bool reveal);
    // Hide a display-math line and grow its height to fit `body`'s rendered
    // formula, so the editor can paint it in the reserved space. Shared by the
    // single-line $$…$$ branch and the multi-line $$ fence's first body line.
    void reserveDisplayHeight(int len, const QString &body);
    // True when the cursor sits anywhere inside the $$…$$ fenced region that
    // `block` belongs to, so the whole region shows raw source for editing.
    // `openingHere` means `block` is itself the opening fence.
    bool caretInMathRegion(const QTextBlock &block, bool openingHere) const;
    // Rehighlight every line of the math region containing `blockNumber` (or
    // just that line if it's not in one) so a whole region reveals/conceals as
    // the caret crosses its boundary.
    void rehighlightAround(int blockNumber);

    int m_activeBlock = 0; // block number of the cursor's line
    int m_selFirst = 0;    // first/last block of the selection (== m_activeBlock
    int m_selLast = 0;     // when there's no selection); math reveals if touched
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
    QTextCharFormat m_table;       // monospace cell text
    QTextCharFormat m_tableHeader; // monospace + bold header cells
    QTextCharFormat m_tablePipe;   // dimmed column separators
    QTextCharFormat m_marker; // dimmed markers, shown on the active line
    QTextCharFormat m_mascot; // a recognised mascot seed line (first line only)
    QTextCharFormat m_math;   // inline $…$ formula body

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
    QRegularExpression m_reTableSep;
    QRegularExpression m_reLink;         // [text](url)
};
