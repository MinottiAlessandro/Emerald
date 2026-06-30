#pragma once

#include <QList>
#include <QPlainTextEdit>
#include <QRectF>
#include <QStringList>
#include <QTextBlock>
#include <functional>

class MarkdownHighlighter;
class QCompleter;
class QPlainTextDocumentLayout;
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

    // Vertical spacing between rows, as a percent of the font's natural line
    // height (100 = normal). Persisted in settings. Handled by the document
    // layout, so it survives note loads on its own.
    void setLineSpacing(int percent);
    void applyLineSpacing(); // recompute the per-row padding from font + percent

    // Select and scroll to the first occurrence of `text` (case-insensitive).
    void jumpToMatch(const QString &text);

    // The note's mascot seed, stored as a hidden header line at the top of the
    // document (see MascotSeed). 0 when the note has no mascot.
    quint64 mascotSeed() const;
    // The user-creature kind on that header line, or empty (built-in / none).
    QString mascotKind() const;
    // Write/replace (seed != 0) or remove (seed == 0) the mascot header line,
    // carrying an optional user-creature kind.
    void setMascot(quint64 seed, const QString &kind = QString());
    // The body text with any leading mascot header line removed (for hashing /
    // indexing — the header line isn't note content).
    QString bodyText() const;
    // The first document position the caret may land on: just past a hidden
    // mascot header line, or 0 when there isn't one.
    int firstContentPosition() const;

    // Drop all active folds. Call before replacing the document's content
    // (loading/reloading a note, clearing on a vault switch) so the stale fold
    // blocks are never dereferenced by reapplyFolds() during the load.
    void clearFolds();

signals:
    void linkClicked(const QString &target);
    void navigateBack();
    void navigateForward();
    void noticeRequested(const QString &text); // transient feedback (e.g. "Copied")
    // The mascot seed changed — on load, on Generate/Delete, or when the user
    // hand-edits the revealed header line. 0 means the note now has no mascot.
    void mascotSeedChanged(quint64 seed);

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
    // True if `pos` lies within the rendered horizontal span of block columns
    // [startCol, endCol) — keeps a trailing link's click area on its text.
    bool posInColumns(const QPoint &pos, const QTextBlock &block, int startCol,
                      int endCol) const;
    // Should a click at `pos` follow a link? True on Ctrl+click, or a plain
    // click on a rendered link that lives off the active (cursor) line.
    bool followsLink(const QPoint &pos, Qt::KeyboardModifiers mods) const;
    // On Enter in a list item or blockquote, continue it: append a fresh marker
    // at the line end, or split mid-item so the text after the caret moves onto
    // a new marked item. An empty item clears itself (ends the list). Returns
    // true if it handled the key.
    bool continueList();
    // On Tab/Shift+Tab inside a list item, indent/outdent it by one level.
    // Returns true if it handled the key.
    bool adjustListIndent(bool deeper);
    // On Tab/Shift+Tab with a selection spanning multiple lines, indent/outdent
    // every selected line by one level (two spaces). Returns true if handled.
    bool indentSelection(bool deeper);
    // On Tab inside a pipe table, move to the next cell — growing the table
    // (new column/row) at its edges. Shift+Tab (forward=false) just steps back
    // one cell. Returns true if it handled the key.
    bool handleTableTab(bool forward = true);
    // On Enter on a table header that has no separator row yet, insert the
    // `| --- |` separator (and an empty data row if none follows) and move the
    // caret to the first data cell. Returns true if it handled the key.
    bool handleTableHeaderEnter();
    // On Enter on the last row of a table, leave the grid: open an empty line
    // below it. Returns true if it handled the key.
    bool handleTableExitEnter();
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
    // Visit fenced code blocks that intersect `clip` in viewport coordinates.
    void forEachCodeBlock(const QRectF &clip,
                          const std::function<void(const CodeBlock &)> &fn,
                          bool includeCode = false) const;
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
    // Surround the selection with a typed pairing char (* ( _ = [ ' " ` ~).
    // Returns false when `text` isn't such a char or there's no selection.
    bool surroundSelection(const QString &text);
    // Format a selection that spans more than one line: each line's selected
    // segment is wrapped in open..close on its own (full line -> whole line,
    // partial -> just the selected part). With `toggle`, an already-wrapped
    // run is unwrapped instead. Returns false if the selection is single-line.
    bool wrapSelectionByLine(const QString &open, const QString &close,
                             bool toggle);
    void selectCurrentLine();                  // Ctrl+L
    void moveLines(bool up);                   // Alt+Up / Alt+Down
    void insertLink();                         // Ctrl+K — wrap/insert [text](…)
    void setHeadingLevel(int level);           // Ctrl+1…6 — set/toggle # heading

    // Heading folding. A heading hides everything below it until the next
    // heading of the same or higher level.
    int headingLevel(const QString &text) const;       // 0 if not a heading
    // Last block a fold of `heading` hides (section minus trailing blank lines);
    // invalid if the heading has no foldable content below it.
    QTextBlock foldSectionEnd(const QTextBlock &heading) const;
    bool headingFoldable(const QTextBlock &heading) const;
    bool isFolded(const QTextBlock &heading) const;
    // Index of the fold collapsing `heading` in m_folds, or -1.
    int foldIndexOf(const QTextBlock &heading) const;
    void toggleFoldAt(const QTextBlock &heading);
    void reapplyFolds(); // recompute block visibility from the folded set

    // Re-align the pipe table containing the given block (pad cells to equal
    // column widths) so the monospace render reads as a real grid. Run when the
    // caret leaves the table.
    void prettifyTableAt(int blockNumber);

    // Mascot header line (block 0). mascotBlock() is the first block when it is
    // a header line, else invalid; updateMascotLineState() keeps it hidden
    // unless the caret rests on it and emits mascotSeedChanged on any change.
    QTextBlock mascotBlock() const;
    void updateMascotLineState();

    // Completion: the partial title typed after the nearest unclosed "[[" on
    // the current line, or empty with *inContext=false when not inside a link.
    QString wikiContextPrefix(bool *inContext) const;
    void updateCompletionPopup();
    void insertCompletion(const QString &completion);

    MarkdownHighlighter *m_highlighter = nullptr;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_completionModel = nullptr;

    // A collapsed section: the heading plus the last block it hides. The end is
    // captured when the fold happens and then held fixed, so editing the visible
    // trailing blank lines below it doesn't pull more text into the fold — the
    // extent only changes when the section is folded again.
    struct Fold {
        QTextBlock heading;
        QTextBlock end;
    };
    QList<Fold> m_folds;          // collapsed sections
    bool m_applyingFolds = false; // guard against re-entrant relayout

    int m_lastCursorBlock = 0;   // to detect leaving a table
    bool m_prettifying = false;  // guard against re-entrant table reformatting
    bool m_adjustingScroll = false; // guard the over-scroll range extension
    int m_lineSpacing = 100;     // row spacing, percent of natural line height
    quint64 m_mascotSeed = 0;    // last seen mascot seed, to detect changes
    QString m_mascotKind;        // last seen kind, so a kind-only change emits too
    // The spacing-aware document layout (owned by the document). Held as the
    // base type; applyLineSpacing() downcasts to set the per-row padding.
    QPlainTextDocumentLayout *m_spacedLayout = nullptr;
};
