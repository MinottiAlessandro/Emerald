#include "ui/MarkdownEditor.h"

#include "core/Perf.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextStream>
#include <QTimer>
#include <QWheelEvent>
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
    // Querying the block geometry forces the document's lazy layout even when
    // the editor is not currently painting this particular paragraph.
    editor.document()->documentLayout()->blockBoundingRect(block);
    QApplication::processEvents();
}

void waitForMs(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
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

void waitForQuickJump() {
    waitForMs(350);
}

void beginQuickJump(MarkdownEditor &editor) {
    sendKey(editor, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
    waitForQuickJump();
}

void endQuickJump(MarkdownEditor &editor) {
    sendKey(editor, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
}

void sendWheel(MarkdownEditor &editor, const QPoint &pixelDelta,
               const QPoint &angleDelta) {
    const QPointF local(editor.viewport()->rect().center());
    QWheelEvent event(local, editor.viewport()->mapToGlobal(local.toPoint()),
                      pixelDelta, angleDelta, Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(editor.viewport(), &event);
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

    // Heading hierarchy affects both glyph size and paragraph rhythm while
    // remaining a presentation-only property of the plain Markdown document.
    const QString headingSource = QStringLiteral(
        "# First\nbody\n## Second\nbody\n###### Sixth\nbody");
    editor.setPlainText(headingSource);
    editor.document()->setModified(false);
    const QTextBlock h1 = editor.document()->findBlockByNumber(0);
    const QTextBlock h2 = editor.document()->findBlockByNumber(2);
    const QTextBlock h6 = editor.document()->findBlockByNumber(4);
    settleLayout(editor, h6);
    check(h1.blockFormat().topMargin() > h2.blockFormat().topMargin() &&
              h2.blockFormat().topMargin() > h6.blockFormat().topMargin(),
          QStringLiteral("heading top spacing should follow visual hierarchy"));
    check(h1.blockFormat().bottomMargin() > h2.blockFormat().bottomMargin() &&
              h2.blockFormat().bottomMargin() > h6.blockFormat().bottomMargin(),
          QStringLiteral("heading bottom spacing should follow visual hierarchy"));
    const QTextCharFormat h1Format = h1.layout()->formats().constLast().format;
    const QTextCharFormat h6Format = h6.layout()->formats().constLast().format;
    check(h1Format.fontPointSize() > h6Format.fontPointSize(),
          QStringLiteral("higher-level headings should use larger glyphs"));
    check(h1Format.fontWeight() > h6Format.fontWeight(),
          QStringLiteral("higher-level headings should use stronger weight"));
    check(editor.toPlainText() == headingSource &&
              !editor.document()->isModified() &&
              !editor.document()->isUndoAvailable(),
          QStringLiteral("heading presentation must not alter source or undo"));

    // Hash-prefixed text inside a fence is code, not a heading, and must retain
    // ordinary code-block paragraph margins.
    editor.setPlainText(QStringLiteral("```\n# literal heading\n```"));
    const QTextBlock fencedHeading = editor.document()->findBlockByNumber(1);
    settleLayout(editor, fencedHeading);
    check(qFuzzyIsNull(fencedHeading.blockFormat().topMargin()),
          QStringLiteral("heading-looking fenced code should have no heading margin"));

    // Quotes use a stable hanging content edge and progressively deeper rails.
    // Both compact (`>>`) and spaced (`> >`) nesting are valid Markdown forms.
    const QString quoteTail = QStringLiteral(
        "quoted words continue for long enough to wrap across several visual "
        "lines in this deliberately narrow editor viewport");
    editor.resize(250, 220);
    editor.setPlainText(QStringLiteral("> ") + quoteTail +
                        QStringLiteral("\n> > nested ") + quoteTail +
                        QStringLiteral("\nafter quote"));
    const QTextBlock quoteOne = editor.document()->findBlockByNumber(0);
    const QTextBlock quoteTwo = editor.document()->findBlockByNumber(1);
    const QTextBlock afterQuote = editor.document()->findBlockByNumber(2);
    settleLayout(editor, quoteTwo);
    checkWrappedBlock(quoteOne, 2, QStringLiteral("single blockquote"));
    checkWrappedBlock(quoteTwo, 4, QStringLiteral("nested blockquote"));
    check(quoteTwo.blockFormat().leftMargin() >
              quoteOne.blockFormat().leftMargin(),
          QStringLiteral("nested blockquotes should receive deeper indentation"));
    check(quoteOne.blockFormat().topMargin() > 0.0 &&
              quoteTwo.blockFormat().topMargin() == 0.0 &&
              quoteTwo.blockFormat().bottomMargin() >
                  afterQuote.blockFormat().bottomMargin(),
          QStringLiteral("quote group spacing should occur only at its edges"));
    check(editor.toPlainText() ==
              QStringLiteral("> ") + quoteTail +
                  QStringLiteral("\n> > nested ") + quoteTail +
                  QStringLiteral("\nafter quote"),
          QStringLiteral("blockquote layout must preserve Markdown source"));

    editor.setPlainText(QStringLiteral("```\n> literal quote\n```"));
    const QTextBlock fencedQuote = editor.document()->findBlockByNumber(1);
    settleLayout(editor, fencedQuote);
    check(qFuzzyIsNull(fencedQuote.blockFormat().leftMargin()),
          QStringLiteral("quote-looking fenced code should remain unindented"));

    // Paragraph-only presentation changes must never become separate undo
    // commands. Moving away from a list conceals its marker and can change the
    // measured hanging indent, but Ctrl+Z should still address source edits.
    const QString undoSource = QStringLiteral("- list item\nplain line");
    editor.setPlainText(undoSource);
    editor.document()->setModified(false);
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    check(!editor.document()->isUndoAvailable(),
          QStringLiteral("visual list formatting should not enter undo history"));
    check(!editor.document()->isModified(),
          QStringLiteral("visual list formatting should not mark source dirty"));
    sendKey(editor, QEvent::KeyPress, Qt::Key_X, Qt::NoModifier,
            QStringLiteral("x"));
    check(editor.toPlainText() == undoSource + QLatin1Char('x'),
          QStringLiteral("typing should still edit plain Markdown source"));
    editor.undo();
    check(editor.toPlainText() == undoSource,
          QStringLiteral("one undo should revert one source edit"));

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
    editor.verticalScrollBar()->setValue(100);
    const int scrollBeforeDown = editor.verticalScrollBar()->value();
    sendKey(editor, QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    const int downTarget = editor.smoothScrollTarget();
    check(editor.smoothScrollActive() && downTarget > scrollBeforeDown,
          QStringLiteral("Down should begin a smooth downward scroll in Read "
                         "Mode"));
    waitForMs(35);
    const int scrollDuringDown = editor.verticalScrollBar()->value();
    check(scrollDuringDown > scrollBeforeDown && scrollDuringDown < downTarget,
          QStringLiteral("Read Mode Down should animate through an intermediate "
                         "pixel position"));
    waitForMs(100);
    check(editor.verticalScrollBar()->value() == downTarget &&
              !editor.smoothScrollActive(),
          QStringLiteral("Read Mode Down should finish at its pixel target"));
    check(editor.textCursor().position() == cursorBeforeScroll,
          QStringLiteral("Down should not move the hidden text cursor"));
    const int scrollBeforeUp = editor.verticalScrollBar()->value();
    sendKey(editor, QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    const int upTarget = editor.smoothScrollTarget();
    check(editor.smoothScrollActive() && upTarget < scrollBeforeUp,
          QStringLiteral("Up should begin a smooth upward scroll in Read Mode"));
    waitForMs(125);
    check(editor.verticalScrollBar()->value() == upTarget &&
              !editor.smoothScrollActive(),
          QStringLiteral("Read Mode Up should finish at its pixel target"));

    // Conventional wheel notches are animated and accumulate against the
    // pending target, while native high-resolution pixel deltas stay direct so
    // platform trackpad momentum is not animated a second time.
    editor.verticalScrollBar()->setValue(100);
    sendWheel(editor, {}, QPoint(0, -120));
    const int firstWheelTarget = editor.smoothScrollTarget();
    check(editor.smoothScrollActive() && firstWheelTarget > 100,
          QStringLiteral("a wheel notch should start smooth pixel scrolling"));
    sendWheel(editor, {}, QPoint(0, -120));
    const int secondWheelTarget = editor.smoothScrollTarget();
    check(secondWheelTarget > firstWheelTarget,
          QStringLiteral("repeated wheel notches should extend the pending "
                         "scroll target"));
    waitForMs(170);
    check(editor.verticalScrollBar()->value() == secondWheelTarget &&
              !editor.smoothScrollActive(),
          QStringLiteral("wheel scrolling should settle at the accumulated "
                         "target"));

    const int beforePixelDelta = editor.verticalScrollBar()->value();
    sendWheel(editor, QPoint(0, -17), {});
    check(editor.verticalScrollBar()->value() == beforePixelDelta + 17 &&
              !editor.smoothScrollActive(),
          QStringLiteral("native trackpad deltas should remain direct and "
                         "pixel exact"));

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
