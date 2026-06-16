#pragma once

#include <QPlainTextEdit>
#include <QStringList>

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

private:
    QString linkAt(const QPoint &pos) const;
    // Should a click at `pos` follow a link? True on Ctrl+click, or a plain
    // click on a rendered link that lives off the active (cursor) line.
    bool followsLink(const QPoint &pos, Qt::KeyboardModifiers mods) const;
    // On Enter inside a list item, continue the list (or clear an empty item).
    // Returns true if it handled the key.
    bool continueList();
    // On Tab/Shift+Tab inside a list item, indent/outdent it by one level.
    // Returns true if it handled the key.
    bool adjustListIndent(bool deeper);

    // Completion: the partial title typed after the nearest unclosed "[[" on
    // the current line, or empty with *inContext=false when not inside a link.
    QString wikiContextPrefix(bool *inContext) const;
    void updateCompletionPopup();
    void insertCompletion(const QString &completion);

    MarkdownHighlighter *m_highlighter = nullptr;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_completionModel = nullptr;
};
