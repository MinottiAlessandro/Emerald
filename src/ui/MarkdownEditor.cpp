#include "MarkdownEditor.h"

#include "MarkdownHighlighter.h"
#include "core/WikiLink.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>

namespace {
// A task line: capture(1) = indent, capture(2) = the [ ] / [x] status char.
const QRegularExpression &taskRe() {
    static const QRegularExpression re(
        QStringLiteral("^(\\s*)[-*+]\\s+\\[([ xX])\\]\\s"));
    return re;
}
}

MarkdownEditor::MarkdownEditor(QWidget *parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("editor"));
    setFrameStyle(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    // Allow scrolling past the end so the last line can rise to the top.
    setCenterOnScroll(true);

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
        viewport()->update(); // repaint bullets as the active line moves
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

void MarkdownEditor::applyFont(const QFont &font) {
    setFont(font);
    document()->setDefaultFont(font);
    if (m_highlighter)
        m_highlighter->setBaseSize(font.pointSizeF());
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

bool MarkdownEditor::followsLink(const QPoint &pos,
                                 Qt::KeyboardModifiers mods) const {
    if (linkAt(pos).isEmpty())
        return false;
    if (mods & Qt::ControlModifier)
        return true;
    // A plain click follows the link only when it is rendered, i.e. on a line
    // other than the one the cursor (active line) is on, where the markup is
    // still visible for editing.
    return cursorForPosition(pos).blockNumber() != textCursor().blockNumber();
}

bool MarkdownEditor::toggleTaskAt(const QPoint &pos) {
    const QTextBlock block = cursorForPosition(pos).block();
    if (block.blockNumber() == textCursor().blockNumber())
        return false; // the active line shows raw markup; edit it normally
    const auto m = taskRe().match(block.text());
    if (!m.hasMatch())
        return false;

    // The checkbox is painted over the dash; accept a click on that column.
    QTextCursor at(block);
    at.setPosition(block.position() + m.capturedLength(1)); // dash column
    const QRectF cell = cursorRect(at);
    const qreal cw = QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
    const QRectF hit(cell.left() - cw * 0.5, cell.top(), cw * 2.4, cell.height());
    if (!hit.contains(pos))
        return false;

    const int statusPos = m.capturedStart(2);
    QTextCursor edit(block);
    edit.setPosition(block.position() + statusPos);
    edit.setPosition(block.position() + statusPos + 1, QTextCursor::KeepAnchor);
    const bool done = block.text().at(statusPos).toLower() == QLatin1Char('x');
    edit.insertText(done ? QStringLiteral(" ") : QStringLiteral("x"));
    return true;
}

void MarkdownEditor::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::BackButton) {
        emit navigateBack();
        return;
    }
    if (event->button() == Qt::ForwardButton) {
        emit navigateForward();
        return;
    }
    if (event->button() == Qt::LeftButton && toggleTaskAt(event->pos()))
        return;
    if (event->button() == Qt::LeftButton &&
        followsLink(event->pos(), event->modifiers())) {
        emit linkClicked(linkAt(event->pos()));
        return;
    }
    QPlainTextEdit::mousePressEvent(event);
}

void MarkdownEditor::mouseMoveEvent(QMouseEvent *event) {
    const bool overLink = followsLink(event->pos(), event->modifiers());
    viewport()->setCursor(overLink ? Qt::PointingHandCursor : Qt::IBeamCursor);
    QPlainTextEdit::mouseMoveEvent(event);
}

bool MarkdownEditor::continueList() {
    QTextCursor cur = textCursor();
    // Only continue when the caret is at the end of the line; pressing Enter
    // mid-line just splits it as usual.
    if (cur.hasSelection() || !cur.atBlockEnd())
        return false;

    const QString line = cur.block().text();
    auto endConstruct = [&] { // drop the marker, stay on the now-empty line
        cur.movePosition(QTextCursor::StartOfBlock);
        cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cur.removeSelectedText();
        setTextCursor(cur);
    };

    static const QRegularExpression listRe(QStringLiteral(
        "^(\\s*)(?:([-*+])|(\\d+)([.)]))\\s+(\\[[ xX]\\]\\s+)?(.*)$"));
    if (const auto m = listRe.match(line); m.hasMatch()) {
        if (m.captured(6).isEmpty()) { // empty item ends the list
            endConstruct();
            return true;
        }
        QString prefix = m.captured(1); // indent
        if (!m.captured(2).isEmpty())
            prefix += m.captured(2) + QLatin1Char(' '); // bullet
        else
            prefix += QString::number(m.captured(3).toInt() + 1) +
                      m.captured(4) + QLatin1Char(' '); // next ordinal
        if (!m.captured(5).isEmpty())
            prefix += QStringLiteral("[ ] "); // continued task starts unchecked
        cur.insertText(QStringLiteral("\n") + prefix);
        setTextCursor(cur);
        return true;
    }

    // Blockquotes behave like lists: Enter continues "> ", empty quote ends it.
    static const QRegularExpression quoteRe(
        QStringLiteral("^(\\s*)(>+)\\s?(.*)$"));
    if (const auto q = quoteRe.match(line); q.hasMatch()) {
        if (q.captured(3).isEmpty()) {
            endConstruct();
            return true;
        }
        cur.insertText(QStringLiteral("\n") + q.captured(1) + q.captured(2) +
                       QLatin1Char(' '));
        setTextCursor(cur);
        return true;
    }
    return false;
}

bool MarkdownEditor::adjustListIndent(bool deeper) {
    static const QRegularExpression re(
        QStringLiteral("^(\\s*)(?:[-*+]|\\d+[.)])\\s+"));
    const QTextCursor cur = textCursor();
    if (cur.hasSelection())
        return false;
    const QTextBlock block = cur.block();
    if (!re.match(block.text()).hasMatch())
        return false;

    const int caret = cur.positionInBlock();
    QTextCursor edit(block);
    edit.movePosition(QTextCursor::StartOfBlock);
    int delta = 0;
    if (deeper) {
        edit.insertText(QStringLiteral("  ")); // one level = two spaces
        delta = 2;
    } else {
        const QString line = block.text();
        int n = 0;
        while (n < 2 && n < line.size() && line[n] == QLatin1Char(' '))
            ++n;
        if (n == 0 && !line.isEmpty() && line[0] == QLatin1Char('\t'))
            n = 1; // also accept a leading tab
        if (n > 0) {
            edit.movePosition(QTextCursor::NextCharacter,
                              QTextCursor::KeepAnchor, n);
            edit.removeSelectedText();
            delta = -n;
        }
    }

    QTextCursor out(block);
    out.setPosition(block.position() +
                    qBound(0, caret + delta, block.length() - 1));
    setTextCursor(out);
    return true;
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
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) &&
        continueList()) {
        return;
    }
    if (event->key() == Qt::Key_Tab && adjustListIndent(true))
        return;
    if (event->key() == Qt::Key_Backtab && adjustListIndent(false))
        return;
    QPlainTextEdit::keyPressEvent(event);
    updateCompletionPopup();
}

void MarkdownEditor::paintEvent(QPaintEvent *event) {
    QPlainTextEdit::paintEvent(event);

    // Draw a bullet glyph over each list dash that the highlighter hid (every
    // bullet line except the active one). The glyph varies by nesting level.
    static const QRegularExpression re(
        QStringLiteral("^(\\s*)[-*+]\\s+(?!\\[[ xX]\\]\\s)"));
    const int active = textCursor().blockNumber();
    const QFontMetricsF fm(font());
    const qreal diameter = fm.ascent() * 0.30;

    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    const QColor color(0x78, 0x7c, 0x99);

    for (QTextBlock block = firstVisibleBlock(); block.isValid();
         block = block.next()) {
        const QRectF geo = blockBoundingGeometry(block).translated(contentOffset());
        if (geo.top() > event->rect().bottom())
            break;
        if (geo.bottom() < event->rect().top() || block.blockNumber() == active)
            continue;
        // Task checkbox, drawn over the hidden "- [ ] " markup.
        if (const auto t = taskRe().match(block.text()); t.hasMatch()) {
            QTextCursor curT(block);
            curT.setPosition(block.position() + t.capturedLength(1)); // dash col
            const QRectF cellT = cursorRect(curT);
            const qreal s = fm.ascent() * 0.92;
            const QRectF box(cellT.left(), cellT.center().y() - s / 2.0, s, s);
            const bool checked = t.captured(2).compare(QStringLiteral("x"),
                                                       Qt::CaseInsensitive) == 0;
            const QColor accent(0x7a, 0xa2, 0xf7);
            if (checked) {
                p.setPen(Qt::NoPen);
                p.setBrush(accent);
                p.drawRoundedRect(box, 3, 3);
                QPen tick(QColor(0x1a, 0x1b, 0x26));
                tick.setWidthF(1.6);
                tick.setCapStyle(Qt::RoundCap);
                tick.setJoinStyle(Qt::RoundJoin);
                p.setPen(tick);
                const QPointF pts[3] = {{box.left() + s * 0.24, box.top() + s * 0.52},
                                        {box.left() + s * 0.42, box.top() + s * 0.70},
                                        {box.left() + s * 0.78, box.top() + s * 0.30}};
                p.drawPolyline(pts, 3);
            } else {
                QPen pen(accent);
                pen.setWidthF(1.5);
                p.setPen(pen);
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(box, 3, 3);
            }
            continue;
        }

        // Bullet glyph, drawn over the hidden dash.
        const auto m = re.match(block.text());
        if (!m.hasMatch())
            continue;

        const int markerPos = m.capturedLength(1); // the dash column
        QTextCursor cur(block);
        cur.setPosition(block.position() + markerPos);
        const QRectF cell = cursorRect(cur);
        const qreal cw = fm.horizontalAdvance(block.text().at(markerPos));
        const QPointF c(cell.left() + cw / 2.0, cell.center().y());
        const qreal r = diameter / 2.0;

        switch ((markerPos / 2) % 3) {
        case 0: // filled disc
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(c, r, r);
            break;
        case 1: { // hollow circle
            QPen pen(color);
            pen.setWidthF(1.2);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(c, r, r);
            break;
        }
        default: // filled square
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawRect(QRectF(c.x() - r, c.y() - r, diameter, diameter));
            break;
        }
    }
}
