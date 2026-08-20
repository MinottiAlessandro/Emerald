#pragma once

#include "core/MarkdownImage.h"

#include <QList>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

// Inline live-preview highlighter.
//
// Headings, bold, italic, code, strikethrough, ==highlight==, quotes, task
// lists, fenced code blocks and [[wiki|links]] are rendered with real text
// formats. The syntax markers themselves (#, **, `, [[ ]] ...) are collapsed
// to near-zero width and made transparent on every line EXCEPT the one the
// cursor is on (and any line a selection covers), where they reappear so
// editing stays natural. That is what produces the Obsidian-style "the markup
// melts away as you type" feel.
class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit MarkdownHighlighter(QTextDocument *document);

    // Number of source characters that remain visible after Emerald's inline
    // preview rules are applied. Table alignment uses this instead of the raw
    // QString length so concealed emphasis markers, wiki-link targets, and URL
    // tails do not make a rendered cell wider than its visible contents.
    static int inlinePreviewColumnCount(const QString &text);

    // Source positions of the pipes that actually delimit table cells. Pipes
    // inside inline constructs (notably [[target|alias]]) stay part of the cell.
    static QList<int> tablePipePositions(const QString &text);

    // The block (paragraph) currently holding the cursor (and, when text is
    // selected, the anchor's block — pass it so a math formula spanning the
    // selection reveals its raw source instead of rendering under the
    // highlight). Markers in the caret's block are shown, concealed elsewhere.
    // The editor calls this on cursor moves *and* on content changes — some
    // edits (e.g. Ctrl+Backspace joining two lines) relocate the caret without a
    // cursorPositionChanged. `anchorBlock < 0` means "no selection" (anchor =
    // caret).
    void setActiveBlock(int caretBlock, int anchorBlock = -1,
                        int caretColumn = -1, bool hasSelection = false);

    // The checker is owned by MarkdownEditor. Rehighlighting remains explicit
    // so settings changes can update the whole note once, while ordinary edits
    // retain QSyntaxHighlighter's incremental one-block behavior.
    void setSpellChecker(class SpellChecker *checker);

    // Reference-style images depend on definitions elsewhere in the note.
    // MarkdownEditor refreshes this compact lookup once per source edit so
    // individual block highlights stay incremental.
    void setImageReferences(const MarkdownImage::References &references);

    // The base point size headings scale from; call when the editor font size
    // changes so heading sizes track it.
    void setBaseSize(double pt);

    // Refresh cached text formats after the application palette changes.
    void applyTheme();

    // Read Mode keeps the Markdown source authoritative but hidden. Suspend
    // parsing while notes are replaced there; resuming followed by rehighlight
    // restores the complete live-preview formatting before Edit Mode is shown.
    void setSuspended(bool suspended) { m_suspended = suspended; }

protected:
    void highlightBlock(const QString &text) override;

private:
    // StateMath marks a line that is inside a multi-line $$…$$ block and whose
    // block continues onto the next line (the opening line and any middle
    // lines). The closing line is StateNormal (like a code block's closing
    // fence), so a region is "StateMath… then a normal line". The editor reads
    // these states to paint the formula and to know which lines to leave alone.
    enum BlockState {
        StateNormal = 0,
        StateCode = 1,
        StateMath = 2,
        StateComment = 3,
        StateQuoteBase = 100
    };

    // Inline emphasis that *accumulates*: a character can be bold and italic and
    // struck and highlighted at once (nested markers like ==a ~~b *c **d***~~==).
    // SDone is the completed-task strikethrough, seeded over the task's label so
    // a styled word on a ticked checkbox stays struck through (the style stacks
    // on top instead of overwriting the strike).
    enum InlineStyle {
        SBold = 1,
        SItalic = 2,
        SStrike = 4,
        SHighlight = 8,
        SDone = 16
    };

    struct EmphasisAnalysis {
        QList<int> mask;
        QList<bool> delimiters;
    };

    static EmphasisAnalysis analyzeEmphasis(const QString &text,
                                             const QList<bool> &consumed,
                                             int seedStyle = 0,
                                             int seedStart = 0,
                                             int seedEnd = 0);

    QTextCharFormat conceal() const; // tiny + transparent
    QTextCharFormat inlineFormat(const QTextCharFormat &overlay) const;
    void applyInline(const QRegularExpression &re, const QString &text,
                     QList<bool> &consumed, const QTextCharFormat &contentFmt,
                     bool reveal);
    // Parse **bold**, *italic* / _italic_, ~~strike~~ and ==highlight== as
    // overlappable, nestable spans: build a per-character style mask, then apply
    // a single merged format per run so the styles stack instead of one winning.
    // Markers are concealed off the active line (dimmed on it), like applyInline.
    // `seedStyle` (e.g. SDone) is OR'd into the mask of every char in
    // [seedStart, seedEnd) before parsing, so a pre-existing run style (a done
    // task's strikethrough) stacks with any emphasis inside it.
    void applyEmphasis(const QString &text, QList<bool> &consumed, bool reveal,
                       int seedStyle = 0, int seedStart = 0, int seedEnd = 0);
    // The merged char format for a set of InlineStyle flags (foreground
    // precedence: highlight > strike > bold/italic; background only from
    // highlight; bold/italic/strikeout accumulate).
    QTextCharFormat emphasisFormat(int mask) const;
    // Wiki links need bespoke handling so [[Note|alias]] hides "Note|" and
    // shows only "alias" when the cursor is elsewhere.
    void applyWikiLinks(const QString &text, QList<bool> &consumed, bool reveal);
    // [text](url) inline links: off the active line show only "text" (styled as
    // a link) and hide the "](url)" + brackets; on it dim the markup but keep
    // the raw text editable, like the wiki-link handling above.
    void applyInternetLinks(const QString &text, QList<bool> &consumed,
                            bool reveal);
    // All image dialects share MarkdownImage's parser. Standalone images are
    // handled earlier as block previews; this pass keeps inline image source
    // compact without pretending it is an ordinary clickable link.
    void applyImages(const QString &text, QList<bool> &consumed, bool reveal);
    // Inline math $…$: on the active line dim the $ and tint the raw body so it
    // stays editable; off it, hide the source but reserve the formula's rendered
    // width (the editor paints the formula over the gap in paintEvent).
    void applyMath(const QString &text, QList<bool> &consumed, bool reveal);
    // Per-character mask of the line's struck text: a completed task's label and
    // every ~~…~~ span (ignoring ~~ inside inline `code`). Used to extend the
    // strike onto inline code/math and link labels, which consume their span
    // before emphasis.
    QList<bool> struckMask(const QString &text, int doneStart, int doneEnd) const;
    // After the inline passes, add strikethrough to inline code, link labels,
    // and revealed inline math that fall inside a struck span — so consumed
    // constructs read as struck like the surrounding text.
    void strikeConsumedInline(const QString &text, bool reveal, int doneStart,
                              int doneEnd);
    void applySpelling(const QString &text);
    // Dim a marker off the active line, reveal it (dimmed) on it. Marks the
    // span consumed either way.
    void markup(int start, int len, QList<bool> &consumed, bool reveal);
    // Hide a display-math line and grow its height to fit `body`'s rendered
    // formula, so the editor can paint it in the reserved space. Shared by the
    // single-line $$…$$ branch and the multi-line $$ fence's first body line.
    void reserveDisplayHeight(int len, const QString &body);
    // Hide a standalone image line and reserve a preview-sized block for the
    // editor painter.
    void reserveImageHeight(int len);
    // True when the cursor sits anywhere inside the $$…$$ fenced region that
    // `block` belongs to, so the whole region shows raw source for editing.
    // `openingHere` means `block` is itself the opening fence.
    bool caretInMathRegion(const QTextBlock &block, bool openingHere) const;
    // True when the cursor (or selection) sits anywhere inside the ```…```
    // fenced code region that `block` belongs to, so *both* the opening and the
    // closing fence reveal together while editing inside the block.
    // `openingHere` means `block` is itself the opening fence.
    bool caretInCodeRegion(const QTextBlock &block, bool openingHere) const;
    // Rehighlight every line of the math region containing `blockNumber` (or
    // just that line if it's not in one) so a whole region reveals/conceals as
    // the caret crosses its boundary.
    void rehighlightAround(int blockNumber);

    int m_activeBlock = 0; // block number of the cursor's line
    int m_selFirst = 0;    // first/last block of the selection (== m_activeBlock
    int m_selLast = 0;     // when there's no selection); math reveals if touched
    int m_caretColumn = -1;
    bool m_hasSelection = false;
    double m_baseSize = 12.0;
    bool m_suspended = false;
    class SpellChecker *m_spellChecker = nullptr;
    MarkdownImage::References m_imageReferences;

    QTextCharFormat m_heading;
    QTextCharFormat m_bold;
    QTextCharFormat m_code;
    QTextCharFormat m_codeBlock;
    QTextCharFormat m_codeLang;
    QTextCharFormat m_strike;
    QTextCharFormat m_highlight;
    QTextCharFormat m_link;
    QTextCharFormat m_quote;
    QTextCharFormat m_calloutTitle;
    QTextCharFormat m_rule;
    QTextCharFormat m_listMarker;
    QTextCharFormat m_taskDone;
    QTextCharFormat m_marker; // dimmed markers, shown on the active line
    QTextCharFormat m_mascot; // a recognised mascot seed line (first line only)
    QTextCharFormat m_comment; // visible author-only HTML comment source
    QTextCharFormat m_math;   // inline $…$ formula body
    QTextCharFormat m_image;  // compact inline-image description/path

    QRegularExpression m_reHeading;
    QRegularExpression m_reFence;
    QRegularExpression m_reQuote;
    QRegularExpression m_reRule;
    QRegularExpression m_reTask;
    QRegularExpression m_reList;
    QRegularExpression m_reCode;
    QRegularExpression m_reLink;         // [text](url)
};
