#pragma once

#include <QPlainTextEdit>
#include <QRectF>
#include <QStringList>
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

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    // Draws real bullet glyphs over the (hidden) dash of list items.
    void paintEvent(QPaintEvent *event) override;

private:
    QString linkAt(const QPoint &pos) const;
    // Should a click at `pos` follow a link? True on Ctrl+click, or a plain
    // click on a rendered link that lives off the active (cursor) line.
    bool followsLink(const QPoint &pos, Qt::KeyboardModifiers mods) const;
    // On Enter at the end of a list item or blockquote, continue it (or clear
    // an empty one). Returns true if it handled the key.
    bool continueList();
    // On Tab/Shift+Tab inside a list item, indent/outdent it by one level.
    // Returns true if it handled the key.
    bool adjustListIndent(bool deeper);
    // If pos hits the checkbox of a rendered task line, toggle [ ]<->[x] and
    // return true. The active line is left alone (it shows raw markup).
    bool toggleTaskAt(const QPoint &pos);

    // Visit each fenced code block: its full-width rounded background box, the
    // copy-button rect at its top-right, and the block's code (inner lines).
    void forEachCodeBlock(
        const std::function<void(const QRectF &box, const QRectF &copyBtn,
                                 const QString &code)> &fn) const;
    // If pos hits a code block's copy button, copy its code and return true.
    bool copyCodeBlockAt(const QPoint &pos);

    // Completion: the partial title typed after the nearest unclosed "[[" on
    // the current line, or empty with *inContext=false when not inside a link.
    QString wikiContextPrefix(bool *inContext) const;
    void updateCompletionPopup();
    void insertCompletion(const QString &completion);

    MarkdownHighlighter *m_highlighter = nullptr;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_completionModel = nullptr;
};
