#include "MarkdownEditor.h"

#include "MarkdownHighlighter.h"
#include "core/WikiLink.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>

MarkdownEditor::MarkdownEditor(QWidget *parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("editor"));
    setFrameStyle(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    // Prefer Inter if installed, but fall back to fonts that exist so the first
    // text layout doesn't pay for a failed font lookup.
    QFont font;
    font.setFamilies({QStringLiteral("Inter"), QStringLiteral("Liberation Sans"),
                      QStringLiteral("sans-serif")});
    font.setPointSize(12);
    font.setStyleHint(QFont::SansSerif);
    setFont(font);
    document()->setDefaultFont(font);
    document()->setDocumentMargin(16);

    // Cap the editor to a comfortable reading measure; MainWindow centers it
    // with stretch spacers. Pinning the width means resizing the side panels
    // doesn't reflow or repaint the text (it used to, on every resize event —
    // that was the lag).
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMaximumWidth(820);

    m_highlighter = new MarkdownHighlighter(document());

    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        m_highlighter->setActiveBlock(textCursor().blockNumber());
    });

    m_completionModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completionModel, this);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_completer->popup()->setObjectName(QStringLiteral("completer"));
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated), this,
            &MarkdownEditor::insertCompletion);
}

void MarkdownEditor::setCompletions(const QStringList &titles) {
    m_completionModel->setStringList(titles);
}

void MarkdownEditor::jumpToMatch(const QString &text) {
    if (text.isEmpty())
        return;
    moveCursor(QTextCursor::Start);
    find(text); // selects the match and scrolls it into view, if found
}

QString MarkdownEditor::wikiContextPrefix(bool *inContext) const {
    *inContext = false;
    const QTextCursor cursor = textCursor();
    const QString before = cursor.block().text().left(cursor.positionInBlock());
    const int open = before.lastIndexOf(QStringLiteral("[["));
    if (open < 0)
        return {};
    // An intervening "]]" means the link is already closed before the cursor.
    if (before.mid(open).contains(QStringLiteral("]]")))
        return {};
    *inContext = true;
    return before.mid(open + 2);
}

void MarkdownEditor::updateCompletionPopup() {
    bool inContext = false;
    const QString prefix = wikiContextPrefix(&inContext);
    if (!inContext) {
        m_completer->popup()->hide();
        return;
    }
    if (prefix != m_completer->completionPrefix())
        m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() == 0) {
        m_completer->popup()->hide();
        return;
    }
    m_completer->popup()->setCurrentIndex(
        m_completer->completionModel()->index(0, 0));

    QRect rect = cursorRect();
    rect.translate(viewport()->pos()); // account for the centered margins
    rect.setWidth(m_completer->popup()->sizeHintForColumn(0) +
                  m_completer->popup()->verticalScrollBar()->sizeHint().width());
    m_completer->complete(rect);
}

void MarkdownEditor::insertCompletion(const QString &completion) {
    QTextCursor cursor = textCursor();
    const int prefixLen = m_completer->completionPrefix().length();
    cursor.setPosition(cursor.position() - prefixLen, QTextCursor::KeepAnchor);
    cursor.insertText(completion);
    // Close the link unless the user already typed the brackets.
    if (cursor.block().text().mid(cursor.positionInBlock(), 2) !=
        QStringLiteral("]]"))
        cursor.insertText(QStringLiteral("]]"));
    setTextCursor(cursor);
}

QString MarkdownEditor::linkAt(const QPoint &pos) const {
    const QTextCursor cursor = cursorForPosition(pos);
    const int column = cursor.positionInBlock();
    const QString text = cursor.block().text();

    auto it = WikiLink::pattern().globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        if (column >= m.capturedStart(0) && column <= m.capturedEnd(0))
            return WikiLink::cleanTarget(m.captured(1));
    }
    return {};
}

void MarkdownEditor::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton &&
        (event->modifiers() & Qt::ControlModifier)) {
        const QString target = linkAt(event->pos());
        if (!target.isEmpty()) {
            emit linkClicked(target);
            return;
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}

void MarkdownEditor::mouseMoveEvent(QMouseEvent *event) {
    const bool overLink = (event->modifiers() & Qt::ControlModifier) &&
                          !linkAt(event->pos()).isEmpty();
    viewport()->setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
    QPlainTextEdit::mouseMoveEvent(event);
}

void MarkdownEditor::keyPressEvent(QKeyEvent *event) {
    if (m_completer->popup()->isVisible()) {
        // These keys belong to the popup while it is open.
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            event->ignore();
            return;
        default:
            break;
        }
    }
    QPlainTextEdit::keyPressEvent(event);
    updateCompletionPopup();
}
