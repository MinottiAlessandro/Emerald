#pragma once

#include <QList>
#include <QPlainTextEdit>
#include <QRectF>
#include <QStringList>
#include <QTextBlock>
#include <functional>

class MarkdownHighlighter;
class QCompleter;
class QStringListModel;

// The writing surface: a plain-text editor wired to the live-preview
// highlighter, with a centered reading measure, Ctrl-click navigation for
// [[wiki-links]], and a completion popup that fires while typing inside [[ ]].
class MarkdownEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

    // The note titles offered by [[ autocomplete. Call when the vault changes.
    void setCompletions(const QStringList &titles);

    // Set the editor body font (family + size) and keep heading scaling in sync.
    void applyFont(const QFont &font);

    // Select and scroll to the first occurrence of `text` (case-insensitive).
    void jumpToMatch(const QString &text);

signals:
    void linkClicked(const QString &target);
    void navigateBack();
    void navigateForward();
    void noticeRequested(const QString &text); // transient feedback (e.g. "Copied")
    void deleteNoteRequested(); // Ctrl+Del: delete the open note, not a word

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    // Draws real bullet glyphs over the (hidden) dash of list items.
    void paintEvent(QPaintEvent *event) override;
    // Keeps the top-visible line pinned when a width change rewraps the text,
    // so resizing the sidebar doesn't make the view jump (see over-scroll).
    void resizeEvent(QResizeEvent *event) override;

private:
    QString linkAt(const QPoint &pos) const;
    // The URL of the [text](url) link under `pos`, or empty. Distinct from
    // linkAt (wiki note targets): these open in the system browser.
    QString internetLinkAt(const QPoint &pos) const;
    // Should a click at `pos` follow a link? True on Ctrl+click, or a plain
    // click on a rendered link that lives off the active (cursor) line.
    bool followsLink(const QPoint &pos, Qt::KeyboardModifiers mods) const;
    // On Enter at the end of a list item or blockquote, continue it (or clear
    // an empty one). Returns true if it handled the key.
    bool continueList();
    // On Tab/Shift+Tab inside a list item, indent/outdent it by one level.
    // Returns true if it handled the key.
    bool adjustListIndent(bool deeper);
    // On Tab inside a pipe table, move to the next cell — growing the table
    // (new column/row) at its edges. Shift+Tab (forward=false) just steps back
    // one cell. Returns true if it handled the key.
    bool handleTableTab(bool forward = true);
    // Place the caret in cell `cellIdx` of a table row, selecting its content.
    void moveToTableCell(const QTextBlock &block, int cellIdx);
    // The rendered task line whose checkbox sits under `pos`, or an invalid
    // block. Used both to toggle (on click) and to show a pointer cursor (on
    // hover). The active line is excluded — it shows raw markup.
    QTextBlock taskCheckboxBlockAt(const QPoint &pos) const;
    // If pos hits the checkbox of a rendered task line, toggle [ ]<->[x] and
    // return true.
    bool toggleTaskAt(const QPoint &pos);
    // Is `pos` over a foldable heading's fold control (the left margin)?
    bool isOverFoldControl(const QPoint &pos) const;

    struct CodeBlock {
        QRectF header;   // the top header bar (language tag + copy button)
        QRectF body;     // the code body below the header
        QRectF copyBtn;  // copy-button rect, inside the header on the right
        QString language; // language tag, or "Text"
        QString code;     // the block's inner lines
        bool active = false; // the caret is inside this block (show raw markup)
    };
    // Visit each fenced code block's geometry/content.
    void forEachCodeBlock(const std::function<void(const CodeBlock &)> &fn) const;
    // True for a line *inside* a fenced code block (not a fence itself). Such
    // lines must render verbatim: no bullets, rules, headings or list-continue.
    bool insideCodeBlock(const QTextBlock &block) const;
    // If pos hits a code block's copy button, copy its code and return true.
    bool copyCodeBlockAt(const QPoint &pos);
    // Is `pos` over a (non-active) code block's copy button?
    bool isOverCopyButton(const QPoint &pos) const;

    // Editor keybindings (line ops + inline formatting). Each acts on the
    // selection when there is one, else the current line.
    void wrapSelection(const QString &marker); // Ctrl+B / Ctrl+I
    void selectCurrentLine();                  // Ctrl+L
    void duplicateLineOrSelection();           // Ctrl+D
    void deleteCurrentLine();                  // Ctrl+Shift+K
    void moveLines(bool up);                   // Alt+Up / Alt+Down

    // Heading folding. A heading hides everything below it until the next
    // heading of the same or higher level.
    int headingLevel(const QString &text) const;       // 0 if not a heading
    bool headingFoldable(const QTextBlock &heading) const;
    bool isFolded(const QTextBlock &heading) const;
    void toggleFoldAt(const QTextBlock &heading);
    void reapplyFolds(); // recompute block visibility from the folded set

    // Re-align the pipe table containing the given block (pad cells to equal
    // column widths) so the monospace render reads as a real grid. Run when the
    // caret leaves the table.
    void prettifyTableAt(int blockNumber);

    // Completion: the partial title typed after the nearest unclosed "[[" on
    // the current line, or empty with *inContext=false when not inside a link.
    QString wikiContextPrefix(bool *inContext) const;
    void updateCompletionPopup();
    void insertCompletion(const QString &completion);

    MarkdownHighlighter *m_highlighter = nullptr;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_completionModel = nullptr;

    QList<QTextBlock> m_foldedHeadings; // headings whose section is collapsed
    bool m_applyingFolds = false;       // guard against re-entrant relayout

    int m_lastCursorBlock = 0;   // to detect leaving a table
    bool m_prettifying = false;  // guard against re-entrant table reformatting
};
