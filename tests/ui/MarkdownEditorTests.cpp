#include "ui/MarkdownEditor.h"

#include "core/Perf.h"

#include <QApplication>
#include <QEventLoop>
#include <QKeyEvent>
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
