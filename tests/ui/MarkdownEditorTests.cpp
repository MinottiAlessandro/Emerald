#include "ui/MarkdownEditor.h"
#include "ui/MarkdownReadObjectRenderer.h"
#include "ui/MathRender.h"

#include "core/Perf.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextStream>
#include <QTextTable>
#include <QTemporaryDir>
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

QTextCharFormat formatAt(const QTextBlock &block, int position) {
    if (!block.layout())
        return {};
    for (const QTextLayout::FormatRange &range : block.layout()->formats())
        if (position >= range.start && position < range.start + range.length)
            return range.format;
    return {};
}

int firstOpaqueRow(const QImage &image) {
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) != 0)
                return y;
    return -1;
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

void clickEditor(MarkdownEditor &editor, const QPoint &position,
                 Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(position),
                      QPointF(editor.viewport()->mapToGlobal(position)),
                      Qt::LeftButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(editor.viewport(), &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(position),
                        QPointF(editor.viewport()->mapToGlobal(position)),
                        Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(editor.viewport(), &release);
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

    // Source indentation is normalized into stable visual nesting steps. The
    // marker width remains font-shaped, so ordered and task markers can differ
    // without disturbing the content edge or wrapped continuation lines.
    const QString listGroup = QStringLiteral(
        "- top level\n  - nested level\n    12. deeper ordered item\n"
        "    - [ ] deeper task item\nafter list");
    editor.setPlainText(listGroup);
    const QTextBlock listTop = editor.document()->findBlockByNumber(0);
    const QTextBlock listNested = editor.document()->findBlockByNumber(1);
    const QTextBlock listDeep = editor.document()->findBlockByNumber(2);
    const QTextBlock listTask = editor.document()->findBlockByNumber(3);
    const QTextBlock listAfter = editor.document()->findBlockByNumber(4);
    settleLayout(editor, listTask);
    const qreal firstNestStep = listNested.blockFormat().leftMargin() -
                                listTop.blockFormat().leftMargin();
    const qreal secondNestStep = listDeep.blockFormat().leftMargin() -
                                 listNested.blockFormat().leftMargin();
    check(firstNestStep > 0.0 && secondNestStep > 0.0,
          QStringLiteral("nested list levels should move progressively inward"));
    check(listTop.blockFormat().topMargin() > 0.0 &&
              qFuzzyIsNull(listNested.blockFormat().topMargin()) &&
              listTask.blockFormat().bottomMargin() >
                  listAfter.blockFormat().bottomMargin(),
          QStringLiteral("list group spacing should occur only at its edges"));
    check(editor.toPlainText() == listGroup,
          QStringLiteral("normalized list layout must preserve source indentation"));

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

    const QString paddedCodeSource = QStringLiteral(
        "before\n```cpp\nconst int value = 7;\n```\nafter");
    editor.setPlainText(paddedCodeSource);
    editor.moveCursor(QTextCursor::End);
    const QTextBlock codeOpen = editor.document()->findBlockByNumber(1);
    const QTextBlock codeBody = editor.document()->findBlockByNumber(2);
    const QTextBlock codeClose = editor.document()->findBlockByNumber(3);
    settleLayout(editor, codeClose);
    check(codeOpen.blockFormat().leftMargin() > 0.0 &&
              codeBody.blockFormat().leftMargin() ==
                  codeOpen.blockFormat().leftMargin() &&
              codeClose.blockFormat().rightMargin() ==
                  codeOpen.blockFormat().rightMargin(),
          QStringLiteral("every fenced-code row should share horizontal padding"));
    check(codeOpen.blockFormat().topMargin() > 0.0 &&
              codeClose.blockFormat().bottomMargin() > 0.0,
          QStringLiteral("fenced-code boxes should have outer vertical spacing"));
    check(editor.toPlainText() == paddedCodeSource &&
              !editor.document()->isUndoAvailable(),
          QStringLiteral("code-box padding must preserve source and undo history"));

    const QString tableSource = QStringLiteral(
        "| Name  | Score |\n| :---- | ----: |\n| Ada   |    10 |\n"
        "| Grace |     9 |\nafter table");
    editor.setPlainText(tableSource);
    editor.moveCursor(QTextCursor::End);
    const QTextBlock tableHeader = editor.document()->findBlockByNumber(0);
    const QTextBlock tableBody = editor.document()->findBlockByNumber(2);
    settleLayout(editor, tableBody);
    const QTextCharFormat headerCell = formatAt(tableHeader, 2);
    const QTextCharFormat bodyCell = formatAt(tableBody, 2);
    check(headerCell.fontWeight() > bodyCell.fontWeight(),
          QStringLiteral("table headers should be visually stronger than data"));
    check(headerCell.fontStyleHint() == QFont::Monospace &&
              bodyCell.fontStyleHint() == QFont::Monospace,
          QStringLiteral("table cells should retain a cross-platform monospace grid"));
    check(editor.toPlainText() == tableSource &&
              !editor.document()->isUndoAvailable(),
          QStringLiteral("table preview must preserve source and undo history"));

    // Painted interaction affordances and their hit tests share one geometry
    // source. A rendered task checkbox should toggle at the same pixel where it
    // is drawn, even when the raw marker is concealed.
    editor.setPlainText(QStringLiteral("- [ ] geometry task\nother line"));
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    QTextCursor taskMarker(editor.document()->firstBlock());
    const QRect taskCell = editor.cursorRect(taskMarker);
    clickEditor(editor, QPoint(taskCell.left() + 5, taskCell.center().y()));
    check(editor.document()->firstBlock().text().startsWith(
              QStringLiteral("- [x]")),
          QStringLiteral("task checkbox paint and hit geometry should agree"));

    // Fold hit testing uses the exact block viewport rectangle used to paint its
    // arrow; clicking the visual gutter therefore folds and unfolds reliably.
    editor.setPlainText(QStringLiteral("# Fold me\ninside\n# Next"));
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    const QTextBlock foldHeading = editor.document()->firstBlock();
    const QPoint foldPoint(5, editor.cursorRect(QTextCursor(foldHeading)).center().y());
    clickEditor(editor, foldPoint);
    check(!editor.document()->findBlockByNumber(1).isVisible(),
          QStringLiteral("clicking the painted fold gutter should collapse it"));
    clickEditor(editor, foldPoint);
    check(editor.document()->findBlockByNumber(1).isVisible(),
          QStringLiteral("clicking the same fold gutter should expand it"));

    // Link hit testing now uses the same wrapped-range rectangles as Quick Jump.
    // Clicking blank space after the token must not inherit the nearest cursor.
    editor.resize(230, 220);
    const QString geometryLinkSource = QStringLiteral(
        "enough leading words to wrap [[Geometry Target]]\nother line");
    editor.setPlainText(geometryLinkSource);
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    QString geometryJump;
    QObject::connect(&editor, &MarkdownEditor::linkClicked,
                     [&geometryJump](const QString &target) {
                         geometryJump = target;
                     });
    const auto geometryMatch =
        QRegularExpression(QStringLiteral("\\[\\[([^]]+)\\]\\]"))
            .match(editor.document()->firstBlock().text());
    QTextCursor linkText(editor.document()->firstBlock());
    linkText.setPosition(editor.document()->firstBlock().position() +
                         geometryMatch.capturedStart(1));
    const QRect linkCell = editor.cursorRect(linkText);
    clickEditor(editor, linkCell.center(), Qt::ControlModifier);
    check(geometryJump == QStringLiteral("Geometry Target"),
          QStringLiteral("wrapped link geometry should remain clickable"));
    geometryJump.clear();
    clickEditor(editor,
                QPoint(editor.viewport()->width() - 3, linkCell.center().y()),
                Qt::ControlModifier);
    check(geometryJump.isEmpty(),
          QStringLiteral("blank trailing space should not activate a nearby link"));
    editor.resize(250, 220);

    // Image blocks derive their height from real metadata and viewport limits.
    // The source line collapses back to ordinary text height while it is edited.
    QTemporaryDir mediaDir;
    check(mediaDir.isValid(), QStringLiteral("media test directory should exist"));
    const QString wideImagePath = mediaDir.filePath(QStringLiteral("wide.png"));
    QImage wideImage(1200, 600, QImage::Format_ARGB32_Premultiplied);
    wideImage.fill(QColor(0x2b, 0xbf, 0x74));
    check(wideImage.save(wideImagePath),
          QStringLiteral("media test image should be writable"));
    editor.resize(320, 240);
    editor.setImagePaths(mediaDir.path(), mediaDir.path());
    const QString imageSource =
        QStringLiteral("![Wide preview](wide.png)\nafter image");
    editor.setPlainText(imageSource);
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    const QTextBlock imageBlock = editor.document()->firstBlock();
    const qreal compactImageHeight = imageBlock.blockFormat().lineHeight();
    check(imageBlock.blockFormat().lineHeightType() ==
              QTextBlockFormat::FixedHeight &&
              compactImageHeight > 100.0 &&
              compactImageHeight <= editor.viewport()->height() * 0.62 + 25.0,
          QStringLiteral("image preview should respect aspect ratio and viewport cap"));
    editor.resize(520, 600);
    QApplication::processEvents();
    const qreal roomyImageHeight = imageBlock.blockFormat().lineHeight();
    check(roomyImageHeight > compactImageHeight,
          QStringLiteral("image preview should grow when viewport bounds allow"));
    QTextCursor editImage(imageBlock);
    editor.setTextCursor(editImage);
    QApplication::processEvents();
    check(imageBlock.blockFormat().lineHeightType() ==
              QTextBlockFormat::SingleHeight,
          QStringLiteral("active image source should return to normal text height"));
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    editor.undo(); // skips any internal paragraph-geometry command
    QApplication::processEvents();
    check(imageBlock.blockFormat().lineHeightType() ==
              QTextBlockFormat::FixedHeight &&
              editor.toPlainText() == imageSource,
          QStringLiteral("image layout transitions must preserve source and "
                         "stay invisible to user undo"));

    editor.setPlainText(QStringLiteral("![Missing](missing.png)\nafter"));
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    check(editor.document()->firstBlock().blockFormat().lineHeight() >= 100.0,
          QStringLiteral("missing images should reserve a readable fallback card"));

    // The explicit inline baseline moves the rendered box with the surrounding
    // shaped text baseline and participates in the pixmap cache key.
    QImage baselineHigh(180, 70, QImage::Format_ARGB32_Premultiplied);
    QImage baselineLow(180, 70, QImage::Format_ARGB32_Premultiplied);
    baselineHigh.fill(Qt::transparent);
    baselineLow.fill(Qt::transparent);
    const QFont inlineMathFont = MathRender::mathFont(editor.font(), false);
    {
        QPainter painter(&baselineHigh);
        MathRender::paint(painter, QRectF(0, 0, 180, 70),
                          QStringLiteral("x^2"), inlineMathFont, Qt::white,
                          MathRender::Align::Inline, 25.0);
    }
    {
        QPainter painter(&baselineLow);
        MathRender::paint(painter, QRectF(0, 0, 180, 70),
                          QStringLiteral("x^2"), inlineMathFont, Qt::white,
                          MathRender::Align::Inline, 45.0);
    }
    check(firstOpaqueRow(baselineLow) > firstOpaqueRow(baselineHigh),
          QStringLiteral("inline math should honor the surrounding text baseline"));
    const QSizeF measuredOnce =
        MathRender::measure(QStringLiteral("\\frac{a}{b}"), inlineMathFont);
    const QSizeF measuredAgain =
        MathRender::measure(QStringLiteral("\\frac{a}{b}"), inlineMathFont);
    check(measuredOnce == measuredAgain && measuredOnce.height() > 0.0,
          QStringLiteral("math measurement cache should be stable"));
    editor.resize(250, 220);

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
    const QTextBlock fencedQuoteOpen = editor.document()->firstBlock();
    settleLayout(editor, fencedQuote);
    check(fencedQuote.blockFormat().leftMargin() ==
              fencedQuoteOpen.blockFormat().leftMargin(),
          QStringLiteral("quote-looking fenced code should use only uniform "
                         "code-box padding"));

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

    editor.setPlainText(QStringLiteral("item"));
    editor.moveCursor(QTextCursor::Start);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Minus, Qt::NoModifier,
            QStringLiteral("-"));
    sendKey(editor, QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier,
            QStringLiteral(" "));
    QApplication::processEvents();
    check(editor.toPlainText() == QStringLiteral("- item"),
          QStringLiteral("typing a marker should create a list item"));
    editor.undo();
    check(editor.toPlainText() == QStringLiteral("item"),
          QStringLiteral("one undo should remove a typed list marker despite "
                         "visual layout changes"));
    editor.redo();
    check(editor.toPlainText() == QStringLiteral("- item"),
          QStringLiteral("one redo should restore source while skipping "
                         "visual-only layout commands"));

    // Read Mode removes the caret, rejects editing keys, and turns plain arrow
    // navigation into viewport scrolling without relocating the text cursor.
    QStringList readingLines{
        QStringLiteral("# Rendered heading"),
        QStringLiteral("A **bold** paragraph with [[Target|wiki label]] and "
                       "[site](https://example.com), plus $x^2$."),
        QStringLiteral("> A quoted paragraph"),
        QStringLiteral("- A list item"),
        QStringLiteral("- [x] A completed task"), QStringLiteral("---"),
        QStringLiteral("![Wide preview](wide.png)"),
        QStringLiteral("| Name | Value | Formula |"),
        QStringLiteral("| :--- | ---: | :---: |"),
        QStringLiteral("| Alpha | 10 | `x|y` |"),
        QStringLiteral("| Beta | **20** | $n^2$ |"),
        QStringLiteral("```cpp"),
        QStringLiteral("const int answer = 42;"), QStringLiteral("```"),
        QStringLiteral("$$ E = mc^2 $$")};
    for (int i = 0; i < 80; ++i)
        readingLines << QStringLiteral("Reading line %1").arg(i);
    const QString readingSource = readingLines.join(QLatin1Char('\n'));
    editor.setPlainText(readingSource);
    editor.resize(300, 180);
    QTextDocument *const sourceDocument = editor.document();
    QTextCursor readingCursor(sourceDocument);
    readingCursor.setPosition(readingSource.indexOf(QStringLiteral("bold")));
    editor.setTextCursor(readingCursor);
    const int sourceCursorBeforeRead = editor.textCursor().position();
    editor.setReadMode(true);
    QApplication::processEvents();
    check(editor.readMode() && editor.isReadOnly(),
          QStringLiteral("Read Mode should make the editor read-only"));
    check(editor.cursorWidth() == 0,
          QStringLiteral("Read Mode should hide the text caret"));
    check(editor.sourceDocument() == sourceDocument &&
              editor.document() != sourceDocument,
          QStringLiteral("Read Mode should display a separate document while "
                         "retaining the Markdown source document"));
    const QString renderedReading = editor.document()->toPlainText();
    check(renderedReading.contains(QStringLiteral("Rendered heading")) &&
              renderedReading.contains(QStringLiteral("bold")) &&
              renderedReading.contains(QStringLiteral("wiki label")) &&
              !renderedReading.contains(QStringLiteral("# Rendered")) &&
              !renderedReading.contains(QStringLiteral("**bold**")) &&
              !renderedReading.contains(QStringLiteral("[[Target")) &&
              !renderedReading.contains(QStringLiteral("```")) &&
              !renderedReading.contains(QStringLiteral("$x^2$")) &&
              !renderedReading.contains(QStringLiteral("$$")) &&
              !renderedReading.contains(QStringLiteral("| :---")),
          QStringLiteral("the Read Mode document should contain presentation "
                         "text without Markdown source markers"));
    const QTextBlock renderedHeading = editor.document()->firstBlock();
    QTextCursor renderedHeadingText(renderedHeading);
    renderedHeadingText.movePosition(QTextCursor::NextCharacter,
                                     QTextCursor::KeepAnchor);
    check(renderedHeadingText.charFormat().fontWeight() >= QFont::Bold &&
              renderedHeadingText.charFormat().fontPointSize() >
                  editor.font().pointSizeF(),
          QStringLiteral("the rendered document should retain heading visual "
                         "hierarchy without source markers"));

    bool sawImageObject = false;
    bool sawInlineMathObject = false;
    bool sawDisplayMathObject = false;
    bool sawRuleObject = false;
    bool sawCheckboxObject = false;
    bool sawCodeObject = false;
    QTextBlock imageObjectBlock;
    QTextBlock codeObjectBlock;
    QTextCharFormat codeObjectFormat;
    QTextTable *renderedTable = nullptr;
    for (QTextBlock block = editor.document()->firstBlock(); block.isValid();
         block = block.next()) {
        if (!renderedTable) {
            QTextCursor blockCursor(block);
            renderedTable = blockCursor.currentTable();
        }
        for (auto fragmentIt = block.begin(); !fragmentIt.atEnd(); ++fragmentIt) {
            const QTextFragment fragment = fragmentIt.fragment();
            if (!fragment.isValid())
                continue;
            const QTextCharFormat format = fragment.charFormat();
            switch (MarkdownReadObjectRenderer::kind(format)) {
            case MarkdownReadObjectRenderer::Kind::Image:
                sawImageObject = true;
                imageObjectBlock = block;
                break;
            case MarkdownReadObjectRenderer::Kind::InlineMath:
                sawInlineMathObject = true;
                break;
            case MarkdownReadObjectRenderer::Kind::DisplayMath:
                sawDisplayMathObject = true;
                break;
            case MarkdownReadObjectRenderer::Kind::Rule:
                sawRuleObject = true;
                break;
            case MarkdownReadObjectRenderer::Kind::Checkbox:
                sawCheckboxObject = true;
                break;
            case MarkdownReadObjectRenderer::Kind::CodeBlock:
                sawCodeObject = true;
                codeObjectBlock = block;
                codeObjectFormat = format;
                break;
            case MarkdownReadObjectRenderer::Kind::None:
                break;
            }
        }
    }
    check(sawImageObject && sawInlineMathObject && sawDisplayMathObject &&
              sawRuleObject && sawCheckboxObject && sawCodeObject,
          QStringLiteral("Read Mode should embed native image, math, rule, "
                         "checkbox, and code objects"));
    check(MarkdownReadObjectRenderer::codeText(codeObjectFormat) ==
              QStringLiteral("const int answer = 42;"),
          QStringLiteral("the code-card object should retain exact copyable "
                         "source without its Markdown fences"));
    check(renderedTable && renderedTable->rows() == 3 &&
              renderedTable->columns() == 3 &&
              renderedTable->format().headerRowCount() == 1,
          QStringLiteral("Read Mode should turn a Markdown header/separator/body "
                         "group into one semantic Qt table"));
    if (renderedTable) {
        check(renderedTable->cellAt(0, 0)
                      .firstCursorPosition()
                      .block()
                      .text() == QStringLiteral("Name") &&
                  renderedTable->cellAt(1, 2)
                          .firstCursorPosition()
                          .block()
                          .text() == QStringLiteral("x|y") &&
                  renderedTable->cellAt(2, 1)
                          .firstCursorPosition()
                          .block()
                          .text() == QStringLiteral("20"),
              QStringLiteral("semantic table cells should omit pipe/separator "
                             "syntax and preserve pipes inside code spans"));
        check(renderedTable->cellAt(0, 0)
                      .firstCursorPosition()
                      .blockFormat()
                      .alignment() == Qt::AlignLeft &&
                  renderedTable->cellAt(0, 1)
                          .firstCursorPosition()
                          .blockFormat()
                          .alignment() == Qt::AlignRight &&
                  renderedTable->cellAt(0, 2)
                          .firstCursorPosition()
                          .blockFormat()
                          .alignment() == Qt::AlignCenter,
              QStringLiteral("separator colons should become per-column table "
                             "alignment"));
        QTextCursor headerText =
            renderedTable->cellAt(0, 0).firstCursorPosition();
        headerText.movePosition(QTextCursor::NextCharacter,
                                QTextCursor::KeepAnchor);
        check(headerText.charFormat().fontWeight() >= QFont::DemiBold &&
                  renderedTable->format().borderCollapse(),
              QStringLiteral("semantic table headers should be emphasized and "
                             "use one collapsed grid"));
    }
    if (imageObjectBlock.isValid()) {
        settleLayout(editor, imageObjectBlock);
        check(editor.document()->documentLayout()
                      ->blockBoundingRect(imageObjectBlock)
                      .height() > 70.0,
              QStringLiteral("a Read Mode image object should reserve a "
                             "metadata-derived visual area"));
    }

    // Force a real widget render so each registered QTextObjectInterface paint
    // path runs under the offscreen regression test.
    const QSizeF readDocumentSize = editor.document()->size();
    QImage readModeRender(qMax(1, qCeil(readDocumentSize.width())),
                          qMax(1, qCeil(readDocumentSize.height())),
                          QImage::Format_ARGB32_Premultiplied);
    readModeRender.fill(Qt::transparent);
    {
        QPainter painter(&readModeRender);
        editor.document()->drawContents(&painter);
    }
    bool paintedLocalImage = false;
    const QRgb expectedImagePixel = QColor(0x2b, 0xbf, 0x74).rgba();
    for (int y = 0; y < readModeRender.height() && !paintedLocalImage; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(
            readModeRender.constScanLine(y));
        for (int x = 0; x < readModeRender.width(); ++x) {
            if (line[x] == expectedImagePixel) {
                paintedLocalImage = true;
                break;
            }
        }
    }
    check(!readModeRender.isNull() && paintedLocalImage,
          QStringLiteral("native Read Mode objects should paint through the Qt "
                         "document layout and load vault-local image content"));

    if (codeObjectBlock.isValid()) {
        settleLayout(editor, codeObjectBlock);
        const QRectF documentCodeRect = editor.document()->documentLayout()
                                            ->blockBoundingRect(codeObjectBlock);
        editor.verticalScrollBar()->setValue(qRound(documentCodeRect.top()));
        QApplication::processEvents();
        QTextLayout *layout = codeObjectBlock.layout();
        if (layout && layout->lineCount() > 0) {
            const QTextLine line = layout->lineAt(0);
            QTextCursor origin(codeObjectBlock);
            const qreal xOffset =
                editor.cursorRect(origin).left() - line.cursorToX(0);
            const QRectF objectRect(
                xOffset + line.cursorToX(0),
                documentCodeRect.top() - editor.verticalScrollBar()->value() +
                    line.y(),
                qAbs(line.cursorToX(1) - line.cursorToX(0)), line.height());
            QApplication::clipboard()->clear();
            clickEditor(editor,
                        MarkdownReadObjectRenderer::codeCopyButtonRect(objectRect)
                            .center()
                            .toPoint());
            check(QApplication::clipboard()->text() ==
                      QStringLiteral("const int answer = 42;"),
                  QStringLiteral("the Read Mode code-card Copy control should "
                                 "copy the unfenced source"));
        }
    }
    check(editor.toPlainText() == readingSource &&
              editor.sourceDocument()->toPlainText() == readingSource,
          QStringLiteral("Read Mode presentation must not replace the Markdown "
                         "source"));
    check(!editor.document()->isUndoRedoEnabled(),
          QStringLiteral("the Read Mode presentation document should have undo "
                         "recording disabled"));
    check(!editor.document()->isModified(),
          QStringLiteral("installing the Read Mode presentation document should "
                         "not mark it modified"));

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
    const int readScrollBeforeExit = editor.verticalScrollBar()->value();
    editor.setReadMode(false);
    QApplication::processEvents();
    check(!editor.isReadOnly() && editor.cursorWidth() > 0 &&
              editor.document() == sourceDocument,
          QStringLiteral("leaving Read Mode should restore the source document, "
                         "editing, and caret"));
    check(editor.textCursor().position() == sourceCursorBeforeRead &&
              editor.toPlainText() == readingSource,
          QStringLiteral("leaving Read Mode should restore the exact source "
                         "cursor and Markdown text"));
    check(readScrollBeforeExit == 0 || editor.verticalScrollBar()->value() > 0,
          QStringLiteral("leaving Read Mode should preserve reading progress "
                         "instead of jumping to the top"));

    // Loading another note while Read Mode remains enabled updates the hidden
    // source and rebuilds the presentation document, without ever exposing the
    // source document as the editor's active surface.
    const QString replacedWhileReading =
        QStringLiteral("## Rebuilt note\nText with ~~old~~ and `code`.");
    editor.setReadMode(true);
    QTextDocument *const renderedBeforeReplace = editor.document();
    editor.setPlainText(replacedWhileReading);
    QApplication::processEvents();
    check(editor.document() == renderedBeforeReplace &&
              editor.document() != editor.sourceDocument() &&
              editor.toPlainText() == replacedWhileReading &&
              editor.document()->toPlainText().contains(
                  QStringLiteral("Rebuilt note")) &&
              !editor.document()->toPlainText().contains(QStringLiteral("##")) &&
              !editor.document()->toPlainText().contains(QStringLiteral("~~")),
          QStringLiteral("setPlainText in Read Mode should replace source and "
                         "rebuild the separate presentation document"));
    editor.setReadMode(false);

    // Swapping documents must not discard the source document's undo stack.
    editor.setPlainText(QStringLiteral("undo survives"));
    editor.moveCursor(QTextCursor::End);
    sendKey(editor, QEvent::KeyPress, Qt::Key_X, Qt::NoModifier,
            QStringLiteral("x"));
    check(editor.sourceDocument()->isUndoAvailable(),
          QStringLiteral("typing should create source undo history before a "
                         "Read Mode swap"));
    const int cursorAfterTyping = editor.textCursor().position();
    editor.setReadMode(true);
    editor.setReadMode(false);
    check(editor.textCursor().position() == cursorAfterTyping &&
              editor.sourceDocument()->isUndoAvailable(),
          QStringLiteral("a Read Mode round trip should preserve source cursor "
                         "and undo availability"));
    editor.undo();
    check(editor.toPlainText() == QStringLiteral("undo survives"),
          QStringLiteral("source undo should still work after a Read Mode "
                         "document swap"));

    // QTextEdit normally owns and deletes its installed document. The source
    // and rendered documents use a neutral owner specifically so destruction
    // is also safe while the rendered one is still installed.
    {
        MarkdownEditor destroyedInReadMode;
        destroyedInReadMode.setPlainText(QStringLiteral("# Temporary note"));
        destroyedInReadMode.setReadMode(true);
    }

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
