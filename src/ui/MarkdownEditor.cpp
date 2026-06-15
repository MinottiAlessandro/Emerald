#include "MarkdownEditor.h"

#include "MarkdownHighlighter.h"

#include <QMouseEvent>
#include <QRegularExpression>
#include <QTextBlock>

MarkdownEditor::MarkdownEditor(QWidget *parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("editor"));
    setFrameStyle(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    QFont font(QStringLiteral("Inter"), 12);
    font.setStyleHint(QFont::SansSerif);
    setFont(font);
    document()->setDefaultFont(font);
    document()->setDocumentMargin(8);

    m_highlighter = new MarkdownHighlighter(document());

    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        m_highlighter->setActiveBlock(textCursor().blockNumber());
    });
}

QString MarkdownEditor::linkAt(const QPoint &pos) const {
    const QTextCursor cursor = cursorForPosition(pos);
    const int column = cursor.positionInBlock();
    const QString text = cursor.block().text();

    static const QRegularExpression re(QStringLiteral("\\[\\[([^\\[\\]]+)\\]\\]"));
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        if (column >= m.capturedStart(0) && column <= m.capturedEnd(0)) {
            return m.captured(1)
                .section(QLatin1Char('|'), 0, 0)
                .section(QLatin1Char('#'), 0, 0)
                .trimmed();
        }
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

void MarkdownEditor::updateMargins() {
    // Keep a comfortable reading measure: cap text width and center it.
    const int measure = 760;
    const int side = qMax(28, (width() - measure) / 2);
    setViewportMargins(side, 20, side, 20);
}

void MarkdownEditor::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    updateMargins();
}
