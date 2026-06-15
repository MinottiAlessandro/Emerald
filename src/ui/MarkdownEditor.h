#pragma once

#include <QPlainTextEdit>

class MarkdownHighlighter;

// The writing surface: a plain-text editor wired to the live-preview
// highlighter, with a centered reading measure and Ctrl-click navigation
// for [[wiki-links]].
class MarkdownEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit MarkdownEditor(QWidget *parent = nullptr);

signals:
    void linkClicked(const QString &target);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Cleaned link target under the given viewport point, or empty.
    QString linkAt(const QPoint &pos) const;
    void updateMargins();

    MarkdownHighlighter *m_highlighter;
};
