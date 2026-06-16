#include "MarkdownEditor.h"

#include "MarkdownHighlighter.h"
#include "core/WikiLink.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextDocumentLayout>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextLayout>
#include <algorithm>

namespace {
// A task line: capture(1) = indent, capture(2) = the [ ] / [x] status char.
const QRegularExpression &taskRe() {
    static const QRegularExpression re(
        QStringLiteral("^(\\s*)[-*+]\\s+\\[([ xX])\\]\\s"));
    return re;
}

// --- pipe-table helpers (for the auto-prettifier) -----------------------
bool isTableRow(const QString &text) {
    const QString t = text.trimmed();
    return t.size() > 1 && t.startsWith(QLatin1Char('|')) &&
           t.endsWith(QLatin1Char('|'));
}
bool isSeparatorRow(const QString &text) {
    static const QRegularExpression re(
        QStringLiteral("^\\s*\\|?[\\s:|-]*-[\\s:|-]*\\|?\\s*$"));
    return re.match(text).hasMatch();
}
QStringList splitRow(const QString &text) {
    QString t = text.trimmed();
    if (t.startsWith(QLatin1Char('|')))
        t.remove(0, 1);
    if (t.endsWith(QLatin1Char('|')))
        t.chop(1);
    QStringList cells = t.split(QLatin1Char('|'));
    for (QString &c : cells)
        c = c.trimmed();
    return cells;
}
int sepAlign(const QString &cell) { // 0 left, 1 right, 2 centre
    const QString t = cell.trimmed();
    const bool l = t.startsWith(QLatin1Char(':'));
    const bool r = t.endsWith(QLatin1Char(':'));
    return (l && r) ? 2 : r ? 1 : 0;
}
QString padCell(const QString &s, int width, int align) {
    const int pad = qMax(0, width - int(s.length()));
    if (align == 1)
        return QString(pad, QLatin1Char(' ')) + s;
    if (align == 2)
        return QString(pad / 2, QLatin1Char(' ')) + s +
               QString(pad - pad / 2, QLatin1Char(' '));
    return s + QString(pad, QLatin1Char(' '));
}
QString dashCell(int width, int align) {
    width = qMax(3, width);
    if (align == 2)
        return QLatin1Char(':') + QString(width - 2, QLatin1Char('-')) +
               QLatin1Char(':');
    if (align == 1)
        return QString(width - 1, QLatin1Char('-')) + QLatin1Char(':');
    return QString(width, QLatin1Char('-'));
}
}

MarkdownEditor::MarkdownEditor(QWidget *parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("editor"));
    setFrameStyle(QFrame::NoFrame);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // text always wraps
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

    // MainWindow caps and centers the editor's column to a comfortable reading
    // measure, so the editor itself just expands to fill it.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_highlighter = new MarkdownHighlighter(document());

    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        m_highlighter->setActiveBlock(textCursor().blockNumber());
        viewport()->update(); // repaint bullets as the active line moves

        const int cur = textCursor().blockNumber();
        if (!m_prettifying && cur != m_lastCursorBlock) {
            const QTextBlock prev = document()->findBlockByNumber(m_lastCursorBlock);
            // If the caret just left a table, align it.
            if (prev.isValid() && isTableRow(prev.text())) {
                int first = m_lastCursorBlock, last = m_lastCursorBlock;
                while (document()->findBlockByNumber(first - 1).isValid() &&
                       isTableRow(document()->findBlockByNumber(first - 1).text()))
                    --first;
                while (document()->findBlockByNumber(last + 1).isValid() &&
                       isTableRow(document()->findBlockByNumber(last + 1).text()))
                    ++last;
                if (cur < first || cur > last)
                    prettifyTableAt(m_lastCursorBlock);
            }
        }
        m_lastCursorBlock = textCursor().blockNumber();
    });
    // Keep folded sections hidden as the document is edited.
    connect(document(), &QTextDocument::contentsChanged, this, [this] {
        if (!m_applyingFolds && !m_foldedHeadings.isEmpty())
            reapplyFolds();
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
    // A click in the left margin next to a heading folds/unfolds its section.
    if (event->button() == Qt::LeftButton &&
        event->pos().x() < document()->documentMargin()) {
        const QTextBlock b = cursorForPosition(event->pos()).block();
        if (headingFoldable(b)) {
            toggleFoldAt(b);
            return;
        }
    }
    if (event->button() == Qt::LeftButton && copyCodeBlockAt(event->pos()))
        return;
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

void MarkdownEditor::forEachCodeBlock(
    const std::function<void(const CodeBlock &)> &fn) const {
    const qreal docMargin = document()->documentMargin();
    const qreal left = docMargin * 0.5;
    const qreal right = viewport()->width() - docMargin * 0.5;
    static const QRegularExpression fenceRe(
        QStringLiteral("^\\s*(?:```|~~~)\\s*(\\S*)"));

    const int caret = textCursor().blockNumber();
    bool inCode = false;
    qreal headerTop = 0, headerBottom = 0;
    int openNum = 0;
    QString lang;
    QStringList code;
    auto emitRegion = [&](qreal bodyBottom, int closeNum) {
        CodeBlock cb;
        cb.header = QRectF(left, headerTop, right - left, headerBottom - headerTop);
        cb.body = QRectF(left, headerBottom, right - left, bodyBottom - headerBottom);
        const qreal s = 16;
        cb.copyBtn = QRectF(cb.header.right() - s - 8,
                            cb.header.center().y() - s / 2, s, s);
        cb.language = lang.isEmpty() ? QStringLiteral("Text") : lang;
        cb.code = code.join(QLatin1Char('\n'));
        // The caret sitting anywhere from the opening to the closing fence means
        // the block is being edited; callers then show raw markup instead of the
        // header bar (which would overlap the now-visible ``` fence).
        cb.active = caret >= openNum && caret <= closeNum;
        fn(cb);
    };

    for (QTextBlock b = document()->firstBlock(); b.isValid(); b = b.next()) {
        const bool isCode = b.userState() == 1; // MarkdownHighlighter::StateCode
        const QRectF geo = blockBoundingGeometry(b).translated(contentOffset());
        if (isCode && !inCode) { // opening fence = the header row
            inCode = true;
            headerTop = geo.top();
            headerBottom = geo.bottom();
            openNum = b.blockNumber();
            const auto m = fenceRe.match(b.text());
            lang = m.hasMatch() ? m.captured(1) : QString();
            code.clear();
        } else if (isCode && inCode) { // inner code line
            code << b.text();
        } else if (!isCode && inCode) { // closing fence
            emitRegion(geo.bottom(), b.blockNumber());
            inCode = false;
        }
    }
    if (inCode)
        emitRegion(blockBoundingGeometry(document()->lastBlock())
                       .translated(contentOffset())
                       .bottom(),
                   document()->lastBlock().blockNumber());
}

bool MarkdownEditor::copyCodeBlockAt(const QPoint &pos) {
    bool copied = false;
    forEachCodeBlock([&](const CodeBlock &cb) {
        if (!copied && !cb.active && cb.copyBtn.contains(pos)) {
            QApplication::clipboard()->setText(cb.code);
            copied = true;
        }
    });
    return copied;
}

int MarkdownEditor::headingLevel(const QString &text) const {
    int n = 0;
    while (n < text.size() && n < 6 && text[n] == QLatin1Char('#'))
        ++n;
    if (n > 0 && n < text.size() && text[n] == QLatin1Char(' '))
        return n;
    return 0;
}

bool MarkdownEditor::headingFoldable(const QTextBlock &heading) const {
    const int level = headingLevel(heading.text());
    if (level == 0)
        return false;
    const QTextBlock next = heading.next();
    if (!next.isValid())
        return false;
    const int l = headingLevel(next.text());
    return !(l > 0 && l <= level); // foldable unless the next line is a peer
}

bool MarkdownEditor::isFolded(const QTextBlock &heading) const {
    for (const QTextBlock &b : m_foldedHeadings)
        if (b == heading)
            return true;
    return false;
}

void MarkdownEditor::toggleFoldAt(const QTextBlock &heading) {
    if (!headingFoldable(heading))
        return;
    int idx = -1;
    for (int i = 0; i < m_foldedHeadings.size(); ++i)
        if (m_foldedHeadings[i] == heading) {
            idx = i;
            break;
        }
    if (idx >= 0) {
        m_foldedHeadings.removeAt(idx);
    } else {
        // Move the caret out of the section that is about to be hidden.
        const int level = headingLevel(heading.text());
        const int caret = textCursor().blockNumber();
        for (QTextBlock b = heading.next(); b.isValid(); b = b.next()) {
            const int l = headingLevel(b.text());
            if (l > 0 && l <= level)
                break;
            if (b.blockNumber() == caret) {
                QTextCursor c(heading);
                c.movePosition(QTextCursor::EndOfBlock);
                setTextCursor(c);
                break;
            }
        }
        m_foldedHeadings.append(heading);
    }
    reapplyFolds();
}

void MarkdownEditor::reapplyFolds() {
    if (m_applyingFolds)
        return;
    m_applyingFolds = true;

    // Forget folds whose heading was edited away or deleted.
    m_foldedHeadings.erase(
        std::remove_if(m_foldedHeadings.begin(), m_foldedHeadings.end(),
                       [this](const QTextBlock &b) {
                           return !b.isValid() || headingLevel(b.text()) == 0;
                       }),
        m_foldedHeadings.end());

    for (QTextBlock b = document()->firstBlock(); b.isValid(); b = b.next())
        if (!b.isVisible())
            b.setVisible(true);

    for (const QTextBlock &h : m_foldedHeadings) {
        const int level = headingLevel(h.text());
        for (QTextBlock b = h.next(); b.isValid(); b = b.next()) {
            const int l = headingLevel(b.text());
            if (l > 0 && l <= level)
                break;
            b.setVisible(false);
        }
    }

    document()->markContentsDirty(0, document()->characterCount());
    m_applyingFolds = false;
    viewport()->update();
}

void MarkdownEditor::prettifyTableAt(int blockNumber) {
    QTextBlock first = document()->findBlockByNumber(blockNumber);
    if (!first.isValid() || !isTableRow(first.text()))
        return;
    while (first.previous().isValid() && isTableRow(first.previous().text()))
        first = first.previous();
    QTextBlock last = first;
    while (last.next().isValid() && isTableRow(last.next().text()))
        last = last.next();

    QList<QStringList> rows;
    QList<bool> sep;
    for (QTextBlock b = first;; b = b.next()) {
        rows << splitRow(b.text());
        sep << isSeparatorRow(b.text());
        if (b == last)
            break;
    }

    int cols = 0;
    for (const QStringList &r : rows)
        cols = qMax(cols, int(r.size()));

    QList<int> width(cols, 3);
    for (int r = 0; r < rows.size(); ++r)
        if (!sep[r])
            for (int c = 0; c < rows[r].size(); ++c)
                width[c] = qMax(width[c], int(rows[r][c].length()));

    QList<int> align(cols, 0);
    for (int r = 0; r < rows.size(); ++r)
        if (sep[r])
            for (int c = 0; c < cols && c < rows[r].size(); ++c)
                align[c] = sepAlign(rows[r][c]);

    QStringList out;
    for (int r = 0; r < rows.size(); ++r) {
        QString line = QStringLiteral("|");
        for (int c = 0; c < cols; ++c) {
            const QString cell =
                sep[r] ? dashCell(width[c], align[c])
                       : padCell(c < rows[r].size() ? rows[r][c] : QString(),
                                 width[c], align[c]);
            line += QLatin1Char(' ') + cell + QStringLiteral(" |");
        }
        out << line;
    }

    QTextCursor cur(first);
    cur.movePosition(QTextCursor::StartOfBlock);
    cur.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    if (cur.selectedText().replace(QChar(0x2029), QLatin1Char('\n')) ==
        out.join(QLatin1Char('\n')))
        return; // already aligned
    m_prettifying = true;
    cur.insertText(out.join(QLatin1Char('\n')));
    m_prettifying = false;
}

void MarkdownEditor::resizeEvent(QResizeEvent *event) {
    // Remember which block tops the viewport before the base relayout. With
    // setCenterOnScroll the document can scroll past its end, and a width
    // change rewraps every line; left alone, the preserved scrollbar value
    // would point at unrelated content and the view would lurch (most
    // noticeably near the end of the file).
    const int anchor = firstVisibleBlock().blockNumber();

    QPlainTextEdit::resizeEvent(event);

    auto *layout =
        qobject_cast<QPlainTextDocumentLayout *>(document()->documentLayout());
    if (!layout || anchor <= 0)
        return;

    // The vertical scrollbar counts wrapped lines, so pinning the anchor back
    // to the top means summing the wrapped lines above it. QPlainTextEdit lays
    // out blocks lazily; force each block's layout first or an untouched block
    // reports zero lines and we undercount (which scrolls to the very top).
    int line = 0;
    for (QTextBlock b = document()->firstBlock();
         b.isValid() && b.blockNumber() < anchor; b = b.next()) {
        if (!b.isVisible())
            continue;
        layout->blockBoundingRect(b); // forces layout of this block
        line += b.layout()->lineCount();
    }
    verticalScrollBar()->setValue(line);
}

void MarkdownEditor::paintEvent(QPaintEvent *event) {
    // Code-block backgrounds go behind the text: a rounded body with a thin
    // header bar (rounded top corners) in a lighter complementary colour.
    {
        QPainter bg(viewport());
        bg.setRenderHint(QPainter::Antialiasing);
        bg.setPen(Qt::NoPen);
        forEachCodeBlock([&](const CodeBlock &cb) {
            const QRectF full(cb.header.left(), cb.header.top(), cb.header.width(),
                              cb.body.bottom() - cb.header.top());
            if (!full.intersects(event->rect()))
                return;
            bg.setBrush(QColor(0x13, 0x20, 0x1a));
            bg.drawRoundedRect(full, 6, 6);
            if (cb.active) // editing: show the raw ``` fence, no header bar
                return;
            const qreal r = 6;
            const QRectF h = cb.header;
            QPainterPath path;
            path.moveTo(h.left(), h.bottom());
            path.lineTo(h.left(), h.top() + r);
            path.quadTo(h.left(), h.top(), h.left() + r, h.top());
            path.lineTo(h.right() - r, h.top());
            path.quadTo(h.right(), h.top(), h.right(), h.top() + r);
            path.lineTo(h.right(), h.bottom());
            path.closeSubpath();
            bg.fillPath(path, QColor(0x1d, 0x3a, 0x2a));
        });
    }

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
    const QColor color(0x6d, 0x8e, 0x7c);

    for (QTextBlock block = firstVisibleBlock(); block.isValid();
         block = block.next()) {
        const QRectF geo = blockBoundingGeometry(block).translated(contentOffset());
        if (geo.top() > event->rect().bottom())
            break;
        if (geo.bottom() < event->rect().top() || block.blockNumber() == active)
            continue;

        // Horizontal rule: a full-width line across the (hidden) dashes.
        static const QRegularExpression ruleRe(
            QStringLiteral("^\\s*([-*_])\\s*(?:\\1\\s*){2,}$"));
        if (ruleRe.match(block.text()).hasMatch()) {
            const qreal margin = document()->documentMargin();
            const qreal y = geo.center().y();
            QPen pen(QColor(0x2f, 0x4a, 0x3b));
            pen.setWidthF(1.4);
            p.setPen(pen);
            p.drawLine(QPointF(margin, y),
                       QPointF(viewport()->width() - margin, y));
            continue;
        }

        // Task checkbox, drawn over the hidden "- [ ] " markup.
        if (const auto t = taskRe().match(block.text()); t.hasMatch()) {
            QTextCursor curT(block);
            curT.setPosition(block.position() + t.capturedLength(1)); // dash col
            const QRectF cellT = cursorRect(curT);
            const qreal s = fm.ascent() * 0.92;
            const QRectF box(cellT.left(), cellT.center().y() - s / 2.0, s, s);
            const bool checked = t.captured(2).compare(QStringLiteral("x"),
                                                       Qt::CaseInsensitive) == 0;
            const QColor accent(0x33, 0xd6, 0x85);
            if (checked) {
                p.setPen(Qt::NoPen);
                p.setBrush(accent);
                p.drawRoundedRect(box, 3, 3);
                QPen tick(QColor(0x10, 0x18, 0x14));
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

    // Code-block header content: language label on the left, copy button right.
    forEachCodeBlock([&](const CodeBlock &cb) {
        if (cb.active || !cb.header.intersects(event->rect()))
            return; // while editing, the raw fence shows instead
        QFont lf = font();
        lf.setPointSizeF(font().pointSizeF() * 0.85);
        p.setFont(lf);
        p.setPen(QColor(0x7e, 0xe0, 0xb0));
        p.drawText(QRectF(cb.header.left() + 12, cb.header.top(),
                          cb.header.width() - 44, cb.header.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, cb.language);
        p.setFont(font());

        const QRectF btn = cb.copyBtn;
        QPen pen(QColor(0x92, 0xb3, 0xa2));
        pen.setWidthF(1.3);
        p.setPen(pen);
        p.setBrush(QColor(0x1d, 0x3a, 0x2a));
        p.drawRoundedRect(QRectF(btn.left() + 5, btn.top() + 2, 8, 10), 2, 2);
        p.drawRoundedRect(QRectF(btn.left() + 2, btn.top() + 4, 8, 10), 2, 2);
    });

    // Fold arrows in the left margin next to foldable headings.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x4f, 0x75, 0x65));
    for (QTextBlock block = firstVisibleBlock(); block.isValid();
         block = block.next()) {
        const QRectF geo = blockBoundingGeometry(block).translated(contentOffset());
        if (geo.top() > event->rect().bottom())
            break;
        if (geo.bottom() < event->rect().top() || !headingFoldable(block))
            continue;
        QTextCursor hc(block);
        const qreal y = cursorRect(hc).center().y();
        const qreal x = 5;
        QPointF tri[3];
        if (isFolded(block)) { // ▸ collapsed
            tri[0] = {x, y - 4};
            tri[1] = {x + 6, y};
            tri[2] = {x, y + 4};
        } else { // ▾ expanded
            tri[0] = {x - 1, y - 3};
            tri[1] = {x + 7, y - 3};
            tri[2] = {x + 3, y + 4};
        }
        p.drawPolygon(tri, 3);
    }
}
