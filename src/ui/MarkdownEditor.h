#pragma once

#include "core/MarkdownImage.h"

#include <QList>
#include <QHash>
#include <QImage>
#include <QRectF>
#include <QStringList>
#include <QTextEdit>
#include <QTextBlock>
#include <functional>

class MarkdownHighlighter;
class MarkdownReadObjectRenderer;
class SpellChecker;
class QCompleter;
class QFocusEvent;
class QMimeData;
class QPainter;
class QTextDocument;
class QStringListModel;
class QTimer;
class QVariantAnimation;
class QWheelEvent;

// The writing surface: a plain-text editor wired to the live-preview
// highlighter, with a centered reading measure, Ctrl-click navigation for
// [[wiki-links]], and a completion popup that fires while typing inside [[ ]].
class MarkdownEditor : public QTextEdit {
    Q_OBJECT
public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

    // Replace the Markdown source and finish all presentation-only paragraph
    // formatting before exposing the new document to undo/redo. QTextEdit's
    // reset clears source history, so visual margins must not reintroduce it.
    void setPlainText(const QString &text);
    void clear();
    QString toPlainText() const;
    void undo();
    void redo();
    void copy();

    // The Markdown document remains authoritative while Read Mode displays a
    // separate presentation document. MainWindow uses these accessors for
    // saving, modified state, and caret restoration across note changes.
    QTextDocument *sourceDocument() const { return m_sourceDocument; }
    QTextCursor sourceTextCursor() const;
    void setSourceTextCursor(const QTextCursor &cursor);

    // The note titles offered by [[ autocomplete. Call when the vault changes.
    void setCompletions(const QStringList &titles);

    // Set the editor body font (family + size) and keep heading scaling in sync.
    void applyFont(const QFont &font);

    // Rebuild cached Markdown formats and repaint custom elements after the
    // application theme changes.
    void applyTheme();

    // Folder used to resolve relative Markdown image paths, plus the vault root
    // that every resolved inline preview must remain inside.
    void setImagePaths(const QString &basePath, const QString &vaultRoot);

    // Vertical spacing between rows, as a percent of the font's natural line
    // height (100 = normal). Persisted in settings. Handled by the document
    // layout, so it survives note loads on its own.
    void setLineSpacing(int percent);
    int lineSpacing() const { return m_lineSpacing; }
    void applyLineSpacing(); // recompute the per-row padding from font + percent

    // Search helpers used by vault search and Find in Note. Successful matches
    // are selected and vertically centred after the document layout settles.
    void jumpToMatch(const QString &text);
    bool findAndCenter(const QString &text,
                       QTextDocument::FindFlags flags = {});
    void centerCursor();

    // Reading presentation: swap the Markdown source for a separate,
    // syntax-free document, prevent text edits, and hide the caret. Task
    // checkboxes remain explicitly interactive. Plain Up/Down scroll the
    // viewport instead of moving the hidden text cursor.
    void setReadMode(bool enabled);
    bool readMode() const { return m_readMode; }
    bool smoothScrollActive() const;
    int smoothScrollTarget() const { return m_smoothScrollTarget; }

    // The note's mascot seed, stored as a hidden header line at the top of the
    // document (see MascotSeed). 0 when the note has no mascot.
    quint64 mascotSeed() const;
    // The user-creature kind on that header line, or empty (built-in / none).
    QString mascotKind() const;
    // Write/replace (seed != 0) or remove (seed == 0) the mascot header line,
    // carrying an optional user-creature kind.
    void setMascot(quint64 seed, const QString &kind = QString());
    // The author-visible body text with the mascot header and ordinary HTML
    // comments removed (for hashing / indexing — comments are not note content).
    QString bodyText() const;
    // The first document position the caret may land on: just past a hidden
    // mascot header line, or 0 when there isn't one.
    int firstContentPosition() const;

    // Drop all active folds. Call before replacing the document's content
    // (loading/reloading a note, clearing on a vault switch) so the stale fold
    // blocks are never dereferenced by reapplyFolds() during the load.
    void clearFolds();

    // An application-level Alt chord has taken ownership of the keyboard.
    // Cancel both pending and visible Quick Jump state immediately so link
    // hints cannot remain behind the owning overlay.
    void suppressQuickJump();

    // Incremental Hunspell integration. Multiple selected dictionaries are
    // checked as one stack; the dictionary files remain outside every vault.
    void setSpellCheckingEnabled(bool enabled);
    bool spellCheckingEnabled() const;
    bool setSpellCheckingLanguages(const QStringList &locales,
                                   QString *error = nullptr);
    QStringList spellCheckingLanguages() const;
    // Singular helpers retained for focused callers and compatibility.
    bool setSpellCheckingLanguage(const QString &locale,
                                  QString *error = nullptr);
    QString spellCheckingLanguage() const;
    void setSpellCheckingOptions(bool ignoreWordsWithNumbers,
                                 bool ignoreAllCaps);
    QString misspelledWordAt(const QPoint &viewportPosition) const;
    QStringList spellingSuggestions(const QString &word) const;
    bool replaceMisspelledWordAt(const QPoint &viewportPosition,
                                 const QString &expectedWord,
                                 const QString &replacement);
    bool addToPersonalDictionary(const QString &word,
                                 QString *error = nullptr);
    void ignoreSpellingForSession(const QString &word);

signals:
    void linkClicked(const QString &target);
    void navigateBack();
    void navigateForward();
    void noticeRequested(const QString &text); // transient feedback (e.g. "Copied")
    // A pasted or dropped image should be attached by MainWindow, which knows
    // the active vault and note path.
    void imageFilesInserted(const QStringList &paths);
    void imagePasted(const QImage &image);
    // A permitted Read Mode interaction changed the hidden Markdown source
    // (a task checkbox or Ctrl+Shift+H highlight). MainWindow uses this to
    // autosave even though ordinary Read Mode editing remains disabled.
    void sourceChanged();
    // The mascot seed changed — on load, on Generate/Delete, or when the user
    // hand-edits the revealed header line. 0 means the note now has no mascot.
    void mascotSeedChanged(quint64 seed);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    // Draws real bullet glyphs over the (hidden) dash of list items.
    void paintEvent(QPaintEvent *event) override;
    // Keeps the top-visible line pinned when a width change rewraps the text,
    // so resizing the sidebar doesn't make the view jump (see over-scroll).
    void resizeEvent(QResizeEvent *event) override;

private:
    struct ScrollAnchor {
        int sourcePosition = -1;
        qreal viewportOffset = 0.0;
        qreal fallbackRatio = 0.0;
    };

    void updateActiveHighlight();
    QTextDocument *createReadDocument();
    void rebuildReadDocument(qreal scrollRatio = -1.0);
    void syncSourceCursorFromReadSelection();
    QString readSelectionText(const QTextCursor &selection) const;
    void copyReadSelection();
    // Ctrl+Shift+H in Read Mode edits the authoritative Markdown selection:
    // remove == only when every selected word is highlighted; otherwise fill
    // every missing portion. Returns false when there is no eligible text.
    bool toggleReadHighlight();
    ScrollAnchor captureScrollAnchor() const;
    void restoreScrollAnchor(const ScrollAnchor &anchor);
    qreal currentScrollRatio() const;
    void restoreScrollRatio(qreal ratio);
    QTextCharFormat readObjectFormat(const QTextBlock &block) const;
    QRectF readObjectRect(const QTextBlock &block) const;
    QTextBlock readCheckboxBlockAt(const QPoint &pos) const;
    bool toggleReadCheckboxAt(const QPoint &pos);
    bool isOverReadCodeCopyButton(const QPoint &pos) const;
    bool copyReadCodeBlockAt(const QPoint &pos);
    QTextBlock firstVisibleTextBlock() const;
    QRectF blockViewportRect(const QTextBlock &block) const;
    QList<QRectF> textRangeViewportRects(const QTextBlock &block, int start,
                                         int length) const;
    MarkdownImage::Image imageForBlock(const QTextBlock &block) const;
    QString resolvedImagePath(const QTextBlock &block) const;
    QSize imageSourceSize(const QString &path) const;
    QSizeF imagePreviewSize(const QTextBlock &block) const;
    qreal imagePreviewContentHeight(const QTextBlock &block) const;
    QRectF imagePreviewArea(const QTextBlock &block) const;
    void applyImagePreviewFormats();
    void applyVisualBlockFormats(int position = 0, int charsChanged = -1);
    void scheduleVisualBlockFormats(int position, int charsChanged,
                                    bool preserveModification = false);
    bool updateImageReferences();
    void smoothScrollBy(qreal pixels, int durationMs = 140);
    void stopSmoothScroll();
    void watchScrollPastEnd(QTextDocument *watchedDocument);
    void applyScrollPastEndRange(int naturalMaximum);
    void scheduleScrollPastEndRangeUpdate();
    void updateScrollPastEndRange();
    QString linkAt(const QPoint &pos) const;
    // The URL of the [text](url) link under `pos`, or empty. Distinct from
    // linkAt (wiki note targets): these open in the system browser.
    QString internetLinkAt(const QPoint &pos) const;
    // True if `pos` lies within any rendered visual-line segment occupied by
    // block columns [startCol, endCol). Works for wrapped inline ranges.
    bool pointInTextRange(const QPoint &pos, const QTextBlock &block,
                          int startCol, int endCol) const;
    // Should a click at `pos` follow a link? True on Ctrl+click, or a plain
    // click on a rendered link that lives off the active (cursor) line.
    bool followsLink(const QPoint &pos, Qt::KeyboardModifiers mods) const;

    // Keyboard-only link navigation. Holding Alt briefly labels every visible
    // link; while Alt remains held, typing its QWERTY-ordered hint opens it.
    enum class QuickJumpKind { Wiki, External };
    struct QuickJumpTarget {
        QString hint;
        QString destination;
        QRectF linkRect;
        QRectF badgeRect;
        QuickJumpKind kind = QuickJumpKind::Wiki;
    };
    void armQuickJump();
    void activateQuickJump();
    void cancelQuickJump();
    bool handleQuickJumpKey(QKeyEvent *event);
    void refreshQuickJumpTargets();
    void openQuickJumpTarget(const QuickJumpTarget &target);
    void drawQuickJumpOverlay(QPainter &painter);
    QRectF visibleLinkRect(const QTextBlock &block, int startCol,
                           int endCol) const;
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
    // On Enter anywhere in a table header that has no separator row yet, insert
    // the `| --- |` separator (and an empty data row if none follows), then move
    // to the first cell in the first data row. Returns true if handled.
    bool handleTableHeaderEnter();
    // On Enter in an established pipe table, header/separator rows move to the
    // first data cell; body rows preserve their column, appending when needed.
    // Returns true if handled.
    bool handleTableCellEnter();
    // Place the caret in cell `cellIdx` of a table row, selecting its content.
    void moveToTableCell(const QTextBlock &block, int cellIdx);
    // The rendered task line whose checkbox sits under `pos`, or an invalid
    // block. Used both to toggle (on click) and to show a pointer cursor (on
    // hover). The active line is excluded — it shows raw markup.
    QTextBlock taskCheckboxBlockAt(const QPoint &pos) const;
    QRectF taskCheckboxRect(const QTextBlock &block) const;
    // If pos hits the checkbox of a rendered task line, toggle [ ]<->[x] and
    // return true.
    bool toggleTaskAt(const QPoint &pos);
    // Is `pos` over a foldable heading/list item's fold control?
    bool isOverFoldControl(const QPoint &pos) const;
    QRectF foldControlRect(const QTextBlock &block) const;
    QRectF listMarkerRect(const QTextBlock &block) const;
    void drawFoldControls(QPainter &painter, const QRect &clip) const;
    // Paint one continuous quote/callout panel behind consecutive rows. Shared
    // by Edit and Read modes so fractional block geometry cannot expose seams.
    void drawQuotePanels(QPainter &painter, const QRect &clip,
                         bool drawRails = true) const;

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

    // Structural folding. A heading hides everything below it until the next
    // heading of the same or higher level. A list item hides the immediately
    // following run of more deeply-indented list items.
    int headingLevel(const QString &text) const;       // 0 if not a heading
    // Last block a fold of `heading` hides (section minus trailing blank lines);
    // invalid if the heading has no foldable content below it.
    QTextBlock foldSectionEnd(const QTextBlock &heading) const;
    bool headingFoldable(const QTextBlock &heading) const;
    QTextBlock listSubtreeEnd(const QTextBlock &item) const;
    bool listItemFoldable(const QTextBlock &item) const;
    bool foldAnchorFoldable(const QTextBlock &block) const;
    QTextBlock sourceBlockForDisplay(const QTextBlock &block) const;
    bool isFolded(const QTextBlock &heading) const;
    // Index of the fold collapsing `heading` in m_folds, or -1.
    int foldIndexOf(const QTextBlock &heading) const;
    void toggleFoldAt(const QTextBlock &heading);
    void reapplyFolds(); // recompute block visibility from the folded set

    // Re-align the pipe table containing the given block (pad cells to equal
    // column widths). If a resulting row would be wider than the editor's text
    // area, leave the entire table untouched. Run when the caret leaves it.
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
    void dismissCompletionIfOutOfContext();
    void insertCompletion(const QString &completion);

    MarkdownHighlighter *m_highlighter = nullptr;
    SpellChecker *m_spellChecker = nullptr;
    QObject *m_documentOwner = nullptr;
    QTextDocument *m_sourceDocument = nullptr;
    QTextDocument *m_readDocument = nullptr;
    MarkdownReadObjectRenderer *m_readObjectRenderer = nullptr;
    QTextCursor m_sourceCursor;
    bool m_switchingDocuments = false;
    bool m_readResizeQueued = false;
    bool m_readCursorChanged = false;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_completionModel = nullptr;

    // A collapsed source section: its heading/list item plus the last source
    // block it hides. Source handles remain stable while Read Mode swaps in a
    // separate rendered document. The end is captured when the fold happens,
    // so visible trailing edits do not unexpectedly grow an existing fold.
    struct Fold {
        enum class Kind { Heading, List };
        QTextBlock anchor;
        QTextBlock end;
        Kind kind = Kind::Heading;
    };
    QList<Fold> m_folds;          // collapsed sections
    bool m_applyingFolds = false; // guard against re-entrant relayout

    int m_lastCursorBlock = 0;   // to detect leaving a table
    int m_visualSelectionFirst = 0;
    int m_visualSelectionLast = 0;
    bool m_prettifying = false;  // guard against re-entrant table reformatting
    bool m_adjustingScroll = false; // guard the over-scroll range extension
    bool m_scrollRangeUpdateQueued = false;
    bool m_applyingVisualBlockFormats = false;
    bool m_visualFormatQueued = false;
    bool m_pendingVisualPreserveModification = false;
    int m_pendingVisualFormatStart = -1;
    int m_pendingVisualFormatEnd = -1;
    bool m_readMode = false;        // rendered document; task toggles allowed
    bool m_mouseSelectionDrag = false; // retain image geometry until release
    int m_editCursorWidth = 1;      // restored when leaving Read Mode
    quint64 m_scrollRestoreGeneration = 0; // cancels stale deferred anchors
    int m_lineSpacing = 100;     // row spacing, percent of natural line height
    quint64 m_mascotSeed = 0;    // last seen mascot seed, to detect changes
    QString m_mascotKind;        // last seen kind, so a kind-only change emits too
    QString m_imageBasePath;     // current note folder for relative image links
    QString m_imageRootPath;     // vault boundary for local image previews
    MarkdownImage::References m_imageReferences;
    mutable QHash<QString, QSize> m_imageSizeCache;
    mutable QStringList m_imageSizeCacheOrder;
    QTimer *m_quickJumpTimer = nullptr;
    QList<QuickJumpTarget> m_quickJumpTargets;
    QString m_quickJumpPrefix;
    bool m_quickJumpAltHeld = false;
    bool m_quickJumpArmed = false;
    bool m_quickJumpActive = false;
    QVariantAnimation *m_smoothScroll = nullptr;
    int m_smoothScrollTarget = 0;
    bool m_settingAnimatedScrollValue = false;
};
