#include "MarkdownEditor.h"

#include "MarkdownHighlighter.h"
#include "core/WikiLink.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QDesktopServices>
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
#include <QTextDocument>
#include <QTextLayout>
#include <QUrl>
#include <algorithm>

namespace {
// A task line: capture(1) = indent, capture(2) = the [ ] / [x] status char.
const QRegularExpression &taskRe() {
    static const QRegularExpression re(
        QStringLiteral("^(\\s*)[-*+]\\s+\\[([ xX])\\]\\s"));
    return re;
}

// A [text](url) inline link: capture(1) = text, capture(2) = url.
const QRegularExpression &mdLinkRe() {
    static const QRegularExpression re(
        QStringLiteral("\\[([^\\]\\[]+)\\]\\(([^)\\s]+)\\)"));
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
int sepAlign(const QString &cell) { // 0 left, 1 right, 2 centre, 3 explicit-left
    const QString t = cell.trimmed();
    const bool l = t.startsWith(QLatin1Char(':'));
    const bool r = t.endsWith(QLatin1Char(':'));
    return (l && r) ? 2 : r ? 1 : l ? 3 : 0;
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
    if (align == 3) // explicit left ":--": keep the colon the user typed
        return QLatin1Char(':') + QString(width - 1, QLatin1Char('-'));
    return QString(width, QLatin1Char('-'));
}

// QPlainTextEdit's layout ignores block-format line height and margins, so the
// only way to open up the gap between rows is to report a taller block. This
// layout pads each block with a fixed extra height below its text; the editor
// sets that padding from the spacing percentage and the font's line height.
class SpacedTextLayout : public QPlainTextDocumentLayout {
public:
    explicit SpacedTextLayout(QTextDocument *doc)
        : QPlainTextDocumentLayout(doc) {}
    void setExtraLeading(qreal px) { m_extra = qMax(qreal(0), px); }
    QRectF blockBoundingRect(const QTextBlock &block) const override {
        QRectF r = QPlainTextDocumentLayout::blockBoundingRect(block);
        // A folded-away block lays out at zero height; it must not reclaim space
        // through the row padding, or collapsed sections leave a phantom gap.
        if (m_extra > 0.0 && block.isVisible())
            r.setHeight(r.height() + m_extra);
        return r;
    }

private:
    qreal m_extra = 0.0;
};
}

MarkdownEditor::MarkdownEditor(QWidget *parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("editor"));

    // Swap in the spacing-aware layout before anything touches document().
    // QPlainTextEdit keeps any QPlainTextDocumentLayout it's handed, so the
    // subclass survives setDocument and drives the row spacing (see below).
    auto *doc = new QTextDocument(this);
    auto *spaced = new SpacedTextLayout(doc);
    doc->setDocumentLayout(spaced);
    setDocument(doc);
    m_spacedLayout = spaced;

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
        // Some edits move the caret to a new line without emitting
        // cursorPositionChanged — notably Ctrl+Backspace joining two lines. Re-
        // sync the active (revealed) block here too, or the merged line keeps
        // its markup concealed with no glyph painted (showing nothing at all).
        m_highlighter->setActiveBlock(textCursor().blockNumber());
        if (!m_applyingFolds && !m_folds.isEmpty())
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
    applyLineSpacing(); // extra leading is measured in font line-heights
}

void MarkdownEditor::setLineSpacing(int percent) {
    m_lineSpacing = qBound(100, percent, 300);
    applyLineSpacing();
}

void MarkdownEditor::applyLineSpacing() {
    if (!m_spacedLayout)
        return;
    // Turn the percentage into pixels of padding per row: 100% adds nothing,
    // 200% adds a whole extra line height below each block.
    const qreal lineHeight = QFontMetricsF(font()).lineSpacing();
    static_cast<SpacedTextLayout *>(m_spacedLayout)
        ->setExtraLeading(lineHeight * (m_lineSpacing - 100) / 100.0);
    // The layout adds the padding on demand from blockBoundingRect; nudge the
    // view to re-run geometry and repaint with the new spacing.
    document()->markContentsDirty(0, document()->characterCount());
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

bool MarkdownEditor::posInColumns(const QPoint &pos, const QTextBlock &block,
                                  int startCol, int endCol) const {
    // cursorForPosition snaps a click in the blank area past the end of a line
    // onto the nearest character — for a line that ends with a [[link]] that is
    // the link itself. Confirm the click x actually lies within the token's
    // rendered horizontal span so the clickable area stops at the text. (The
    // concealed brackets collapse to ~0 width, so [start,end] tracks what's
    // visible.)
    QTextCursor c(block);
    c.setPosition(block.position() + startCol);
    const int left = cursorRect(c).left();
    c.setPosition(block.position() + endCol);
    const int right = cursorRect(c).left();
    return pos.x() >= left && pos.x() <= right;
}

QString MarkdownEditor::linkAt(const QPoint &pos) const {
    const QTextCursor cursor = cursorForPosition(pos);
    const QTextBlock block = cursor.block();
    const int column = cursor.positionInBlock();

    auto it = WikiLink::pattern().globalMatch(block.text());
    while (it.hasNext()) {
        const auto m = it.next();
        if (column >= m.capturedStart(0) && column <= m.capturedEnd(0) &&
            posInColumns(pos, block, m.capturedStart(0), m.capturedEnd(0)))
            return WikiLink::cleanTarget(m.captured(1));
    }
    return {};
}

QString MarkdownEditor::internetLinkAt(const QPoint &pos) const {
    const QTextCursor cursor = cursorForPosition(pos);
    const QTextBlock block = cursor.block();
    const int column = cursor.positionInBlock();

    auto it = mdLinkRe().globalMatch(block.text());
    while (it.hasNext()) {
        const auto m = it.next();
        if (column >= m.capturedStart(0) && column <= m.capturedEnd(0) &&
            posInColumns(pos, block, m.capturedStart(0), m.capturedEnd(0)))
            return m.captured(2); // the URL
    }
    return {};
}

bool MarkdownEditor::followsLink(const QPoint &pos,
                                 Qt::KeyboardModifiers mods) const {
    if (linkAt(pos).isEmpty() && internetLinkAt(pos).isEmpty())
        return false;
    if (mods & Qt::ControlModifier)
        return true;
    // A plain click follows the link only when it is rendered, i.e. on a line
    // other than the one the cursor (active line) is on, where the markup is
    // still visible for editing.
    return cursorForPosition(pos).blockNumber() != textCursor().blockNumber();
}

QTextBlock MarkdownEditor::taskCheckboxBlockAt(const QPoint &pos) const {
    const QTextBlock block = cursorForPosition(pos).block();
    if (block.blockNumber() == textCursor().blockNumber())
        return {}; // the active line shows raw markup; edit it normally
    const auto m = taskRe().match(block.text());
    if (!m.hasMatch())
        return {};
    // The checkbox is painted over the dash; accept a click on that column.
    QTextCursor at(block);
    at.setPosition(block.position() + m.capturedLength(1)); // dash column
    const QRectF cell = cursorRect(at);
    const qreal cw = QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
    const QRectF hit(cell.left() - cw * 0.5, cell.top(), cw * 2.4, cell.height());
    return hit.contains(pos) ? block : QTextBlock();
}

bool MarkdownEditor::toggleTaskAt(const QPoint &pos) {
    const QTextBlock block = taskCheckboxBlockAt(pos);
    if (!block.isValid())
        return false;
    const auto m = taskRe().match(block.text());
    const int statusPos = m.capturedStart(2);
    QTextCursor edit(block);
    edit.setPosition(block.position() + statusPos);
    edit.setPosition(block.position() + statusPos + 1, QTextCursor::KeepAnchor);
    const bool done = block.text().at(statusPos).toLower() == QLatin1Char('x');
    edit.insertText(done ? QStringLiteral(" ") : QStringLiteral("x"));
    return true;
}

bool MarkdownEditor::isOverFoldControl(const QPoint &pos) const {
    return pos.x() < document()->documentMargin() &&
           headingFoldable(cursorForPosition(pos).block());
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
    if (event->button() == Qt::LeftButton && isOverFoldControl(event->pos())) {
        toggleFoldAt(cursorForPosition(event->pos()).block());
        return;
    }
    if (event->button() == Qt::LeftButton && copyCodeBlockAt(event->pos()))
        return;
    if (event->button() == Qt::LeftButton && toggleTaskAt(event->pos()))
        return;
    if (event->button() == Qt::LeftButton &&
        followsLink(event->pos(), event->modifiers())) {
        const QString url = internetLinkAt(event->pos());
        if (!url.isEmpty())
            QDesktopServices::openUrl(QUrl::fromUserInput(url));
        else
            emit linkClicked(linkAt(event->pos()));
        return;
    }
    QPlainTextEdit::mousePressEvent(event);
}

void MarkdownEditor::mouseMoveEvent(QMouseEvent *event) {
    // Show the hand cursor over anything clickable: links, task checkboxes, the
    // heading fold control, and a code block's copy button.
    const QPoint p = event->pos();
    const bool clickable = followsLink(p, event->modifiers()) ||
                           taskCheckboxBlockAt(p).isValid() ||
                           isOverFoldControl(p) || isOverCopyButton(p);
    viewport()->setCursor(clickable ? Qt::PointingHandCursor : Qt::IBeamCursor);
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

bool MarkdownEditor::indentSelection(bool deeper) {
    QTextCursor cur = textCursor();
    if (!cur.hasSelection())
        return false;
    const QTextBlock first = document()->findBlock(cur.selectionStart());
    QTextBlock last = document()->findBlock(cur.selectionEnd());
    // A selection ending exactly at a line start doesn't really include that
    // line (whole-line selections land there); back up to the previous block.
    if (last != first && cur.selectionEnd() == last.position())
        last = last.previous();
    if (first == last)
        return false; // single line — leave it to adjustListIndent / default Tab

    QTextCursor edit(document());
    edit.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        edit.setPosition(b.position());
        if (deeper) {
            edit.insertText(QStringLiteral("  ")); // one level = two spaces
        } else {
            const QString line = b.text();
            int n = 0;
            while (n < 2 && n < line.size() && line[n] == QLatin1Char(' '))
                ++n;
            if (n == 0 && !line.isEmpty() && line[0] == QLatin1Char('\t'))
                n = 1; // also accept a leading tab
            if (n > 0) {
                edit.movePosition(QTextCursor::NextCharacter,
                                  QTextCursor::KeepAnchor, n);
                edit.removeSelectedText();
            }
        }
        if (b == last)
            break;
    }
    edit.endEditBlock();

    // Re-select the affected lines so a run of Tabs keeps working. The block
    // handles stay valid across the edits (no blocks were split or merged).
    QTextCursor sel(document());
    sel.setPosition(first.position());
    sel.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    setTextCursor(sel);
    return true;
}

void MarkdownEditor::keyPressEvent(QKeyEvent *event) {
    // Ctrl+Del deletes the whole note. QPlainTextEdit would otherwise eat this
    // as "delete word forward" (it claims the shortcut via ShortcutOverride),
    // so intercept it here where the editor has focus and hand it to the window.
    if (event->modifiers() == Qt::ControlModifier &&
        event->key() == Qt::Key_Delete) {
        emit deleteNoteRequested();
        return;
    }
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
    // Ctrl+Enter: open a new line below without splitting the current one. Move
    // to the line end first, then reuse the normal Enter logic so a list keeps
    // continuing (and an empty item still clears itself).
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        (event->modifiers() & Qt::ControlModifier)) {
        QTextCursor c = textCursor();
        c.movePosition(QTextCursor::EndOfBlock);
        setTextCursor(c);
        if (insideCodeBlock(c.block()) || !continueList())
            textCursor().insertText(QStringLiteral("\n"));
        ensureCursorVisible();
        return;
    }

    // Enter at the very start of a collapsed heading inserts the blank line
    // above without popping the section open. A plain insert would split the
    // heading's block, drift its fold handle onto the new empty line, and
    // reapplyFolds would then drop the fold; instead re-point the fold (and its
    // captured end) to the heading's new position and re-apply.
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        !(event->modifiers() &
          (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier |
           Qt::ShiftModifier))) {
        QTextCursor c = textCursor();
        const int fi = foldIndexOf(c.block());
        if (fi >= 0 && c.atBlockStart() && !c.hasSelection()) {
            const int headNum = c.block().blockNumber();
            const int endNum =
                m_folds[fi].end.isValid() ? m_folds[fi].end.blockNumber() : -1;
            m_applyingFolds = true; // hold folds steady across the split
            c.insertText(QStringLiteral("\n"));
            setTextCursor(c);
            m_applyingFolds = false;
            // Everything from the heading down shifted one block lower.
            m_folds[fi].heading = document()->findBlockByNumber(headNum + 1);
            if (endNum >= 0)
                m_folds[fi].end = document()->findBlockByNumber(endNum + 1);
            reapplyFolds();
            return;
        }
    }

    // Tab / Shift+Tab over a multi-line selection indents every selected line.
    // Works inside code blocks too, so it runs before the code-block guard.
    if (event->key() == Qt::Key_Tab && indentSelection(true))
        return;
    if (event->key() == Qt::Key_Backtab && indentSelection(false))
        return;

    // Typing a pairing character with text selected wraps the selection in it
    // (select a word, press "(" -> "(word)"). Shift is fine — it produces the
    // character — but Ctrl/Alt/Cmd mean a shortcut, so bail on those.
    if (!(event->modifiers() &
          (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        surroundSelection(event->text()))
        return;

    // Inside a fenced code block the text is verbatim: Enter and Tab insert a
    // plain newline / indent instead of continuing a list or folding markup.
    const bool inCode = insideCodeBlock(textCursor().block());
    if (!inCode) {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) &&
            continueList()) {
            return;
        }
        if (event->key() == Qt::Key_Tab && handleTableTab())
            return;
        if (event->key() == Qt::Key_Backtab && handleTableTab(false))
            return;
        if (event->key() == Qt::Key_Tab && adjustListIndent(true))
            return;
        if (event->key() == Qt::Key_Backtab && adjustListIndent(false))
            return;
    }

    // Editor keybindings. (On macOS Qt maps ControlModifier to ⌘, so these are
    // Cmd-based there automatically.)
    const auto mods = event->modifiers();
    if (mods == Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_B: wrapSelection(QStringLiteral("**")); return; // bold
        case Qt::Key_I: wrapSelection(QStringLiteral("*")); return;  // italic
        case Qt::Key_L: selectCurrentLine(); return;
        case Qt::Key_D: duplicateLineOrSelection(); return;
        default: break;
        }
    } else if (mods == (Qt::ControlModifier | Qt::ShiftModifier) &&
               event->key() == Qt::Key_K) {
        deleteCurrentLine();
        return;
    } else if (mods == Qt::AltModifier &&
               (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        moveLines(event->key() == Qt::Key_Up);
        return;
    } else if (mods == Qt::AltModifier &&
               (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        // Browser-style history. Also bound as a window shortcut, but on macOS
        // Option+Arrow is a text-editing key (word left/right) that the QAction
        // shortcut never receives, so handle it here — where the editor has
        // focus — to make Alt/Option+Arrow navigate on every platform.
        if (event->key() == Qt::Key_Left)
            emit navigateBack();
        else
            emit navigateForward();
        return;
    }

    QPlainTextEdit::keyPressEvent(event);
    updateCompletionPopup();
}

// Wrap the selection in `marker` (e.g. ** or *), or unwrap if it's already
// wrapped. With no selection, insert an empty pair and place the caret inside.
void MarkdownEditor::wrapSelection(const QString &marker) {
    QTextCursor cur = textCursor();
    if (!cur.hasSelection()) {
        cur.insertText(marker + marker);
        cur.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor,
                         marker.size());
        setTextCursor(cur);
        return;
    }
    const QString text = cur.selectedText();
    const int n = marker.size();
    const bool wrapped =
        text.size() >= 2 * n && text.startsWith(marker) && text.endsWith(marker);
    const QString out =
        wrapped ? text.mid(n, text.size() - 2 * n) : marker + text + marker;
    cur.insertText(out);
    // Keep the result selected so a second press toggles it back off.
    cur.setPosition(cur.position() - out.size());
    cur.setPosition(cur.position() + out.size(), QTextCursor::KeepAnchor);
    setTextCursor(cur);
}

// Surround the selection with a typed pairing character: select a word and
// press "(" to get "(word)". Brackets and parens close with their match; the
// rest (* _ = ' " ` ~) pair with themselves. Returns false — leaving the key
// to insert normally — when `text` isn't a pairing char or nothing's selected.
bool MarkdownEditor::surroundSelection(const QString &text) {
    if (text.size() != 1)
        return false;
    const QChar ch = text.at(0);
    static const QString pairs = QStringLiteral("*(_=['\"`~");
    if (!pairs.contains(ch))
        return false;
    QTextCursor cur = textCursor();
    if (!cur.hasSelection())
        return false;
    const QChar close = ch == QLatin1Char('(')   ? QLatin1Char(')')
                        : ch == QLatin1Char('[') ? QLatin1Char(']')
                                                 : ch;
    const QString inner = cur.selectedText();
    cur.insertText(QString(ch) + inner + close);
    // Re-select the original text, now sitting between the new pair.
    const int afterClose = cur.position();
    cur.setPosition(afterClose - 1 - inner.size());
    cur.setPosition(afterClose - 1, QTextCursor::KeepAnchor);
    setTextCursor(cur);
    return true;
}

void MarkdownEditor::selectCurrentLine() {
    QTextCursor cur = textCursor();
    cur.movePosition(QTextCursor::StartOfBlock);
    // Include the trailing newline (so a follow-up delete removes the whole
    // line); fall back to end-of-block on the last line.
    if (!cur.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor))
        cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    setTextCursor(cur);
}

void MarkdownEditor::duplicateLineOrSelection() {
    QTextCursor cur = textCursor();
    if (cur.hasSelection()) {
        const QString sel = cur.selectedText();
        const int end = cur.selectionEnd();
        cur.setPosition(end);
        cur.insertText(sel); // a second copy right after the selection
        setTextCursor(cur);
        return;
    }
    const QString line = cur.block().text();
    const int col = cur.positionInBlock();
    cur.movePosition(QTextCursor::EndOfBlock);
    cur.insertText(QStringLiteral("\n") + line);
    // Keep the caret on the new (lower) copy, at the same column.
    cur.movePosition(QTextCursor::StartOfBlock);
    cur.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                     qMin(col, line.size()));
    setTextCursor(cur);
}

void MarkdownEditor::deleteCurrentLine() {
    const QTextBlock blk = textCursor().block();
    QTextCursor cur = textCursor();
    cur.beginEditBlock();
    if (blk.next().isValid()) {
        // Not the last line: take this line plus its trailing newline.
        cur.setPosition(blk.position());
        cur.setPosition(blk.next().position(), QTextCursor::KeepAnchor);
    } else if (blk.previous().isValid()) {
        // Last line: take the preceding newline too, so no blank line is left.
        cur.setPosition(blk.previous().position() + blk.previous().length() - 1);
        cur.setPosition(blk.position() + blk.length() - 1, QTextCursor::KeepAnchor);
    } else {
        // The only line: just clear its text.
        cur.setPosition(blk.position());
        cur.setPosition(blk.position() + blk.length() - 1, QTextCursor::KeepAnchor);
    }
    cur.removeSelectedText();
    cur.endEditBlock();
    setTextCursor(cur);
}

// Move the current line (or every line the selection touches) up or down by one,
// preserving the relative caret/selection.
void MarkdownEditor::moveLines(bool up) {
    QTextCursor cur = textCursor();
    QTextBlock first = document()->findBlock(cur.selectionStart());
    QTextBlock last = document()->findBlock(cur.selectionEnd());
    // A selection ending at the very start of a line doesn't include that line.
    if (cur.hasSelection() && cur.selectionEnd() == last.position() &&
        last.blockNumber() > first.blockNumber())
        last = last.previous();
    if (up ? !first.previous().isValid() : !last.next().isValid())
        return;
    // Capture line numbers now; the block handles will report their *new*
    // numbers once the edit below moves them.
    const int firstNo = first.blockNumber();
    const int lastNo = last.blockNumber();

    QTextCursor edit(document());
    edit.beginEditBlock();
    if (up) {
        QTextBlock prev = first.previous();
        const QString prevText = prev.text();
        const int cut = prevText.size() + 1; // the line plus its newline
        // End of the last line (before its newline); it shifts up by `cut` once
        // the previous line is removed. Compute it now, from original positions.
        const int insertAt = last.position() + last.length() - 1 - cut;
        // Cut the previous line plus its trailing newline...
        edit.setPosition(prev.position());
        edit.setPosition(first.position(), QTextCursor::KeepAnchor);
        edit.removeSelectedText();
        // ...and paste it just after the (now shifted-up) last line.
        edit.setPosition(insertAt);
        edit.insertText(QStringLiteral("\n") + prevText);
    } else {
        QTextBlock next = last.next();
        const QString nextText = next.text();
        // Cut the next line plus its leading newline...
        edit.setPosition(last.position() + last.length() - 1);
        edit.setPosition(next.position() + next.length() - 1,
                         QTextCursor::KeepAnchor);
        edit.removeSelectedText();
        // ...and paste it just before the first line.
        edit.setPosition(first.position());
        edit.insertText(nextText + QStringLiteral("\n"));
    }
    edit.endEditBlock();

    // Re-anchor the selection onto the moved lines at their new position.
    const int delta = (up ? -1 : 1);
    QTextBlock nf = document()->findBlockByNumber(firstNo + delta);
    QTextBlock nl = document()->findBlockByNumber(lastNo + delta);
    if (nf.isValid() && nl.isValid()) {
        QTextCursor sel(document());
        sel.setPosition(nf.position());
        sel.setPosition(nl.position() + nl.length() - 1, QTextCursor::KeepAnchor);
        setTextCursor(sel);
    }
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
    if (copied)
        emit noticeRequested(tr("Copied code to clipboard"));
    return copied;
}

bool MarkdownEditor::isOverCopyButton(const QPoint &pos) const {
    bool over = false;
    forEachCodeBlock([&](const CodeBlock &cb) {
        if (!cb.active && cb.copyBtn.contains(pos))
            over = true;
    });
    return over;
}

bool MarkdownEditor::insideCodeBlock(const QTextBlock &block) const {
    // The highlighter marks every fence + inner line StateCode (1). A line is
    // *inside* the block (so it must render verbatim) when it is StateCode and
    // its predecessor is too — that excludes the opening fence, whose previous
    // line is normal text. The closing fence is StateNormal, so it's excluded.
    return block.isValid() && block.userState() == 1 &&
           block.previous().isValid() && block.previous().userState() == 1;
}

int MarkdownEditor::headingLevel(const QString &text) const {
    int n = 0;
    while (n < text.size() && n < 6 && text[n] == QLatin1Char('#'))
        ++n;
    if (n > 0 && n < text.size() && text[n] == QLatin1Char(' '))
        return n;
    return 0;
}

QTextBlock MarkdownEditor::foldSectionEnd(const QTextBlock &heading) const {
    // The last block a fold of `heading` should hide: the section runs from
    // heading.next() down to the next same-or-higher heading (or EOF), minus any
    // trailing blank lines — so the blank separation before that next heading
    // stays visible. Invalid when the section has no foldable content.
    const int level = headingLevel(heading.text());
    QTextBlock lastContent;
    for (QTextBlock b = heading.next(); b.isValid(); b = b.next()) {
        // A "# ..." line inside a code block is literal text, not a heading,
        // so it must not end the section early.
        const int l = insideCodeBlock(b) ? 0 : headingLevel(b.text());
        if (l > 0 && l <= level)
            break;
        if (!b.text().trimmed().isEmpty())
            lastContent = b;
    }
    return lastContent;
}

bool MarkdownEditor::headingFoldable(const QTextBlock &heading) const {
    if (insideCodeBlock(heading))
        return false; // a "# ..." line inside a code block isn't a heading
    if (headingLevel(heading.text()) == 0)
        return false;
    return foldSectionEnd(heading).isValid(); // foldable only with content below
}

int MarkdownEditor::foldIndexOf(const QTextBlock &heading) const {
    for (int i = 0; i < m_folds.size(); ++i)
        if (m_folds[i].heading == heading)
            return i;
    return -1;
}

bool MarkdownEditor::isFolded(const QTextBlock &heading) const {
    return foldIndexOf(heading) >= 0;
}

void MarkdownEditor::toggleFoldAt(const QTextBlock &heading) {
    if (!headingFoldable(heading))
        return;
    const int idx = foldIndexOf(heading);
    if (idx >= 0) {
        m_folds.removeAt(idx);
    } else {
        // Capture the section extent now and hold it: later edits to the visible
        // trailing blank lines won't grow the fold (see Fold). Move the caret out
        // of the part about to be hidden; trailing blanks stay visible, so a
        // caret resting on one of those is fine to leave in place.
        const QTextBlock end = foldSectionEnd(heading);
        const int caret = textCursor().blockNumber();
        for (QTextBlock b = heading.next(); end.isValid() && b.isValid();
             b = b.next()) {
            if (b.blockNumber() == caret) {
                QTextCursor c(heading);
                c.movePosition(QTextCursor::EndOfBlock);
                setTextCursor(c);
                break;
            }
            if (b == end)
                break;
        }
        m_folds.append({heading, end});
    }
    reapplyFolds();
}

void MarkdownEditor::reapplyFolds() {
    if (m_applyingFolds)
        return;
    m_applyingFolds = true;

    // Forget folds whose heading was edited away or deleted.
    m_folds.erase(std::remove_if(m_folds.begin(), m_folds.end(),
                                 [this](const Fold &f) {
                                     return !f.heading.isValid() ||
                                            headingLevel(f.heading.text()) == 0;
                                 }),
                  m_folds.end());

    for (QTextBlock b = document()->firstBlock(); b.isValid(); b = b.next())
        if (!b.isVisible())
            b.setVisible(true);

    for (const Fold &f : m_folds) {
        // Use the extent captured when the fold was made. If that block was
        // deleted since, fall back to recomputing so the fold still holds.
        QTextBlock end = f.end.isValid() ? f.end : foldSectionEnd(f.heading);
        if (!end.isValid())
            continue;
        for (QTextBlock b = f.heading.next(); b.isValid(); b = b.next()) {
            b.setVisible(false);
            if (b == end)
                break; // leave the trailing blank lines before the next heading
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

// Tab inside a pipe table grows/navigates the grid:
//  - header row, last cell → append a new column (a cell in every row)
//  - separator row          → build a fresh data row below, go to its first cell
//  - any other non-last cell→ move to the next cell
//  - data row, last cell    → first cell of the row below, appending one if last
// Shift+Tab (forward=false) just steps back one cell, never growing the table.
bool MarkdownEditor::handleTableTab(bool forward) {
    const QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    if (!isTableRow(block.text()))
        return false;

    // Table extent and where the caret sits within it.
    QTextBlock first = block, last = block;
    while (first.previous().isValid() && isTableRow(first.previous().text()))
        first = first.previous();
    while (last.next().isValid() && isTableRow(last.next().text()))
        last = last.next();
    const int firstNo = first.blockNumber();
    const int rowIdx = block.blockNumber() - firstNo;
    const int rowCount = last.blockNumber() - firstNo + 1;

    // Row model + the separator's index (if any).
    QList<QStringList> rows;
    int sepRow = -1;
    for (QTextBlock b = first;; b = b.next()) {
        if (isSeparatorRow(b.text()))
            sepRow = rows.size();
        rows << splitRow(b.text());
        if (b == last)
            break;
    }
    int nCols = 0;
    for (const QStringList &r : rows)
        nCols = qMax(nCols, int(r.size()));

    // The caret's cell = number of pipes before it, minus the leading one.
    const QString text = block.text();
    const int caret = cursor.positionInBlock();
    int pipes = 0;
    for (int i = 0; i < caret && i < text.size(); ++i)
        if (text[i] == QLatin1Char('|'))
            ++pipes;
    const int cells = qMax(1, int(rows[rowIdx].size()));
    const int cellIdx = qBound(0, pipes - 1, cells - 1);
    const bool lastCell = cellIdx >= cells - 1;

    if (!forward) { // Shift+Tab: just step back one cell, no table growth.
        int backRow = rowIdx, backCell = cellIdx;
        if (cellIdx > 0) {
            backCell = cellIdx - 1;
        } else {
            int prev = rowIdx - 1;
            if (prev == sepRow) // never park the caret in the --- row
                --prev;
            if (prev < 0)
                return true; // already at the first cell; nothing precedes it
            backRow = prev;
            backCell = qMax(0, int(rows[prev].size()) - 1);
        }
        const QTextBlock dest = document()->findBlockByNumber(firstNo + backRow);
        if (dest.isValid())
            moveToTableCell(dest, backCell);
        return true;
    }

    // Pad every row out to `width`; separator cells fill with dashes.
    auto normalize = [&](int width) {
        for (int r = 0; r < rows.size(); ++r)
            while (rows[r].size() < width)
                rows[r] << (r == sepRow ? QStringLiteral("---") : QString());
    };
    auto emptyRow = [&] {
        QStringList r;
        for (int i = 0; i < nCols; ++i)
            r << QString();
        return r;
    };

    int targetRow = rowIdx;
    int targetCell = cellIdx;
    bool structural = false;

    if (isSeparatorRow(text)) {
        // Separator: build a fresh data row right below it.
        normalize(nCols);
        rows.insert(rowIdx + 1, emptyRow());
        targetRow = rowIdx + 1;
        targetCell = 0;
        structural = true;
    } else if (rowIdx == 0 && lastCell) {
        // End of the header: grow a new column across the whole table.
        normalize(nCols);
        for (int r = 0; r < rows.size(); ++r)
            rows[r] << (r == sepRow ? QStringLiteral("---") : QString());
        targetRow = 0;
        targetCell = nCols; // the freshly added last column
        structural = true;
    } else if (!lastCell) {
        targetCell = cellIdx + 1; // plain hop to the next cell
    } else if (rowIdx < rowCount - 1) {
        targetRow = rowIdx + 1; // first cell of the existing row below
        targetCell = 0;
    } else {
        // Last cell of the last row: append a new data row.
        normalize(nCols);
        rows << emptyRow();
        targetRow = rowIdx + 1;
        targetCell = 0;
        structural = true;
    }

    if (structural) {
        QStringList lines;
        for (const QStringList &r : rows) {
            QString line = QStringLiteral("|");
            for (const QString &cell : r)
                line += QLatin1Char(' ') + cell + QStringLiteral(" |");
            lines << line;
        }
        QTextCursor edit(document());
        edit.beginEditBlock();
        edit.setPosition(first.position());
        edit.setPosition(last.position() + last.length() - 1,
                         QTextCursor::KeepAnchor);
        m_prettifying = true; // suppress the leave-table reformat during the edit
        edit.insertText(lines.join(QLatin1Char('\n')));
        edit.endEditBlock();
        m_prettifying = false;
        prettifyTableAt(firstNo); // align the rebuilt grid
    }

    const QTextBlock dest = document()->findBlockByNumber(firstNo + targetRow);
    if (dest.isValid())
        moveToTableCell(dest, targetCell);
    return true;
}

void MarkdownEditor::moveToTableCell(const QTextBlock &block, int cellIdx) {
    const QString t = block.text();
    QList<int> pipes;
    for (int i = 0; i < t.size(); ++i)
        if (t[i] == QLatin1Char('|'))
            pipes << i;
    QTextCursor cur(block);
    if (pipes.size() < 2) { // not a real row; land at its start
        setTextCursor(cur);
        return;
    }
    cellIdx = qBound(0, cellIdx, int(pipes.size()) - 2);
    int s = pipes[cellIdx] + 1;
    int e = pipes[cellIdx + 1];
    while (s < e && t[s] == QLatin1Char(' '))
        ++s;
    while (e > s && t[e - 1] == QLatin1Char(' '))
        --e;
    if (e > s) { // select the cell's content so typing overwrites it
        cur.setPosition(block.position() + s);
        cur.setPosition(block.position() + e, QTextCursor::KeepAnchor);
    } else { // empty cell: sit just inside it
        const int p = qMin(pipes[cellIdx] + 2, pipes[cellIdx + 1] - 1);
        cur.setPosition(block.position() + p);
    }
    setTextCursor(cur);
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
            bg.fillPath(path, QColor(0x1f, 0x47, 0x33));
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
        // Folded-away lines collapse onto their heading; skip them or their
        // bullet/checkbox/rule glyphs would pile up just under the title.
        if (!block.isVisible())
            continue;
        // Inside a code block the text is verbatim — no bullets/rules/checkboxes.
        if (insideCodeBlock(block))
            continue;
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
            const QColor accent(0x2b, 0xbf, 0x74);
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

        // Two offset rounded rects = a "copy" (stacked pages) glyph.
        const QRectF btn = cb.copyBtn;
        QPen pen(QColor(0x92, 0xb3, 0xa2));
        pen.setWidthF(1.3);
        p.setPen(pen);
        p.setBrush(QColor(0x1f, 0x47, 0x33)); // the header bar's colour
        p.drawRoundedRect(QRectF(btn.left() + 5, btn.top() + 2, 8, 10), 2, 2);
        p.drawRoundedRect(QRectF(btn.left() + 2, btn.top() + 4, 8, 10), 2, 2);
    });

    // Fold arrows in the left margin next to foldable headings.
    for (QTextBlock block = firstVisibleBlock(); block.isValid();
         block = block.next()) {
        const QRectF geo = blockBoundingGeometry(block).translated(contentOffset());
        if (geo.top() > event->rect().bottom())
            break;
        // A heading hidden inside an enclosing fold (e.g. a collapsed child under
        // a collapsed parent) lays out at zero height on the parent's line; it
        // must not paint its own arrow/ellipsis there.
        if (!block.isVisible())
            continue;
        if (geo.bottom() < event->rect().top() || !headingFoldable(block))
            continue;
        QTextCursor hc(block);
        const qreal y = cursorRect(hc).center().y();
        const qreal x = 5;
        const bool folded = isFolded(block);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x4f, 0x75, 0x65));
        QPointF tri[3];
        if (folded) { // ▸ collapsed
            tri[0] = {x, y - 4};
            tri[1] = {x + 6, y};
            tri[2] = {x, y + 4};
        } else { // ▾ expanded
            tri[0] = {x - 1, y - 3};
            tri[1] = {x + 7, y - 3};
            tri[2] = {x + 3, y + 4};
        }
        p.drawPolygon(tri, 3);

        // Trailing "⋯" — three faint dots after a collapsed heading's title —
        // so a folded section reads as collapsed even away from the margin.
        if (folded) {
            hc.movePosition(QTextCursor::EndOfBlock);
            const QRectF end = cursorRect(hc);
            const qreal dotR = 1.4;
            const qreal gap = 5.5;
            qreal dx = end.right() + 10;
            p.setBrush(QColor(0x4f, 0x75, 0x65));
            for (int i = 0; i < 3; ++i, dx += gap)
                p.drawEllipse(QPointF(dx, end.center().y()), dotR, dotR);
        }
    }
}
