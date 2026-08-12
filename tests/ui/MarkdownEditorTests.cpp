#include "ui/MarkdownEditor.h"
#include "ui/MarkdownHighlighter.h"

#include "core/Perf.h"

#include <QApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextStream>
#include <QTimer>
#include <QtMath>

Q_LOGGING_CATEGORY(emeraldPerf, "emerald.perf.tests")

namespace {
int failures = 0;

void check(bool condition, const QString &message) {
    if (condition)
        return;
    ++failures;
    QTextStream(stderr) << "FAIL: " << message << '\n';
}

void settleLayout(MarkdownEditor &editor, const QTextBlock &block) {
    QApplication::processEvents();
    // Querying the block geometry forces QPlainTextDocumentLayout's lazy block
    // layout even when the editor is not currently painting this particular
    // line.
    editor.document()->documentLayout()->blockBoundingRect(block);
    QApplication::processEvents();
}

void checkWrappedBlock(const QTextBlock &block, int contentStart,
                       const QString &description) {
    QTextLayout *layout = block.layout();
    check(layout && layout->lineCount() >= 2,
          description + QStringLiteral(" should wrap in the test viewport"));
    if (!layout || layout->lineCount() < 2)
        return;

    const QTextLine first = layout->lineAt(0);
    const qreal expectedX = first.cursorToX(contentStart);
    const qreal rightEdge = first.x() + first.width();
    for (int i = 1; i < layout->lineCount(); ++i) {
        const QTextLine line = layout->lineAt(i);
        check(qAbs(line.x() - expectedX) < 0.1,
              description + QStringLiteral(" continuation line %1 should "
                                           "align with its content")
                                .arg(i));
        check(qAbs(line.x() + line.width() - rightEdge) < 0.1,
              description + QStringLiteral(" continuation line %1 should "
                                           "retain the right margin")
                                .arg(i));
    }
}

QTextCharFormat highlighterFormatAt(const QTextBlock &block, int offset) {
    if (!block.layout())
        return {};
    for (const QTextLayout::FormatRange &range : block.layout()->formats())
        if (offset >= range.start && offset < range.start + range.length)
            return range.format;
    return {};
}

bool sameInlineVisual(const QTextCharFormat &left,
                      const QTextCharFormat &right) {
    return left.foreground() == right.foreground() &&
           left.background() == right.background() &&
           left.fontWeight() == right.fontWeight() &&
           left.fontItalic() == right.fontItalic() &&
           left.fontStrikeOut() == right.fontStrikeOut() &&
           left.fontUnderline() == right.fontUnderline();
}

void checkListCase(MarkdownEditor &editor, const QString &line,
                   int contentStart, const QString &description,
                   bool makeInactive = false) {
    const QString source = makeInactive
                               ? line + QStringLiteral("\nplain trailing line")
                               : line;
    editor.setPlainText(source);
    if (makeInactive)
        editor.moveCursor(QTextCursor::End);

    const QTextBlock block = editor.document()->firstBlock();
    settleLayout(editor, block);
    checkWrappedBlock(block, contentStart, description);
    check(editor.toPlainText() == source,
          description + QStringLiteral(" layout must not alter Markdown source"));

    if (makeInactive) {
        // Moving the caret onto a rendered checklist reveals its raw marker,
        // changing that prefix's glyph widths. Alignment must follow the newly
        // visible content start rather than retaining stale checkbox geometry.
        QTextCursor active(block);
        active.movePosition(QTextCursor::EndOfBlock);
        editor.setTextCursor(active);
        settleLayout(editor, block);
        checkWrappedBlock(block, contentStart,
                          description + QStringLiteral(" while active"));
    }

    // A viewport-width change clears Qt's lazy layouts. The hanging indent
    // must be restored when the list wraps again at its new width.
    editor.resize(190, 220);
    settleLayout(editor, block);
    checkWrappedBlock(block, contentStart,
                      description + QStringLiteral(" after resize"));
    editor.resize(250, 220);
}

void sendKey(MarkdownEditor &editor, QEvent::Type type, int key,
             Qt::KeyboardModifiers modifiers,
             const QString &text = QString()) {
    QKeyEvent event(type, key, modifiers, text);
    QApplication::sendEvent(&editor, &event);
}

void sendMousePress(MarkdownEditor &editor, const QPoint &position,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(position),
                      QPointF(editor.viewport()->mapToGlobal(position)),
                      Qt::LeftButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(editor.viewport(), &event);
}

void waitForQuickJump() {
    QEventLoop loop;
    QTimer::singleShot(350, &loop, &QEventLoop::quit);
    loop.exec();
}

void beginQuickJump(MarkdownEditor &editor) {
    sendKey(editor, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
    waitForQuickJump();
}

void endQuickJump(MarkdownEditor &editor) {
    sendKey(editor, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
}
} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    MarkdownEditor editor;
    editor.resize(250, 220);
    editor.show();

    const QString tail = QStringLiteral(
        "continues with enough words to wrap onto more than one visual line "
        "while keeping all of the Markdown on one source line");
    checkListCase(editor, QStringLiteral("- Bullet item ") + tail, 2,
                  QStringLiteral("bullet item"));
    checkListCase(editor, QStringLiteral("  12. Numbered item ") + tail, 6,
                  QStringLiteral("nested numbered item"));
    checkListCase(editor, QStringLiteral("    - [ ] Checklist item ") + tail, 10,
                  QStringLiteral("nested rendered checklist"), true);

    const QString paragraph = QStringLiteral(
        "An ordinary paragraph also contains enough words to wrap across "
        "several visual lines without receiving a hanging indent.");
    editor.setPlainText(paragraph);
    QTextBlock plainBlock = editor.document()->firstBlock();
    settleLayout(editor, plainBlock);
    QTextLayout *plainLayout = plainBlock.layout();
    check(plainLayout && plainLayout->lineCount() >= 2,
          QStringLiteral("ordinary paragraph should wrap in the test viewport"));
    if (plainLayout && plainLayout->lineCount() >= 2) {
        const qreal originX = plainLayout->lineAt(0).x();
        for (int i = 1; i < plainLayout->lineCount(); ++i)
            check(qAbs(plainLayout->lineAt(i).x() - originX) < 0.1,
                  QStringLiteral("ordinary paragraph should remain flush left"));
    }

    // Table cells use the same inline-preview rules as ordinary prose. Their
    // auto-alignment measures rendered content rather than Markdown source, and
    // the alias pipe in [[target|alias]] is not mistaken for a column boundary.
    check(MarkdownHighlighter::inlinePreviewColumnCount(
              QStringLiteral("**Bold**")) == 4,
          QStringLiteral("table width should ignore emphasis delimiters"));
    check(MarkdownHighlighter::inlinePreviewColumnCount(
              QStringLiteral("[[A very long target|Alias]]")) == 5,
          QStringLiteral("table width should measure a wiki-link alias only"));
    check(MarkdownHighlighter::inlinePreviewColumnCount(QStringLiteral(
              "[Web](https://example.com/a/very/long/path)")) == 3,
          QStringLiteral("table width should ignore an internet-link URL"));

    const QString rawTable = QStringLiteral(
        "| Column | Link |\n"
        "| --- | --- |\n"
        "| **x** | [[A very long target|y]] |\n"
        "after table");
    editor.setPlainText(rawTable);
    QTextCursor inTable(editor.document()->findBlockByNumber(2));
    inTable.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(inTable);
    QTextCursor afterTable(editor.document()->findBlockByNumber(3));
    editor.setTextCursor(afterTable); // leaving the table triggers prettification
    QApplication::processEvents();
    check(editor.toPlainText() == QStringLiteral(
              "| Column | Link |\n"
              "| ------ | ---- |\n"
              "| **x**      | [[A very long target|y]]    |\n"
              "after table"),
          QStringLiteral("table alignment should use rendered cell widths and "
                         "preserve wiki-link aliases"));
    check(MarkdownHighlighter::tablePipePositions(
              QStringLiteral("| **x** | [[A very long target|y]] |"))
                  .size() == 3,
          QStringLiteral("a wiki-link alias pipe should stay inside its cell"));

    const QString inlineTable = QStringLiteral(
        "| Bold | Italic | Strike | Mark | Code | Note | Web |\n"
        "| --- | --- | --- | --- | --- | --- | --- |\n"
        "| **Bold** | *Italic* | ~~Strike~~ | ==Mark== | `Code` | "
        "[[Target|Alias]] | [Web](https://example.com) |\n"
        "after table\n"
        "**Bold** *Italic* ~~Strike~~ ==Mark== `Code` [[Target|Alias]] "
        "[Web](https://example.com)");
    editor.setPlainText(inlineTable);
    QTextCursor inactive(editor.document()->findBlockByNumber(3));
    editor.setTextCursor(inactive);
    const QTextBlock data = editor.document()->findBlockByNumber(2);
    const QTextBlock ordinary = editor.document()->findBlockByNumber(4);
    settleLayout(editor, data);
    settleLayout(editor, ordinary);
    const QString dataText = data.text();
    const QString ordinaryText = ordinary.text();
    check(highlighterFormatAt(data, dataText.indexOf(QStringLiteral("Bold")))
                  .fontWeight() >= QFont::Bold,
          QStringLiteral("bold should render inside a table cell"));
    check(highlighterFormatAt(data, dataText.indexOf(QStringLiteral("Italic")))
              .fontItalic(),
          QStringLiteral("italic should render inside a table cell"));
    check(highlighterFormatAt(data, dataText.indexOf(QStringLiteral("Strike")))
              .fontStrikeOut(),
          QStringLiteral("strikethrough should render inside a table cell"));
    check(highlighterFormatAt(data, dataText.indexOf(QStringLiteral("Mark")))
              .background()
              .style() != Qt::NoBrush,
          QStringLiteral("highlight should render inside a table cell"));
    check(highlighterFormatAt(data, dataText.indexOf(QStringLiteral("Code")))
              .background()
              .style() != Qt::NoBrush,
          QStringLiteral("inline code should render inside a table cell"));
    check(highlighterFormatAt(data, dataText.indexOf(QStringLiteral("Alias")))
              .fontUnderline(),
          QStringLiteral("wiki links should render inside a table cell"));
    check(highlighterFormatAt(data, dataText.lastIndexOf(QStringLiteral("Web")))
              .fontUnderline(),
          QStringLiteral("internet links should render inside a table cell"));
    check(highlighterFormatAt(data, dataText.indexOf(QStringLiteral("**")))
              .foreground()
              .color()
              .alpha() == 0,
          QStringLiteral("inactive table markup should be concealed"));
    for (const QString &sample : {QStringLiteral("Bold"),
                                  QStringLiteral("Italic"),
                                  QStringLiteral("Strike"),
                                  QStringLiteral("Mark"),
                                  QStringLiteral("Code"),
                                  QStringLiteral("Alias"),
                                  QStringLiteral("Web")}) {
        check(sameInlineVisual(
                  highlighterFormatAt(data, dataText.indexOf(sample)),
                  highlighterFormatAt(ordinary, ordinaryText.indexOf(sample))),
              QStringLiteral("%1 should have the same visual style inside and "
                             "outside a table")
                  .arg(sample));
    }
    const QString formattedInlineTable = editor.toPlainText();
    check(formattedInlineTable.contains(QStringLiteral("**Bold**")) &&
              formattedInlineTable.contains(QStringLiteral("*Italic*")) &&
              formattedInlineTable.contains(QStringLiteral("~~Strike~~")) &&
              formattedInlineTable.contains(QStringLiteral("==Mark==")) &&
              formattedInlineTable.contains(QStringLiteral("`Code`")) &&
              formattedInlineTable.contains(QStringLiteral("[[Target|Alias]]")) &&
              formattedInlineTable.contains(
                  QStringLiteral("[Web](https://example.com)")),
          QStringLiteral("table auto-formatting should preserve inline markup"));

    const QString codeSource = QStringLiteral(
        "```\n- list-looking code that is deliberately long enough to wrap "
        "but must stay verbatim inside its fenced code block\n```");
    editor.setPlainText(codeSource);
    const QTextBlock codeBlock = editor.document()->findBlockByNumber(1);
    settleLayout(editor, codeBlock);
    QTextLayout *codeLayout = codeBlock.layout();
    check(codeLayout && codeLayout->lineCount() >= 2,
          QStringLiteral("fenced code sample should wrap in the test viewport"));
    if (codeLayout && codeLayout->lineCount() >= 2) {
        const qreal originX = codeLayout->lineAt(0).x();
        for (int i = 1; i < codeLayout->lineCount(); ++i)
            check(qAbs(codeLayout->lineAt(i).x() - originX) < 0.1,
                  QStringLiteral("list-looking fenced code should remain verbatim"));
    }
    check(editor.toPlainText() == codeSource,
          QStringLiteral("fenced-code layout must not alter source"));

    // Read Mode removes the caret, rejects editing keys, and turns plain arrow
    // navigation into viewport scrolling without relocating the text cursor.
    QStringList readingLines;
    for (int i = 0; i < 80; ++i)
        readingLines << QStringLiteral("Reading line %1").arg(i);
    const QString readingSource = readingLines.join(QLatin1Char('\n'));
    editor.setPlainText(readingSource);
    editor.resize(300, 180);
    editor.setReadMode(true);
    QApplication::processEvents();
    check(editor.readMode() && editor.isReadOnly(),
          QStringLiteral("Read Mode should make the editor read-only"));
    check(editor.cursorWidth() == 0,
          QStringLiteral("Read Mode should hide the text caret"));

    const int cursorBeforeScroll = editor.textCursor().position();
    editor.verticalScrollBar()->setValue(2);
    const int scrollBeforeDown = editor.verticalScrollBar()->value();
    sendKey(editor, QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    check(editor.verticalScrollBar()->value() > scrollBeforeDown,
          QStringLiteral("Down should scroll the page in Read Mode"));
    check(editor.textCursor().position() == cursorBeforeScroll,
          QStringLiteral("Down should not move the hidden text cursor"));
    const int scrollBeforeUp = editor.verticalScrollBar()->value();
    sendKey(editor, QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    check(editor.verticalScrollBar()->value() < scrollBeforeUp,
          QStringLiteral("Up should scroll the page in Read Mode"));

    sendKey(editor, QEvent::KeyPress, Qt::Key_A, Qt::NoModifier,
            QStringLiteral("a"));
    check(editor.toPlainText() == readingSource,
          QStringLiteral("typing should not change a note in Read Mode"));
    bool navigatedBack = false;
    QObject::connect(&editor, &MarkdownEditor::navigateBack,
                     [&navigatedBack] { navigatedBack = true; });
    sendKey(editor, QEvent::KeyPress, Qt::Key_Left, Qt::AltModifier);
    check(navigatedBack,
          QStringLiteral("Read Mode should preserve history navigation"));
    editor.setReadMode(false);
    check(!editor.isReadOnly() && editor.cursorWidth() > 0,
          QStringLiteral("leaving Read Mode should restore editing and caret"));

    // Quick Jump labels follow the physical QWERTY rows, so the first two
    // visible links are Q and W rather than A and B.
    editor.resize(700, 700);
    editor.activateWindow();
    editor.setFocus();
    QString jumpedTo;
    QObject::connect(&editor, &MarkdownEditor::linkClicked,
                     [&jumpedTo](const QString &target) { jumpedTo = target; });

    // A rendered wiki link can occupy several visual lines while remaining one
    // QTextBlock. Every visible segment should have its own clickable x range.
    editor.resize(180, 240);
    const QString wrappedTarget(47, QLatin1Char('W'));
    editor.setPlainText(QStringLiteral("[[") + wrappedTarget +
                        QStringLiteral("]]\nplain trailing line"));
    QTextCursor trailing(editor.document()->findBlockByNumber(1));
    editor.setTextCursor(trailing); // render (conceal) the link on block zero
    const QTextBlock wrappedBlock = editor.document()->firstBlock();
    settleLayout(editor, wrappedBlock);
    QTextLayout *wrappedLayout = wrappedBlock.layout();
    check(wrappedLayout && wrappedLayout->lineCount() >= 3,
          QStringLiteral("the wrapped-link fixture should span visual lines"));
    if (wrappedLayout && wrappedLayout->lineCount() >= 2) {
        const int displayStart = 2;
        const int displayEnd = displayStart + wrappedTarget.size();
        QTextCursor sourceStart(wrappedBlock);
        sourceStart.setPosition(wrappedBlock.position());
        QTextCursor sourceEnd(wrappedBlock);
        sourceEnd.setPosition(wrappedBlock.position() + wrappedBlock.length() - 1);
        const int legacyLeft = editor.cursorRect(sourceStart).left();
        const int legacyRight = editor.cursorRect(sourceEnd).left();
        bool exercisesWrappedRange = false;
        for (int i = 0; i < wrappedLayout->lineCount(); ++i) {
            const QTextLine line = wrappedLayout->lineAt(i);
            const int overlapStart = qMax(displayStart, line.textStart());
            const int overlapEnd =
                qMin(displayEnd, line.textStart() + line.textLength());
            if (overlapStart >= overlapEnd)
                continue;

            // Click the final visible character on each segment; this catches
            // the old whole-token x range, which ended at the short final line.
            const int column = overlapEnd - 1;
            QTextCursor before(wrappedBlock);
            before.setPosition(wrappedBlock.position() + column);
            QTextCursor after(wrappedBlock);
            after.setPosition(wrappedBlock.position() + column + 1);
            const QRect beforeRect = editor.cursorRect(before);
            const QRect afterRect = editor.cursorRect(after);
            const QPoint click((beforeRect.left() + afterRect.left()) / 2,
                               beforeRect.center().y());
            exercisesWrappedRange =
                exercisesWrappedRange || click.x() < legacyLeft ||
                click.x() > legacyRight;

            jumpedTo.clear();
            editor.setTextCursor(trailing);
            sendMousePress(editor, click);
            check(jumpedTo == wrappedTarget,
                  QStringLiteral("wrapped wiki-link visual line %1 should be "
                                 "clickable")
                      .arg(i));
        }
        check(exercisesWrappedRange,
              QStringLiteral("the fixture should exercise a wrapped segment "
                             "outside the old whole-token x range"));
    }

    editor.resize(700, 700);
    editor.setPlainText(QStringLiteral("[[First]] then [[Second]]"));
    QApplication::processEvents();

    beginQuickJump(editor);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Q, Qt::AltModifier,
            QStringLiteral("q"));
    check(jumpedTo == QStringLiteral("First"),
          QStringLiteral("Quick Jump Q should open the first visible link"));
    endQuickJump(editor);

    jumpedTo.clear();
    beginQuickJump(editor);
    sendKey(editor, QEvent::KeyPress, Qt::Key_W, Qt::AltModifier,
            QStringLiteral("w"));
    check(jumpedTo == QStringLiteral("Second"),
          QStringLiteral("Quick Jump W should open the second visible link"));
    endQuickJump(editor);

    // Code-looking links and Markdown images are not navigable targets; an
    // external target after them still participates in the same ordering.
    QString notice;
    QObject::connect(&editor, &MarkdownEditor::noticeRequested,
                     [&notice](const QString &text) { notice = text; });
    editor.setPlainText(QStringLiteral(
        "`[[Code]]` ![image](missing.png) [[Real]] "
        "[unsafe](javascript:bad)"));
    QApplication::processEvents();

    jumpedTo.clear();
    beginQuickJump(editor);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Q, Qt::AltModifier,
            QStringLiteral("q"));
    check(jumpedTo == QStringLiteral("Real"),
          QStringLiteral("Quick Jump should skip inline code and images"));
    endQuickJump(editor);

    notice.clear();
    beginQuickJump(editor);
    sendKey(editor, QEvent::KeyPress, Qt::Key_W, Qt::AltModifier,
            QStringLiteral("w"));
    check(notice == QStringLiteral("Blocked unsafe link"),
          QStringLiteral("Quick Jump should route external links through URL "
                         "security"));
    endQuickJump(editor);

    // More than 26 visible links switch the whole overlay to fixed-width
    // hints. The first is QQ; index 26 rolls over to WQ in the same key order.
    QStringList manyLinks;
    for (int i = 1; i <= 27; ++i)
        manyLinks << QStringLiteral("[[Note %1]]").arg(i);
    editor.setPlainText(manyLinks.join(QLatin1Char('\n')));
    QApplication::processEvents();

    jumpedTo.clear();
    beginQuickJump(editor);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Q, Qt::AltModifier,
            QStringLiteral("q"));
    check(jumpedTo.isEmpty(),
          QStringLiteral("a fixed-width Quick Jump hint should wait for key two"));
    sendKey(editor, QEvent::KeyPress, Qt::Key_Q, Qt::AltModifier,
            QStringLiteral("q"));
    check(jumpedTo == QStringLiteral("Note 1"),
          QStringLiteral("Quick Jump QQ should open the first of 27 links"));
    endQuickJump(editor);

    jumpedTo.clear();
    beginQuickJump(editor);
    sendKey(editor, QEvent::KeyPress, Qt::Key_W, Qt::AltModifier,
            QStringLiteral("w"));
    sendKey(editor, QEvent::KeyPress, Qt::Key_Q, Qt::AltModifier,
            QStringLiteral("q"));
    check(jumpedTo == QStringLiteral("Note 27"),
          QStringLiteral("Quick Jump WQ should open link 27"));
    endQuickJump(editor);

    // A key pressed before the hold delay cancels Quick Jump and remains an
    // ordinary existing Alt shortcut.
    editor.setPlainText(QStringLiteral("one\ntwo"));
    QTextCursor firstLine(editor.document()->firstBlock());
    editor.setTextCursor(firstLine);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Down, Qt::AltModifier);
    endQuickJump(editor);
    check(editor.toPlainText() == QStringLiteral("two\none"),
          QStringLiteral("Alt+Down should still move a line without entering "
                         "Quick Jump"));

    if (failures == 0)
        QTextStream(stdout) << "All Markdown editor regression tests passed.\n";
    return failures == 0 ? 0 : 1;
}
