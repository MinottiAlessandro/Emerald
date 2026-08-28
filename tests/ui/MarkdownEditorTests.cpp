#include "ui/MarkdownEditor.h"
#include "ui/AppTheme.h"
#include "ui/MarkdownCallout.h"
#include "ui/MarkdownHighlighter.h"
#include "ui/MarkdownReadObjectRenderer.h"
#include "ui/MarkdownReadRenderer.h"
#include "ui/MarkdownStyle.h"
#include "ui/MathRender.h"

#include "core/Perf.h"
#include "core/MascotSeed.h"
#include "core/SpellChecker.h"

#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
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

void checkCanScrollPastLastLine(MarkdownEditor &editor,
                                const QString &description) {
    const QTextBlock lastBlock = editor.document()->lastBlock();
    settleLayout(editor, lastBlock);
    editor.verticalScrollBar()->setValue(
        editor.verticalScrollBar()->maximum());
    QApplication::processEvents();

    const QRect lastLine = editor.cursorRect(QTextCursor(lastBlock));
    check(editor.verticalScrollBar()->maximum() > 0 &&
              lastLine.bottom() <= 0,
          description +
              QStringLiteral(" should let the final line scroll past the top "
                             "of the viewport (maximum %1, line bottom %2)")
                  .arg(editor.verticalScrollBar()->maximum())
                  .arg(lastLine.bottom()));
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

bool containsPixel(const QImage &image, QRgb expected) {
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line =
            reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (line[x] == expected)
                return true;
    }
    return false;
}

QRectF firstObjectViewportRect(MarkdownEditor &editor,
                               const QTextBlock &block) {
    if (!block.isValid() || !block.layout() || block.layout()->lineCount() == 0)
        return {};
    const QTextLine line = block.layout()->lineAt(0);
    QTextCursor origin(block);
    const qreal xOffset = editor.cursorRect(origin).left() - line.cursorToX(0);
    const QRectF documentRect =
        editor.document()->documentLayout()->blockBoundingRect(block);
    return QRectF(xOffset + line.cursorToX(0),
                  documentRect.top() - editor.verticalScrollBar()->value() +
                      line.y(),
                  qAbs(line.cursorToX(1) - line.cursorToX(0)), line.height());
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

void sendMouseRelease(MarkdownEditor &editor, const QPoint &position,
                      Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(QEvent::MouseButtonRelease, QPointF(position),
                      QPointF(editor.viewport()->mapToGlobal(position)),
                      Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(editor.viewport(), &event);
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
    QTemporaryDir applicationData;
    if (!applicationData.isValid())
        return 2;
    qputenv("XDG_DATA_HOME", applicationData.path().toUtf8());
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("EmeraldTests"));
    QCoreApplication::setApplicationName(QStringLiteral("MarkdownEditorTests"));

    MarkdownEditor editor;
    editor.resize(250, 220);
    editor.show();

    // A note with content shorter than the viewport still needs a full page of
    // trailing scroll room. Reproduce the common long-note to short-note
    // transition because Qt resets its natural range to zero in that case.
    {
        MarkdownEditor scrollEditor;
        scrollEditor.resize(320, 180);
        scrollEditor.show();
        QStringList longRows;
        for (int i = 0; i < 60; ++i)
            longRows.append(QStringLiteral("Long note line %1").arg(i));
        scrollEditor.setPlainText(longRows.join(QLatin1Char('\n')));
        settleLayout(scrollEditor, scrollEditor.document()->lastBlock());

        scrollEditor.setPlainText(QStringLiteral("First line\nLast line"));
        scrollEditor.moveCursor(QTextCursor::Start);
        checkCanScrollPastLastLine(scrollEditor, QStringLiteral("Edit Mode"));

        scrollEditor.verticalScrollBar()->setValue(0);
        scrollEditor.setReadMode(true);
        checkCanScrollPastLastLine(scrollEditor, QStringLiteral("Read Mode"));
    }

    // Both vault-search jumps and Find in Note use the same centred-match path.
    // The deferred second pass covers the layout transition after a match is
    // selected, while keeping the first positioning immediate.
    {
        MarkdownEditor searchEditor;
        searchEditor.resize(420, 180);
        searchEditor.show();
        QStringList rows;
        for (int i = 0; i < 90; ++i) {
            if (i == 38)
                rows.append(QStringLiteral("global centering target"));
            else if (i == 67)
                rows.append(QStringLiteral("in-note centering target"));
            else
                rows.append(QStringLiteral("ordinary search row %1").arg(i));
        }
        searchEditor.setPlainText(rows.join(QLatin1Char('\n')));
        settleLayout(searchEditor, searchEditor.document()->lastBlock());
        const auto cursorIsCentered = [&searchEditor] {
            return qAbs(searchEditor.cursorRect().center().y() -
                        searchEditor.viewport()->rect().center().y()) <=
                   searchEditor.fontMetrics().height();
        };

        searchEditor.jumpToMatch(QStringLiteral("global centering target"));
        QApplication::processEvents();
        check(searchEditor.textCursor().selectedText() ==
                  QStringLiteral("global centering target") &&
                  cursorIsCentered(),
              QStringLiteral("a vault-search jump should center its match"));

        searchEditor.moveCursor(QTextCursor::Start);
        check(searchEditor.findAndCenter(
                  QStringLiteral("in-note centering target")),
              QStringLiteral("Find in Note should locate its match"));
        QApplication::processEvents();
        check(searchEditor.textCursor().selectedText() ==
                  QStringLiteral("in-note centering target") &&
                  cursorIsCentered(),
              QStringLiteral("Find in Note should center its match"));

        QStringList readRows;
        for (int i = 0; i < 90; ++i) {
            if (i == 43)
                readRows.append(QStringLiteral("```cpp"));
            else if (i == 44)
                readRows.append(
                    QStringLiteral("const int read_search_target = 42;"));
            else if (i == 45)
                readRows.append(QStringLiteral("```"));
            else
                readRows.append(QStringLiteral("read search row %1").arg(i));
        }
        searchEditor.setReadMode(false);
        searchEditor.setPlainText(readRows.join(QLatin1Char('\n')));
        searchEditor.setReadMode(true);
        searchEditor.moveCursor(QTextCursor::Start);
        check(searchEditor.findAndCenter(
                  QStringLiteral("read_search_target")),
              QStringLiteral("Find in Note should inspect Read Mode code "
                             "blocks"));
        QApplication::processEvents();
        QTextCursor renderedCodeMatch(searchEditor.document());
        renderedCodeMatch.setPosition(
            searchEditor.textCursor().selectionStart());
        renderedCodeMatch.movePosition(QTextCursor::NextCharacter,
                                       QTextCursor::KeepAnchor);
        const QTextCharFormat renderedCodeFormat =
            renderedCodeMatch.charFormat();
        MarkdownReadObjectRenderer codePainter;
        QTextDocument codeDocument;
        codeDocument.setTextWidth(380.0);
        const QSizeF codeCardSize = codePainter.intrinsicSize(
            &codeDocument, 0, renderedCodeFormat);
        QImage codeCard(qCeil(codeCardSize.width()),
                        qCeil(codeCardSize.height()),
                        QImage::Format_ARGB32_Premultiplied);
        codeCard.fill(Qt::transparent);
        QPainter codeCardPainter(&codeCard);
        codePainter.drawObject(&codeCardPainter,
                               QRectF(QPointF(0, 0), codeCardSize),
                               &codeDocument, 0, renderedCodeFormat);
        codeCardPainter.end();
        const QColor selectionColor =
            searchEditor.palette().color(QPalette::Highlight);
        bool codeSelectionPainted = false;
        for (int y = 0; y < codeCard.height() && !codeSelectionPainted; ++y)
            for (int x = 0; x < codeCard.width(); ++x)
                if (codeCard.pixelColor(x, y) == selectionColor) {
                    codeSelectionPainted = true;
                    break;
                }
        check(searchEditor.sourceTextCursor().selectedText() ==
                  QStringLiteral("read_search_target") &&
                  MarkdownReadObjectRenderer::kind(
                      renderedCodeFormat) ==
                      MarkdownReadObjectRenderer::Kind::CodeBlock &&
                  MarkdownReadObjectRenderer::codeSearchMatchStart(
                      renderedCodeFormat) == 10 &&
                  MarkdownReadObjectRenderer::codeSearchMatchLength(
                      renderedCodeFormat) == 18 &&
                  codeSelectionPainted &&
                  cursorIsCentered(),
              QStringLiteral("a Read Mode code match should retain its exact "
                             "source selection, paint a visible highlight, "
                             "and center the code card"));
        const int codeObjectPosition = renderedCodeMatch.selectionStart();
        searchEditor.moveCursor(QTextCursor::End);
        QApplication::processEvents();
        QTextCursor clearedCodeMatch(searchEditor.document());
        clearedCodeMatch.setPosition(codeObjectPosition);
        clearedCodeMatch.movePosition(QTextCursor::NextCharacter,
                                      QTextCursor::KeepAnchor);
        check(MarkdownReadObjectRenderer::codeSearchMatchStart(
                  clearedCodeMatch.charFormat()) < 0,
              QStringLiteral("moving away from a Read Mode code result should "
                             "clear its transient highlight"));
    }

    // Theme changes refresh both the cached live highlighter formats and the
    // separately rendered Read Mode document without altering Markdown text.
    {
        AppTheme::apply(app, AppTheme::Id::Light);
        MarkdownEditor themedEditor;
        themedEditor.resize(320, 180);
        themedEditor.show();
        themedEditor.setPlainText(
            QStringLiteral("# Themed heading\nordinary text"));
        themedEditor.moveCursor(QTextCursor::End);
        const QTextBlock sourceHeading = themedEditor.document()->firstBlock();
        settleLayout(themedEditor, sourceHeading);
        check(formatAt(sourceHeading, 3).foreground().color() ==
                  AppTheme::color(QColor(QStringLiteral("#d7eee2"))),
              QStringLiteral("Emerald Light recolors Edit Mode Markdown"));

        themedEditor.setReadMode(true);
        const QTextCursor readHeading =
            themedEditor.document()->find(QStringLiteral("Themed heading"));
        settleLayout(themedEditor, readHeading.block());
        check(readHeading.charFormat().foreground().color() ==
                  AppTheme::color(QColor(QStringLiteral("#e3f5ec"))),
              QStringLiteral("Emerald Light recolors Read Mode Markdown"));

        AppTheme::apply(app, AppTheme::Id::Dark);
        themedEditor.applyTheme();
        const QTextCursor restoredHeading =
            themedEditor.document()->find(QStringLiteral("Themed heading"));
        check(restoredHeading.charFormat().foreground().color() ==
                  QColor(QStringLiteral("#e3f5ec")) &&
                  themedEditor.sourceDocument()->toPlainText() ==
                      QStringLiteral("# Themed heading\nordinary text"),
              QStringLiteral("switching themes rebuilds presentation only"));
    }

    // The bundled English dictionary loads without a network connection at
    // runtime, and the Markdown scanner exposes only human-language ranges.
    QString spellError;
    check(editor.setSpellCheckingLanguage(QStringLiteral("en_US"), &spellError),
          QStringLiteral("bundled English dictionary should load: %1")
              .arg(spellError));
    editor.setSpellCheckingEnabled(true);
    editor.setSpellCheckingOptions(true, true);
    const QList<SpellChecker::WordRange> spellRanges =
        SpellChecker::wordsInMarkdown(QStringLiteral(
            "Prose `codde` [[Targget]] [[Note|aliass]] "
            "[labell](https://bad.example/misstake) $formulla$ <tag> "
            "![altt](image.png) ![reff][asset] ![[photoo.png|120]] "
            "<!-- commmentword -->"));
    QStringList spellWords;
    for (const auto &range : spellRanges)
        spellWords.append(range.word);
    check(spellWords.contains(QStringLiteral("Prose")) &&
              spellWords.contains(QStringLiteral("aliass")) &&
              spellWords.contains(QStringLiteral("labell")) &&
              !spellWords.contains(QStringLiteral("codde")) &&
              !spellWords.contains(QStringLiteral("Targget")) &&
              !spellWords.contains(QStringLiteral("misstake")) &&
              !spellWords.contains(QStringLiteral("formulla")) &&
              !spellWords.contains(QStringLiteral("tag")) &&
              !spellWords.contains(QStringLiteral("altt")) &&
              !spellWords.contains(QStringLiteral("reff")) &&
              !spellWords.contains(QStringLiteral("photoo")) &&
              !spellWords.contains(QStringLiteral("commmentword")),
          QStringLiteral("spell ranges should include prose and visible labels "
                         "but exclude Markdown targets, code, math, URLs, and HTML"));
    check(SpellChecker::wordsInMarkdown(
              QStringLiteral("[asset]: image-name.png \"asset title\""))
              .isEmpty(),
          QStringLiteral("spell ranges should exclude image reference "
                         "definitions"));

    const QString spellingSource = QStringLiteral(
        "A correct word and **wrod** plus `codde`, [[Targget]], "
        "[[Note|aliass]], and [labell](https://example.com/misstake).\n"
        "trailing line");
    editor.setPlainText(spellingSource);
    editor.moveCursor(QTextCursor::End);
    const QTextBlock spellingBlock = editor.document()->firstBlock();
    settleLayout(editor, spellingBlock);
    const QString spellingText = spellingBlock.text();
    auto underlined = [&spellingBlock, &spellingText](const QString &word) {
        return highlighterFormatAt(spellingBlock, spellingText.indexOf(word))
                   .underlineStyle() == QTextCharFormat::SpellCheckUnderline;
    };
    check(underlined(QStringLiteral("wrod")) &&
              underlined(QStringLiteral("aliass")) &&
              underlined(QStringLiteral("labell")) &&
              !underlined(QStringLiteral("correct")) &&
              !underlined(QStringLiteral("codde")) &&
              !underlined(QStringLiteral("Targget")) &&
              !underlined(QStringLiteral("misstake")),
          QStringLiteral("only misspelled visible prose should be underlined"));
    const QTextCharFormat styledMisspelling = highlighterFormatAt(
        spellingBlock, spellingText.indexOf(QStringLiteral("wrod")));
    check(styledMisspelling.fontWeight() >= QFont::Bold &&
              styledMisspelling.underlineStyle() ==
                  QTextCharFormat::SpellCheckUnderline,
          QStringLiteral("spell underline should preserve Markdown bold styling"));

    QTextCursor typing(spellingBlock);
    typing.setPosition(spellingBlock.position() +
                       spellingText.indexOf(QStringLiteral("wrod")) + 2);
    editor.setTextCursor(typing);
    settleLayout(editor, spellingBlock);
    check(!underlined(QStringLiteral("wrod")),
          QStringLiteral("the word under the typing caret should not flash red"));
    editor.moveCursor(QTextCursor::End);
    settleLayout(editor, spellingBlock);
    check(underlined(QStringLiteral("wrod")),
          QStringLiteral("a misspelling should appear when the caret leaves it"));

    const QStringList suggestions =
        editor.spellingSuggestions(QStringLiteral("recieve"));
    check(!suggestions.isEmpty(),
          QStringLiteral("Hunspell should provide on-demand corrections"));
    editor.setPlainText(QStringLiteral("Please recieve this\nnext"));
    editor.moveCursor(QTextCursor::End);
    QTextCursor misspellingPoint(editor.document()->firstBlock());
    misspellingPoint.setPosition(
        editor.document()->firstBlock().position() + 9);
    const QPoint misspellingPosition =
        editor.cursorRect(misspellingPoint).center();
    check(editor.misspelledWordAt(misspellingPosition) ==
              QStringLiteral("recieve"),
          QStringLiteral("context lookup should identify the misspelled word"));
    if (!suggestions.isEmpty()) {
        check(editor.replaceMisspelledWordAt(
                  misspellingPosition, QStringLiteral("recieve"),
                  suggestions.first()) &&
                  editor.toPlainText().contains(suggestions.first()),
              QStringLiteral("choosing a spelling suggestion should replace only "
                             "the source word"));
    }

    editor.setPlainText(QStringLiteral("A perssonalword here\nnext"));
    editor.moveCursor(QTextCursor::End);
    const QTextBlock personalBlock = editor.document()->firstBlock();
    settleLayout(editor, personalBlock);
    const int personalOffset = personalBlock.text().indexOf(
        QStringLiteral("perssonalword"));
    check(highlighterFormatAt(personalBlock, personalOffset).underlineStyle() ==
              QTextCharFormat::SpellCheckUnderline,
          QStringLiteral("unknown personal word starts misspelled"));
    check(editor.addToPersonalDictionary(QStringLiteral("perssonalword"),
                                         &spellError),
          QStringLiteral("personal word should persist: %1").arg(spellError));
    settleLayout(editor, personalBlock);
    check(highlighterFormatAt(personalBlock, personalOffset).underlineStyle() !=
              QTextCharFormat::SpellCheckUnderline,
          QStringLiteral("personal words should be accepted immediately"));
    SpellChecker reloadedPersonalDictionary;
    check(reloadedPersonalDictionary.setLanguage(QStringLiteral("en_US"),
                                                  &spellError) &&
              reloadedPersonalDictionary.isCorrect(
                  QStringLiteral("perssonalword")),
          QStringLiteral("personal words should survive a fresh checker instance"));

    editor.setPlainText(QStringLiteral("sesssionword here\nnext"));
    editor.moveCursor(QTextCursor::End);
    const QTextBlock sessionBlock = editor.document()->firstBlock();
    settleLayout(editor, sessionBlock);
    const int sessionOffset =
        sessionBlock.text().indexOf(QStringLiteral("sesssionword"));
    check(highlighterFormatAt(sessionBlock, sessionOffset).underlineStyle() ==
              QTextCharFormat::SpellCheckUnderline,
          QStringLiteral("session fixture starts misspelled"));
    editor.ignoreSpellingForSession(QStringLiteral("sesssionword"));
    settleLayout(editor, sessionBlock);
    check(highlighterFormatAt(sessionBlock, sessionOffset).underlineStyle() !=
              QTextCharFormat::SpellCheckUnderline,
          QStringLiteral("session ignore should remove every matching underline"));
    check(!reloadedPersonalDictionary.isCorrect(
              QStringLiteral("sesssionword")),
          QStringLiteral("session ignored words should not persist globally"));

    const QList<SpellLanguage> spellingLanguages =
        SpellChecker::availableLanguages();
    check(spellingLanguages.size() == 5 &&
              SpellChecker::installedLanguages().contains(QStringLiteral("en_US")),
          QStringLiteral("language catalog should expose bundled English and "
                         "four optional verified packs"));
    for (const SpellLanguage &language : spellingLanguages) {
        if (language.builtIn)
            continue;
        check(language.affix.url.startsWith(QStringLiteral(
                  "https://github.com/MinottiAlessandro/Emerald/releases/download/"
                  "spell-dictionaries-v")) &&
                  language.dictionary.url.startsWith(QStringLiteral(
                      "https://github.com/MinottiAlessandro/Emerald/releases/"
                      "download/spell-dictionaries-v")) &&
                  language.notice.url.startsWith(QStringLiteral(
                      "https://github.com/MinottiAlessandro/Emerald/releases/"
                      "download/spell-dictionaries-v")),
              QStringLiteral("optional dictionaries should use versioned "
                             "Emerald-hosted assets"));
    }
    const QHash<QString, QString> optionalLanguageWords = {
        {QStringLiteral("it_IT"), QStringLiteral("ciao")},
        {QStringLiteral("de_DE"), QStringLiteral("Straße")},
        {QStringLiteral("fr_FR"), QStringLiteral("bonjour")},
        {QStringLiteral("es_ES"), QStringLiteral("hola")}};
    for (auto it = optionalLanguageWords.cbegin();
         it != optionalLanguageWords.cend(); ++it) {
        const QString root = QString::fromUtf8(EMERALD_SOURCE_DIR) +
                             QStringLiteral("/packaging/spelling-packs/v1/") +
                             it.key() + QLatin1Char('/');
        auto readPackFile = [&root](const QString &name) {
            QFile file(root + name);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
        };
        const QByteArray affix =
            readPackFile(it.key() + QStringLiteral(".aff"));
        const QByteArray dictionary =
            readPackFile(it.key() + QStringLiteral(".dic"));
        const QByteArray notice = readPackFile(QStringLiteral("NOTICE.txt"));
        check(SpellChecker::installLanguage(it.key(), affix, dictionary, notice,
                                            &spellError),
              QStringLiteral("reviewed %1 release pack should install: %2")
                  .arg(it.key(), spellError));
        SpellChecker optionalChecker;
        check(optionalChecker.setLanguage(it.key(), &spellError) &&
                  optionalChecker.isCorrect(it.value()),
              QStringLiteral("reviewed %1 dictionary should load and recognize "
                             "its smoke-test word: %2")
                  .arg(it.key(), spellError));
        if (it.key() == QLatin1String("it_IT")) {
            {
                SpellChecker stackedChecker;
                check(stackedChecker.setLanguages(
                          {QStringLiteral("en_US"), QStringLiteral("it_IT")},
                          &spellError) &&
                          stackedChecker.languages() ==
                              QStringList{QStringLiteral("en_US"),
                                          QStringLiteral("it_IT")} &&
                          stackedChecker.isCorrect(QStringLiteral("hello")) &&
                          stackedChecker.isCorrect(
                              QStringLiteral("ghiandaia")) &&
                          !stackedChecker.isCorrect(
                              QStringLiteral("zzzxxyynotaword")),
                      QStringLiteral("stacked dictionaries should accept words "
                                     "from every selected language: %1")
                          .arg(spellError));
            }
            QFile installedDictionary(
                SpellChecker::dictionaryRoot() +
                QStringLiteral("/it_IT/it_IT.dic"));
            check(installedDictionary.open(QIODevice::WriteOnly |
                                           QIODevice::Truncate) &&
                      installedDictionary.write("corrupt\n") == 8,
                  QStringLiteral("optional-pack update fixture should corrupt "
                                 "the installed copy"));
            installedDictionary.close();
            check(!SpellChecker::isLanguageInstalled(it.key()) &&
                      SpellChecker::languageNeedsUpdate(it.key()),
                  QStringLiteral("a stale or corrupt optional pack should be "
                                 "reported as needing an update"));
            check(SpellChecker::installLanguage(it.key(), affix, dictionary,
                                                notice, &spellError) &&
                      SpellChecker::isLanguageInstalled(it.key()) &&
                      !SpellChecker::languageNeedsUpdate(it.key()),
                  QStringLiteral("a verified pack should safely replace a stale "
                                 "installation: %1")
                      .arg(spellError));
        }
        check(SpellChecker::removeLanguage(it.key(), &spellError),
              QStringLiteral("test %1 pack should be removable: %2")
                  .arg(it.key(), spellError));
    }
    QByteArray invalidPart("not a dictionary");
    check(!SpellChecker::installLanguage(QStringLiteral("it_IT"), invalidPart,
                                         invalidPart, invalidPart, &spellError) &&
              !SpellChecker::isLanguageInstalled(QStringLiteral("it_IT")),
          QStringLiteral("invalid optional packs must fail verification without "
                         "becoming installed"));
    check(!SpellChecker::removeLanguage(QStringLiteral("en_US"), &spellError),
          QStringLiteral("the bundled English baseline cannot be removed"));

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
    check(MarkdownHighlighter::inlinePreviewColumnCount(
              QStringLiteral("A ![Alt|120](image.png)")) == 5,
          QStringLiteral("table width should measure an image description "
                         "without its source or dimensions"));

    editor.resize(900, 220);
    QApplication::processEvents();
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

    // Prettification is all-or-nothing when its padded output would be wider
    // than the editor. A compact source table remains compact instead of being
    // expanded into wrapped rows that no longer read as a grid.
    editor.resize(220, 220);
    QApplication::processEvents();
    const QString tooWideTable = QStringLiteral(
        "| A very long header that cannot fit | B |\n"
        "| --- | --- |\n"
        "| x | y |\n"
        "after narrow table");
    editor.setPlainText(tooWideTable);
    QTextCursor narrowTableCell(editor.document()->findBlockByNumber(2));
    editor.setTextCursor(narrowTableCell);
    QTextCursor afterNarrowTable(editor.document()->findBlockByNumber(3));
    editor.setTextCursor(afterNarrowTable);
    QApplication::processEvents();
    check(editor.toPlainText() == tooWideTable,
          QStringLiteral("a table whose formatted row exceeds the editor width "
                         "should remain completely unformatted"));

    editor.resize(250, 220);
    QApplication::processEvents();

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
              .foreground()
              .color() == QColor(QStringLiteral("#7ee0b0")),
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
    const QTextBlock tableSeparator = editor.document()->findBlockByNumber(1);
    const QTextBlock tableBody = editor.document()->findBlockByNumber(2);
    settleLayout(editor, tableBody);
    const QTextCharFormat headerCell = formatAt(tableHeader, 2);
    const QTextCharFormat bodyCell = formatAt(tableBody, 2);
    check(headerCell.fontWeight() == bodyCell.fontWeight() &&
              formatAt(tableHeader, 0).isEmpty() &&
              formatAt(tableSeparator, 3).isEmpty(),
          QStringLiteral("Edit Mode should leave table headers, pipes, and "
                         "separator source unskinned"));
    QImage inactiveTableRender(editor.viewport()->size(),
                               QImage::Format_ARGB32_Premultiplied);
    inactiveTableRender.fill(Qt::transparent);
    editor.viewport()->render(&inactiveTableRender);
    const QRgb tableSurfacePixel = QColor(0x12, 0x1d, 0x18).rgba();
    check(!containsPixel(inactiveTableRender, tableSurfacePixel),
          QStringLiteral("an inactive Edit Mode table should not paint a skin"));
    QTextCursor activeTableHeader(tableHeader);
    editor.setTextCursor(activeTableHeader);
    settleLayout(editor, tableHeader);
    check(formatAt(tableHeader, 0).isEmpty() &&
              formatAt(tableSeparator, 3).isEmpty() &&
              formatAt(tableBody, 0).isEmpty(),
          QStringLiteral("an active Edit Mode table should remain plain source"));
    QImage activeTableRender(editor.viewport()->size(),
                             QImage::Format_ARGB32_Premultiplied);
    activeTableRender.fill(Qt::transparent);
    editor.viewport()->render(&activeTableRender);
    check(!containsPixel(activeTableRender, tableSurfacePixel),
          QStringLiteral("an active Edit Mode table should not paint a skin"));
    check(editor.toPlainText() == tableSource &&
              !editor.document()->isUndoAvailable(),
          QStringLiteral("table preview must preserve source and undo history"));

    // A fresh header owns Enter at every caret position, not only at its end.
    // It creates the separator + first data row and starts in its first cell.
    const QString freshHeader = QStringLiteral("| Name | Score |");
    for (int position = 0; position <= freshHeader.size(); ++position) {
        editor.setPlainText(freshHeader);
        QTextCursor inHeader(editor.document());
        inHeader.setPosition(position);
        editor.setTextCursor(inHeader);
        sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                QStringLiteral("\n"));
        const QTextBlock firstDataRow =
            editor.document()->findBlockByNumber(2);
        const QList<int> dataPipes =
            MarkdownHighlighter::tablePipePositions(firstDataRow.text());
        check(editor.document()->blockCount() == 3 &&
                  editor.document()->findBlockByNumber(1).text().contains(
                      QStringLiteral("---")) &&
                  editor.textCursor().blockNumber() == 2 &&
                  dataPipes.size() == 3 &&
                  editor.textCursor().positionInBlock() >
                      dataPipes.at(0) &&
                  editor.textCursor().positionInBlock() <
                      dataPipes.at(1),
              QStringLiteral("Enter at header position %1 should build the "
                             "table and enter the first data cell")
                  .arg(position));
    }

    const QString navigableTable = QStringLiteral(
        "| A | B |\n| --- | --- |\n| one | two |\n| three | four |");
    const QString formattedNavigableTable = QStringLiteral(
        "| A     | B    |\n"
        "| ----- | ---- |\n"
        "| one   | two  |\n"
        "| three | four |");
    const QStringList navigableRows = navigableTable.split(QLatin1Char('\n'));
    for (int sourceRow = 0; sourceRow <= 1; ++sourceRow) {
        for (int position = 0; position <= navigableRows[sourceRow].size();
             ++position) {
            editor.setPlainText(navigableTable);
            QTextCursor inHeader(editor.document()->findBlockByNumber(sourceRow));
            inHeader.setPosition(inHeader.block().position() + position);
            editor.setTextCursor(inHeader);
            sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                    QStringLiteral("\n"));
            check(editor.toPlainText() == formattedNavigableTable &&
                      editor.textCursor().blockNumber() == 2 &&
                      editor.textCursor().selectedText() ==
                          QStringLiteral("one"),
                  QStringLiteral("Enter at header row %1 position %2 should "
                                 "start in the first body cell")
                      .arg(sourceRow)
                      .arg(position));
        }
    }

    // Exercise the real click path as well as direct QTextCursor placement.
    // This guards the common interaction where a rendered/inactive table is
    // clicked before Return is pressed.
    for (int sourceRow = 0; sourceRow <= 1; ++sourceRow) {
        editor.setPlainText(navigableTable);
        editor.moveCursor(QTextCursor::End);
        settleLayout(editor,
                     editor.document()->findBlockByNumber(sourceRow));
        QTextCursor secondCell(editor.document()->findBlockByNumber(sourceRow));
        const QList<int> sourcePipes = MarkdownHighlighter::tablePipePositions(
            secondCell.block().text());
        secondCell.setPosition(secondCell.block().position() +
                               sourcePipes.at(1) + 2);
        clickEditor(editor, editor.cursorRect(secondCell).center());
        sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                QStringLiteral("\n"));
        check(editor.textCursor().blockNumber() == 2 &&
                  editor.textCursor().selectedText() == QStringLiteral("one"),
              QStringLiteral("clicking the second cell of header row %1 then "
                             "pressing Enter should start in the first data cell")
                  .arg(sourceRow));
    }

    const QString unformattedTable = QStringLiteral(
        "|Name|Score|\n|:--|--:|\n|Ada|10|");
    const QString formattedTable = QStringLiteral(
        "| Name | Score |\n| :--- | ----: |\n| Ada  |    10 |");
    for (int sourceRow = 0; sourceRow <= 1; ++sourceRow) {
        editor.setPlainText(unformattedTable);
        QTextCursor headerEnter(
            editor.document()->findBlockByNumber(sourceRow));
        headerEnter.movePosition(QTextCursor::EndOfBlock);
        editor.setTextCursor(headerEnter);
        sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                QStringLiteral("\n"));
        check(editor.toPlainText() == formattedTable &&
                  editor.textCursor().blockNumber() == 2 &&
                  editor.textCursor().selectedText() == QStringLiteral("Ada"),
              QStringLiteral("Enter on header row %1 should format a table "
                             "that fits and start in its first data cell")
                  .arg(sourceRow));
    }

    editor.resize(180, 220);
    QApplication::processEvents();
    const QString enterTooWideTable = QStringLiteral(
        "| A header far too long for this editor | B |\n"
        "| --- | --- |\n"
        "| x | y |");
    editor.setPlainText(enterTooWideTable);
    QTextCursor wideHeader(editor.document()->firstBlock());
    wideHeader.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(wideHeader);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\n"));
    check(editor.toPlainText() == enterTooWideTable &&
              editor.textCursor().blockNumber() == 2 &&
              editor.textCursor().selectedText() == QStringLiteral("x"),
          QStringLiteral("header Enter should preserve an oversized table "
                         "while still entering its first data cell"));
    editor.resize(250, 220);
    QApplication::processEvents();

    editor.setPlainText(navigableTable);
    QTextCursor inDataCell = editor.document()->find(QStringLiteral("two"));
    inDataCell.clearSelection();
    editor.setTextCursor(inDataCell);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\n"));
    check(editor.toPlainText() == navigableTable &&
              editor.textCursor().blockNumber() == 3 &&
              editor.textCursor().selectedText() == QStringLiteral("four"),
          QStringLiteral("Enter in a data cell should move to the same cell "
                         "in the row below without changing source"));

    editor.setPlainText(QStringLiteral(
        "| A | B |\n| --- | --- |\n| one | two |"));
    QTextCursor inLastCell = editor.document()->find(QStringLiteral("two"));
    inLastCell.clearSelection();
    editor.setTextCursor(inLastCell);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\n"));
    const QTextBlock appendedRow = editor.document()->findBlockByNumber(3);
    const QList<int> appendedPipes =
        MarkdownHighlighter::tablePipePositions(appendedRow.text());
    check(editor.document()->blockCount() == 4 &&
              editor.textCursor().blockNumber() == 3 &&
              appendedPipes.size() == 3 &&
              editor.textCursor().positionInBlock() > appendedPipes.at(1) &&
              editor.textCursor().positionInBlock() < appendedPipes.at(2),
          QStringLiteral("Enter in the final table row should append a row and "
                         "move to the same cell below"));

    editor.setPlainText(QStringLiteral("before\n\nafter"));
    QTextCursor middleEmpty(editor.document()->findBlockByNumber(1));
    editor.setTextCursor(middleEmpty);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\n"));
    check(editor.toPlainText() == QStringLiteral("before\n\n\nafter") &&
              editor.textCursor().blockNumber() == 2,
          QStringLiteral("Enter on an empty middle line should create and enter "
                         "the next line"));

    editor.setPlainText(QStringLiteral("before\n"));
    editor.moveCursor(QTextCursor::End);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\n"));
    check(editor.toPlainText() == QStringLiteral("before\n\n") &&
              editor.textCursor().blockNumber() == 2,
          QStringLiteral("Enter on a trailing empty line should create and enter "
                         "another line"));

    editor.setPlainText(QString());
    editor.moveCursor(QTextCursor::Start);
    QApplication::processEvents();
    const int firstEmptyLineY = editor.cursorRect().top();
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("\n") &&
              editor.textCursor().blockNumber() == 1 &&
              editor.cursorRect().top() > firstEmptyLineY,
          QStringLiteral("Enter in a new empty note should create and enter "
                         "its second line"));
    QApplication::processEvents();
    const int secondEmptyLineY = editor.cursorRect().top();
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("\n\n") &&
              editor.textCursor().blockNumber() == 2 &&
              editor.cursorRect().top() > secondEmptyLineY,
          QStringLiteral("Enter on a newly created empty line should keep "
                         "creating lines"));
    QApplication::processEvents();

    // Every unclaimed Return variant must make an actual Markdown source line.
    // Keep these cases separate from the table/list conveniences above: this is
    // the final fallback that protects plain paragraphs, selections, code, and
    // alternate Enter keys from being silently consumed.
    editor.setPlainText(QStringLiteral("alpha beta"));
    QTextCursor splitParagraph(editor.document());
    splitParagraph.setPosition(5);
    editor.setTextCursor(splitParagraph);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("alpha\n beta") &&
              editor.textCursor().blockNumber() == 1,
          QStringLiteral("Enter in a plain paragraph should split it at the caret"));

    editor.setPlainText(QStringLiteral("plain"));
    QTextCursor replaceSelection(editor.document());
    replaceSelection.setPosition(1);
    replaceSelection.setPosition(4, QTextCursor::KeepAnchor);
    editor.setTextCursor(replaceSelection);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("p\nn") &&
              !editor.textCursor().hasSelection(),
          QStringLiteral("Enter should replace a selection with one source newline"));

    editor.setPlainText(QStringLiteral("first"));
    editor.moveCursor(QTextCursor::End);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::ShiftModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("first\n"),
          QStringLiteral("Shift+Enter should create a Markdown source line"));

    editor.setPlainText(QStringLiteral("keypad"));
    editor.moveCursor(QTextCursor::End);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("keypad\n"),
          QStringLiteral("keypad Enter should create a Markdown source line"));

    editor.setPlainText(QStringLiteral("```\ncode\n```"));
    QTextCursor splitCode = editor.document()->find(QStringLiteral("code"));
    splitCode.setPosition(splitCode.selectionStart() + 2);
    editor.setTextCursor(splitCode);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("```\nco\nde\n```"),
          QStringLiteral("Enter inside fenced code should insert a literal newline"));

    editor.setPlainText(QStringLiteral("open below"));
    QTextCursor controlEnter(editor.document());
    controlEnter.setPosition(4);
    editor.setTextCursor(controlEnter);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("open below\n") &&
              editor.textCursor().blockNumber() == 1,
          QStringLiteral("Ctrl+Enter should open a line below and move into it"));

    editor.setPlainText(QStringLiteral("- item"));
    QTextCursor splitList = editor.document()->find(QStringLiteral("item"));
    splitList.setPosition(splitList.selectionStart() + 2);
    editor.setTextCursor(splitList);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("- it\n- em"),
          QStringLiteral("Enter inside a list item should split and continue it"));

    editor.setPlainText(QStringLiteral("1. item"));
    editor.moveCursor(QTextCursor::End);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("1. item\n2. "),
          QStringLiteral("Enter should advance an ordered-list marker"));

    editor.setPlainText(QStringLiteral("- [x] done"));
    editor.moveCursor(QTextCursor::End);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("- [x] done\n- [ ] "),
          QStringLiteral("Enter should continue a task as an unchecked item"));

    editor.setPlainText(QString());
    for (int i = 0; i < 3; ++i)
        sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("\n\n\n") &&
              editor.textCursor().blockNumber() == 3,
          QStringLiteral("rapid consecutive Enters should never lose a newline"));

    // Exercise Return at every caret position across the main block shapes.
    // Convenience handlers may add markers or table rows, but none may consume
    // the key without adding at least one source line.
    const QStringList newlineShapes{
        QStringLiteral("paragraph"), QStringLiteral("# heading"),
        QStringLiteral("**bold** and `code`"), QStringLiteral("[[Wiki link]]"),
        QStringLiteral("- bullet item"), QStringLiteral("1. numbered item"),
        QStringLiteral("> quoted text"), QStringLiteral("| A | B |"),
        QStringLiteral("```\nbody\n```")};
    for (const QString &shape : newlineShapes) {
        for (int position = 0; position <= shape.size(); ++position) {
            editor.setPlainText(shape);
            QTextCursor atPosition(editor.document());
            atPosition.setPosition(position);
            editor.setTextCursor(atPosition);
            const int linesBefore = shape.count(QLatin1Char('\n'));
            sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                    QStringLiteral("\r"));
            check(editor.toPlainText().count(QLatin1Char('\n')) > linesBefore,
                  QStringLiteral("Enter must add a source line at position %1 "
                                 "in '%2'")
                      .arg(position)
                      .arg(shape));
        }
    }

    // Replacing a note while wiki-link completion is open must not leave Enter
    // owned by the old popup. A new empty note has no completion context, so
    // Enter always belongs to the editor.
    editor.setCompletions({QStringLiteral("Destination")});
    QString headingCompletionTarget;
    editor.setHeadingCompletionProvider(
        [&headingCompletionTarget](const QString &target) {
            headingCompletionTarget = target;
            return QStringList({QStringLiteral("Overview"),
                                QStringLiteral("Details")});
        });
    editor.activateWindow();
    editor.setFocus();
    editor.setPlainText(QStringLiteral("[[Destination"));
    editor.moveCursor(QTextCursor::End);
    sendKey(editor, QEvent::KeyPress, Qt::Key_NumberSign, Qt::NoModifier,
            QStringLiteral("#"));
    sendKey(editor, QEvent::KeyPress, Qt::Key_D, Qt::NoModifier,
            QStringLiteral("d"));
    QApplication::processEvents();
    QCompleter *headingCompleter = editor.findChild<QCompleter *>();
    QAbstractItemView *headingPopup =
        headingCompleter ? headingCompleter->popup() : nullptr;
    const int headingRows =
        headingPopup && headingPopup->model()
            ? headingPopup->model()->rowCount()
            : -1;
    const QString firstHeadingCompletion =
        headingRows > 0
            ? headingPopup->model()->index(0, 0).data().toString()
            : QString();
    check(headingCompletionTarget == QStringLiteral("Destination") &&
              headingPopup && headingPopup->isVisible() &&
              headingRows == 1 && firstHeadingCompletion ==
                  QStringLiteral("Details"),
          QStringLiteral("typing # in a wiki link should offer headings from "
                         "that note (target='%1', visible=%2, rows=%3, first='%4', source='%5')")
              .arg(headingCompletionTarget)
              .arg(headingPopup && headingPopup->isVisible())
              .arg(headingRows)
              .arg(firstHeadingCompletion, editor.toPlainText()));
    check(headingCompleter &&
              QMetaObject::invokeMethod(
                  headingCompleter, "activated", Qt::DirectConnection,
                  Q_ARG(QString, firstHeadingCompletion)),
          QStringLiteral("the heading completion can be activated"));
    QApplication::processEvents();
    check(editor.toPlainText() ==
              QStringLiteral("[[Destination#Details]]"),
          QStringLiteral("accepting a heading completion should insert the "
                         "qualified wiki destination (actual '%1')")
              .arg(editor.toPlainText()));

    editor.setPlainText(
        QStringLiteral("[[Destination#Details]]\nplain trailing line"));
    QTextCursor afterHeadingLink(editor.document()->findBlockByNumber(1));
    editor.setTextCursor(afterHeadingLink);
    const QTextBlock renderedHeadingLink = editor.document()->firstBlock();
    settleLayout(editor, renderedHeadingLink);
    const int headingHash = renderedHeadingLink.text().indexOf(QLatin1Char('#'));
    check(headingHash > 0 &&
              highlighterFormatAt(renderedHeadingLink, headingHash)
                      .foreground()
                      .color()
                      .alpha() == 0 &&
              highlighterFormatAt(renderedHeadingLink, 2)
                      .foreground()
                      .color()
                      .alpha() > 0,
          QStringLiteral("an inactive heading-qualified wiki link should hide "
                         "its #heading suffix but retain the note label"));
    editor.setReadMode(true);
    QApplication::processEvents();
    const QTextCursor readHeadingLink =
        editor.document()->find(QStringLiteral("Destination"));
    check(editor.document()->firstBlock().text() ==
              QStringLiteral("Destination") &&
              !editor.document()->toPlainText().contains(
                  QStringLiteral("Details")) &&
              readHeadingLink.charFormat().isAnchor() &&
              MarkdownReadRenderer::wikiTargetFromHref(
                  readHeadingLink.charFormat().anchorHref()) ==
                  QStringLiteral("Destination#Details"),
          QStringLiteral("Read Mode should display the note label without its "
                         "wiki heading qualifier while retaining its target"));
    editor.setReadMode(false);

    editor.setPlainText(QStringLiteral("[[\n"));
    QTextCursor completionLine(editor.document()->firstBlock());
    completionLine.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(completionLine);
    sendKey(editor, QEvent::KeyPress, Qt::Key_D, Qt::NoModifier,
            QStringLiteral("d"));
    QApplication::processEvents();
    QTextCursor emptyLine(editor.document()->findBlockByNumber(1));
    editor.setTextCursor(emptyLine);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("[[d\n\n") &&
              editor.textCursor().blockNumber() == 2,
          QStringLiteral("Enter on an empty line should dismiss completion "
                         "from the previous cursor context"));

    editor.setPlainText(QStringLiteral("[["));
    editor.moveCursor(QTextCursor::End);
    sendKey(editor, QEvent::KeyPress, Qt::Key_D, Qt::NoModifier,
            QStringLiteral("d"));
    QApplication::processEvents();
    editor.setPlainText(QString());
    editor.moveCursor(QTextCursor::Start);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\r"));
    check(editor.toPlainText() == QStringLiteral("\n") &&
              editor.textCursor().blockNumber() == 1,
          QStringLiteral("Enter in a cleared empty note should dismiss stale "
                         "completion state and insert a line"));

    const QStringList emptyConstructs{
        QStringLiteral("-"),       QStringLiteral("- "),
        QStringLiteral("* "),      QStringLiteral("+ "),
        QStringLiteral("- [ ]"),   QStringLiteral("- [ ] "),
        QStringLiteral("- [x] "), QStringLiteral("1."),
        QStringLiteral("2) "),     QStringLiteral(">"),
        QStringLiteral(">> ")};
    for (const QString &emptyConstruct : emptyConstructs) {
        editor.setPlainText(QStringLiteral("first\n") + emptyConstruct);
        editor.moveCursor(QTextCursor::End);
        const int blockCountBefore = editor.document()->blockCount();
        sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                QStringLiteral("\n"));
        check(editor.toPlainText() == QStringLiteral("first\n") &&
                  editor.document()->blockCount() == blockCountBefore &&
                  editor.textCursor().blockNumber() == 1,
              QStringLiteral("Enter on an empty %1 line should remove its "
                             "marker without creating another line")
                  .arg(emptyConstruct.trimmed()));
    }

    editor.undo();
    check(editor.toPlainText() == QStringLiteral("first\n>> "),
          QStringLiteral("undo should restore an empty construct removed by Enter"));

    // Painted interaction affordances and their hit tests share one geometry
    // source. A rendered task checkbox should toggle at the same pixel where it
    // is drawn, even when the raw marker is concealed.
    editor.setPlainText(QStringLiteral("- [x] geometry task\nother line"));
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    QImage checkboxRender(editor.viewport()->size(),
                          QImage::Format_ARGB32_Premultiplied);
    checkboxRender.fill(Qt::transparent);
    editor.viewport()->render(&checkboxRender);
    const QRgb checkboxPixel = QColor(0x2b, 0xbf, 0x74).rgba();
    QRect paintedCheckbox;
    for (int y = 0; y < checkboxRender.height(); ++y) {
        const QRgb *line =
            reinterpret_cast<const QRgb *>(checkboxRender.constScanLine(y));
        for (int x = 0; x < checkboxRender.width(); ++x) {
            if (line[x] != checkboxPixel)
                continue;
            paintedCheckbox = paintedCheckbox.isNull()
                                  ? QRect(x, y, 1, 1)
                                  : paintedCheckbox.united(QRect(x, y, 1, 1));
        }
    }
    QTextCursor taskLabel =
        editor.document()->find(QStringLiteral("geometry task"));
    taskLabel.setPosition(taskLabel.selectionStart());
    const QRect taskLabelCell = editor.cursorRect(taskLabel);
    QTextCursor taskDash(editor.document()->firstBlock());
    const QRect taskDashCell = editor.cursorRect(taskDash);
    const int checkboxLabelGap =
        taskLabelCell.left() - paintedCheckbox.right() - 1;
    check(!paintedCheckbox.isNull() &&
              qAbs(paintedCheckbox.left() - taskDashCell.left()) <= 2,
          QStringLiteral("the rendered checkbox should replace the source dash"));
    check(checkboxLabelGap >= 3 &&
              checkboxLabelGap <= 7,
          QStringLiteral("a rendered checkbox should use a compact gap before "
                         "its label (gap=%1, box=%2..%3, label=%4)")
              .arg(checkboxLabelGap)
              .arg(paintedCheckbox.left())
              .arg(paintedCheckbox.right())
              .arg(taskLabelCell.left()));
    clickEditor(editor, paintedCheckbox.center());
    check(editor.document()->firstBlock().text().startsWith(
              QStringLiteral("- [ ]")),
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

    editor.setPlainText(QStringLiteral(
        "# Fold table\n"
        "| Name | Score |\n"
        "| --- | ---: |\n"
        "| Ada | 10 |\n"
        "# Next"));
    editor.setReadMode(true);
    QApplication::processEvents();
    const QTextBlock readTableHeading =
        MarkdownReadRenderer::blockForSourceBlock(editor.document(), 0);
    const QTextBlock readTableCell =
        MarkdownReadRenderer::blockForSourceBlock(editor.document(), 1);
    QTextTable *foldedTable = QTextCursor(readTableCell).currentTable();
    const QRect readTableHeadingRect =
        editor.cursorRect(QTextCursor(readTableHeading));
    const QPoint readTableFoldPoint(
        qMax(1, readTableHeadingRect.left() - 8),
        readTableHeadingRect.center().y());
    clickEditor(editor, readTableFoldPoint);
    check(foldedTable && !readTableCell.isVisible() &&
              foldedTable->format().border() == 0.0 &&
              foldedTable->cellAt(0, 0).format().background().style() ==
                  Qt::NoBrush,
          QStringLiteral("a collapsed heading should hide the complete Read "
                         "Mode table surface and its cells"));
    clickEditor(editor, readTableFoldPoint);
    check(foldedTable && readTableCell.isVisible() &&
              foldedTable->format().border() > 0.0 &&
              foldedTable->cellAt(0, 0).format().background().style() !=
                  Qt::NoBrush,
          QStringLiteral("expanding a heading should restore its Read Mode "
                         "table surface"));
    editor.setReadMode(false);

    // Consecutive, deeper-indented list items form a source-backed tree. The
    // cached parent makes guide painting linear, while each fold hides only its
    // own descendant run and leaves the next same-level sibling visible.
    const QString nestedListSource = QStringLiteral(
        "- Parent\n"
        "  1. Child one\n"
        "    - Grandchild\n"
        "  - [ ] Child task\n"
        "- Parent sibling");
    editor.setPlainText(nestedListSource);
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    const QTextBlock listParent = editor.document()->findBlockByNumber(0);
    const QTextBlock listChild = editor.document()->findBlockByNumber(1);
    const QTextBlock listGrandchild = editor.document()->findBlockByNumber(2);
    const QTextBlock listTaskChild = editor.document()->findBlockByNumber(3);
    const QTextBlock listSibling = editor.document()->findBlockByNumber(4);
    settleLayout(editor, listSibling);
    check(listChild.blockFormat()
                  .property(MarkdownStyle::ListParentBlockProperty)
                  .toInt() == 0 &&
              listGrandchild.blockFormat()
                      .property(MarkdownStyle::ListParentBlockProperty)
                      .toInt() == 1 &&
              listTaskChild.blockFormat()
                      .property(MarkdownStyle::ListParentBlockProperty)
                      .toInt() == 0 &&
              listSibling.blockFormat()
                      .property(MarkdownStyle::ListParentBlockProperty)
                      .toInt() == -1,
          QStringLiteral("nested bullet, numbered, and task items should cache "
                         "their nearest shallower list parent"));

    // Folding is a presentation-only affordance and never changes source.
    const QRect parentMarkerCell = editor.cursorRect(QTextCursor(listParent));
    QTextCursor childMarkerCursor(listChild);
    childMarkerCursor.setPosition(listChild.position() + 2);
    const QRect childMarkerCell = editor.cursorRect(childMarkerCursor);
    check(editor.toPlainText() == nestedListSource,
          QStringLiteral("nested-list presentation should not modify Markdown "
                         "source"));

    const QPoint parentFoldPoint(qMax(1, parentMarkerCell.left() - 8),
                                 parentMarkerCell.center().y());
    clickEditor(editor, parentFoldPoint);
    check(!listChild.isVisible() && !listGrandchild.isVisible() &&
              !listTaskChild.isVisible() && listSibling.isVisible(),
          QStringLiteral("collapsing a list parent should hide all descendants "
                         "but preserve its next sibling"));
    clickEditor(editor, parentFoldPoint);
    check(listChild.isVisible() && listGrandchild.isVisible() &&
              listTaskChild.isVisible(),
          QStringLiteral("clicking a collapsed list parent should restore its "
                         "whole child tree"));

    const QPoint childFoldPoint(qMax(1, childMarkerCell.left() - 8),
                                childMarkerCell.center().y());
    clickEditor(editor, childFoldPoint);
    check(!listGrandchild.isVisible() && listTaskChild.isVisible(),
          QStringLiteral("a nested parent fold should hide only its own deeper "
                         "children"));
    clickEditor(editor, childFoldPoint);

    // List fold state and parent metadata are source-backed, so the rendered
    // document offers the same interaction without exposing Markdown markers.
    clickEditor(editor, parentFoldPoint);
    editor.setReadMode(true);
    QApplication::processEvents();
    const QTextBlock readParent = MarkdownReadRenderer::blockForSourceBlock(
        editor.document(), 0);
    const QTextBlock readChild = MarkdownReadRenderer::blockForSourceBlock(
        editor.document(), 1);
    const QTextBlock readSibling = MarkdownReadRenderer::blockForSourceBlock(
        editor.document(), 4);
    check(readParent.isVisible() && !readChild.isVisible() &&
              readSibling.isVisible() &&
              readChild.blockFormat()
                      .property(MarkdownStyle::ListParentBlockProperty)
                      .toInt() == readParent.blockNumber(),
          QStringLiteral("Read Mode should preserve nested-list ownership and "
                         "collapsed visibility"));
    const QRect readMarkerCell = editor.cursorRect(QTextCursor(readParent));
    clickEditor(editor,
                QPoint(qMax(1, readMarkerCell.left() - 8),
                       readMarkerCell.center().y()));
    check(readChild.isVisible(),
          QStringLiteral("a list parent should expand directly in Read Mode"));
    editor.setReadMode(false);
    check(editor.document()->findBlockByNumber(1).isVisible(),
          QStringLiteral("expanding in Read Mode should carry back to Edit Mode"));

    editor.setPlainText(QStringLiteral("- Dynamic parent\n- Dynamic child"));
    QTextCursor dynamicChild(editor.document()->findBlockByNumber(1));
    dynamicChild.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(dynamicChild);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::processEvents();
    check(editor.document()
                  ->findBlockByNumber(1)
                  .blockFormat()
                  .property(MarkdownStyle::ListParentBlockProperty)
                  .toInt() == 0,
          QStringLiteral("indenting an existing item should immediately attach "
                         "it to the preceding shallower parent"));
    sendKey(editor, QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
    QApplication::processEvents();
    check(editor.document()
                  ->findBlockByNumber(1)
                  .blockFormat()
                  .property(MarkdownStyle::ListParentBlockProperty)
                  .toInt() == -1,
          QStringLiteral("outdenting an item should immediately detach it from "
                         "the old parent"));

    editor.setPlainText(QStringLiteral(
        "- Bounded parent\n"
        "  - Owned child\n"
        "ordinary paragraph\n"
        "  - Detached indented item"));
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    const QTextBlock boundedParent = editor.document()->firstBlock();
    const QRect boundedMarker = editor.cursorRect(QTextCursor(boundedParent));
    clickEditor(editor,
                QPoint(qMax(1, boundedMarker.left() - 8),
                       boundedMarker.center().y()));
    check(!editor.document()->findBlockByNumber(1).isVisible() &&
              editor.document()->findBlockByNumber(2).isVisible() &&
              editor.document()->findBlockByNumber(3).isVisible() &&
              editor.document()
                      ->findBlockByNumber(3)
                      .blockFormat()
                      .property(MarkdownStyle::ListParentBlockProperty)
                      .toInt() == -1,
          QStringLiteral("a paragraph should terminate both a list fold and "
                         "the following item's parent relationship"));
    clickEditor(editor,
                QPoint(qMax(1, boundedMarker.left() - 8),
                       boundedMarker.center().y()));

    // Edit and Read Mode use different fonts and marker widths, so a list can
    // wrap into different visual lines in the two documents. Preserve the
    // exact source character crossing the viewport top instead of a scrollbar
    // ratio; repeated round trips must not accumulate vertical rounding drift.
    {
        MarkdownEditor driftEditor;
        driftEditor.resize(260, 180);
        driftEditor.show();
        QStringList rows;
        for (int i = 0; i < 90; ++i) {
            const QString indent(i % 4 == 0 ? 4 : (i % 3 == 0 ? 2 : 0),
                                 QLatin1Char(' '));
            rows << QStringLiteral("%1- Item %2 with deliberately varied text "
                                   "that wraps differently across modes %3")
                        .arg(indent)
                        .arg(i)
                        .arg(QString(i % 5, QLatin1Char('x')));
        }
        driftEditor.setPlainText(rows.join(QLatin1Char('\n')));
        settleLayout(driftEditor, driftEditor.document()->lastBlock());
        driftEditor.verticalScrollBar()->setValue(
            driftEditor.verticalScrollBar()->maximum() / 2 + 13);
        QApplication::processEvents();

        QTextCursor editTop = driftEditor.cursorForPosition(
            QPoint(driftEditor.viewport()->width() / 2, 0));
        editTop.clearSelection();
        const int anchoredSourcePosition = editTop.position();
        const int anchoredViewportOffset = driftEditor.cursorRect(editTop).top();

        driftEditor.setReadMode(true);
        QApplication::processEvents();
        QTextCursor sourceAnchor(driftEditor.sourceDocument());
        sourceAnchor.setPosition(anchoredSourcePosition);
        const QTextCursor readAnchor = MarkdownReadRenderer::mapToReadCursor(
            driftEditor.document(), sourceAnchor);
        check(qAbs(driftEditor.cursorRect(readAnchor).top() -
                       anchoredViewportOffset) <= 1,
              QStringLiteral("entering Read Mode on a wrapped bullet list "
                             "should retain the top visual source anchor"));

        driftEditor.setReadMode(false);
        QApplication::processEvents();
        sourceAnchor = QTextCursor(driftEditor.document());
        sourceAnchor.setPosition(anchoredSourcePosition);
        check(qAbs(driftEditor.cursorRect(sourceAnchor).top() -
                       anchoredViewportOffset) <= 1,
              QStringLiteral("a list Read/Edit round trip should return to the "
                             "same viewport pixel"));

        for (int i = 0; i < 8; ++i) {
            driftEditor.setReadMode(true);
            driftEditor.setReadMode(false);
        }
        QApplication::processEvents();
        sourceAnchor = QTextCursor(driftEditor.document());
        sourceAnchor.setPosition(anchoredSourcePosition);
        check(qAbs(driftEditor.cursorRect(sourceAnchor).top() -
                       anchoredViewportOffset) <= 1,
              QStringLiteral("rapid list mode switches should not accumulate "
                             "vertical drift"));
    }

    // Link hit testing now uses the same wrapped-range rectangles as Quick Jump.
    // Clicking blank space after the token must not inherit the nearest cursor.
    editor.resize(230, 220);
    const QString geometryLinkSource = QStringLiteral(
        "enough leading words to wrap [[Geometry Target#Section]]\nother line");
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
    check(geometryJump == QStringLiteral("Geometry Target#Section"),
          QStringLiteral("wrapped link geometry should remain clickable and "
                         "retain its heading destination"));
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
    wideImage.fill(QColor(0xe3, 0x47, 0xa8));
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
    editor.verticalScrollBar()->setValue(0);
    QImage editModeImageRender(editor.viewport()->size(),
                               QImage::Format_ARGB32_Premultiplied);
    editModeImageRender.fill(Qt::transparent);
    editor.viewport()->render(&editModeImageRender);
    bool paintedEditModeImage = false;
    const QRgb expectedEditImagePixel = QColor(0xe3, 0x47, 0xa8).rgba();
    for (int y = 0; y < editModeImageRender.height() && !paintedEditModeImage;
         ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(
            editModeImageRender.constScanLine(y));
        for (int x = 0; x < editModeImageRender.width(); ++x) {
            if (line[x] == expectedEditImagePixel) {
                paintedEditModeImage = true;
                break;
            }
        }
    }
    check(paintedEditModeImage,
          QStringLiteral("edit mode should paint loaded local image content"));
    editor.verticalScrollBar()->setValue(20);
    QImage clippedEditImageRender(editor.viewport()->size(),
                                  QImage::Format_ARGB32_Premultiplied);
    clippedEditImageRender.fill(Qt::transparent);
    editor.viewport()->render(&clippedEditImageRender);
    check(containsPixel(clippedEditImageRender, expectedEditImagePixel),
          QStringLiteral("a partially clipped edit-mode image should remain visible"));
    editor.verticalScrollBar()->setValue(0);
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

    // A reverse selection must not collapse the tall image block while the
    // mouse endpoint is moving upward through it. Keeping that geometry stable
    // prevents the pointer from alternately entering/leaving the block and
    // flickering between rendered content and Markdown source. It should
    // collapse immediately on release so revealed source leaves no image-sized
    // gap between the surrounding lines.
    const QPoint dragStart = editor.cursorRect(editor.textCursor()).center();
    sendMousePress(editor, dragStart);
    QTextCursor reverseImageSelection(editor.document());
    reverseImageSelection.setPosition(imageSource.size());
    reverseImageSelection.setPosition(0, QTextCursor::KeepAnchor);
    editor.setTextCursor(reverseImageSelection);
    QApplication::processEvents();
    check(editor.textCursor().anchor() > editor.textCursor().position() &&
              imageBlock.blockFormat().lineHeightType() ==
                  QTextBlockFormat::FixedHeight,
          QStringLiteral("a bottom-up selection should reveal image syntax "
                         "without changing preview-block geometry"));
    sendMouseRelease(editor, dragStart);
    QApplication::processEvents();
    check(editor.textCursor().anchor() > editor.textCursor().position() &&
              imageBlock.blockFormat().lineHeightType() ==
                  QTextBlockFormat::SingleHeight,
          QStringLiteral("a selected image source should return to normal "
                         "text height when the mouse is released"));
    QImage selectedImageRender(editor.viewport()->size(),
                               QImage::Format_ARGB32_Premultiplied);
    selectedImageRender.fill(Qt::transparent);
    editor.viewport()->render(&selectedImageRender);
    check(!containsPixel(selectedImageRender, expectedEditImagePixel),
          QStringLiteral("a selected image block should not paint the image "
                         "over revealed source"));

    editor.setPlainText(QStringLiteral("![Missing](missing.png)\nafter"));
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    check(editor.document()->firstBlock().blockFormat().lineHeight() >= 100.0,
          QStringLiteral("missing images should reserve a readable fallback card"));

    const QString expandedImageSource = QStringLiteral(
        "![Sized|160x80](wide.png \"inline title\")\n"
        "![Reference][hero]\n"
        "![[wide.png|120]]\n"
        "[hero]: <wide.png> \"reference title\"\n"
        "after expanded images");
    editor.setPlainText(expandedImageSource);
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    const QTextBlock sizedImageBlock =
        editor.document()->findBlockByNumber(0);
    const QTextBlock referenceImageBlock =
        editor.document()->findBlockByNumber(1);
    const QTextBlock obsidianImageBlock =
        editor.document()->findBlockByNumber(2);
    check(sizedImageBlock.blockFormat().lineHeightType() ==
                  QTextBlockFormat::FixedHeight &&
              qAbs(sizedImageBlock.blockFormat().lineHeight() - 104.0) < 2.0,
          QStringLiteral("explicit image dimensions should control Edit Mode "
                         "preview height (type=%1, height=%2)")
              .arg(sizedImageBlock.blockFormat().lineHeightType())
              .arg(sizedImageBlock.blockFormat().lineHeight()));
    check(referenceImageBlock.blockFormat().lineHeightType() ==
                  QTextBlockFormat::FixedHeight &&
              obsidianImageBlock.blockFormat().lineHeightType() ==
                  QTextBlockFormat::FixedHeight,
          QStringLiteral("reference and Obsidian image forms should become "
                         "Edit Mode block previews"));

    QTextDocument expandedReadDocument;
    MarkdownReadRenderer::Options expandedReadOptions;
    expandedReadOptions.baseFont = editor.font();
    expandedReadOptions.imageBasePath = mediaDir.path();
    expandedReadOptions.vaultRootPath = mediaDir.path();
    expandedReadOptions.fallbackWidth = 500.0;
    expandedReadOptions.maxImageHeight = 520.0;
    const QString expandedReadSource = QStringLiteral(
        "Before ![Inline|64x32](wide.png \"inline title\") after\n"
        "![Reference][hero]\n"
        "![[wide.png|80]]\n"
        "[hero]: <wide.png> \"reference title\"");
    MarkdownReadRenderer::render(&expandedReadDocument, expandedReadSource,
                                 expandedReadOptions);
    int expandedImageObjects = 0;
    bool sawInlineImageObject = false;
    bool sawReferenceTitle = false;
    QTextCharFormat inlineImageFormat;
    for (QTextBlock block = expandedReadDocument.firstBlock(); block.isValid();
         block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() ||
                MarkdownReadObjectRenderer::kind(fragment.charFormat()) !=
                    MarkdownReadObjectRenderer::Kind::Image)
                continue;
            ++expandedImageObjects;
            sawInlineImageObject =
                sawInlineImageObject ||
                fragment.charFormat().verticalAlignment() ==
                    QTextCharFormat::AlignMiddle;
            if (fragment.charFormat().verticalAlignment() ==
                QTextCharFormat::AlignMiddle)
                inlineImageFormat = fragment.charFormat();
            sawReferenceTitle =
                sawReferenceTitle ||
                fragment.charFormat().toolTip() ==
                    QStringLiteral("reference title");
        }
    }
    check(expandedImageObjects == 3 && sawInlineImageObject &&
              sawReferenceTitle &&
              !expandedReadDocument.toPlainText().contains(
                  QStringLiteral("[hero]:")),
          QStringLiteral("Read Mode should render inline, reference, and "
                         "Obsidian images and hide reference definitions"));
    MarkdownReadObjectRenderer expandedObjectRenderer;
    const QSizeF inlineImageSize = expandedObjectRenderer.intrinsicSize(
        &expandedReadDocument, 0, inlineImageFormat);
    check(qAbs(inlineImageSize.width() - 66.0) < 2.0 &&
              inlineImageSize.height() >= 32.0,
          QStringLiteral("Read Mode inline images should honor requested "
                         "dimensions"));

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

    // Consecutive quote rows form one painted panel. Check an empty strip far
    // from the text at every physical row between the two line centres; this
    // catches the one-pixel viewport seam caused by fractional block geometry.
    const QString joinedQuoteSource =
        QStringLiteral("> First quote line\n> Second quote line\nafter quote");
    editor.resize(360, 180);
    editor.setLineSpacing(135);
    editor.setPlainText(joinedQuoteSource);
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    const auto quotePanelIsContinuous = [&editor](const QTextBlock &first,
                                                   const QTextBlock &second) {
        settleLayout(editor, second);
        QImage rendered(editor.viewport()->size(),
                        QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        editor.viewport()->render(&rendered);
        const int x = qMax(0, rendered.width() - 20);
        const int firstY = editor.cursorRect(QTextCursor(first)).center().y();
        const int secondY =
            editor.cursorRect(QTextCursor(second)).center().y();
        if (firstY < 0 || secondY <= firstY || secondY >= rendered.height())
            return false;
        const QRgb expected = rendered.pixel(x, firstY);
        for (int y = firstY; y <= secondY; ++y) {
            const QRgb pixel = rendered.pixel(x, y);
            if (qRgb(qRed(pixel), qGreen(pixel), qBlue(pixel)) !=
                qRgb(qRed(expected), qGreen(expected), qBlue(expected)))
                return false;
        }
        return true;
    };
    check(quotePanelIsContinuous(editor.document()->firstBlock(),
                                 editor.document()->findBlockByNumber(1)),
          QStringLiteral("consecutive Edit Mode quote rows should have one "
                         "continuous painted surface"));

    QImage quoteRailRender(editor.viewport()->size(),
                           QImage::Format_ARGB32_Premultiplied);
    quoteRailRender.fill(Qt::transparent);
    editor.viewport()->render(&quoteRailRender);
    const int panelLeft = qCeil(editor.document()->documentMargin());
    const int quoteRailY = editor.cursorRect(
        QTextCursor(editor.document()->firstBlock())).center().y();
    const QRgb beforeRail = quoteRailRender.pixel(panelLeft - 1, quoteRailY);
    const QRgb plainGutter =
        quoteRailRender.pixel(qMax(0, panelLeft - 8), quoteRailY);
    const QRgb onRail = quoteRailRender.pixel(panelLeft + 1, quoteRailY);
    const QRgb insideSurface =
        quoteRailRender.pixel(quoteRailRender.width() - 20, quoteRailY);
    check(qRgb(qRed(beforeRail), qGreen(beforeRail), qBlue(beforeRail)) ==
              qRgb(qRed(plainGutter), qGreen(plainGutter),
                   qBlue(plainGutter)) &&
              qRgb(qRed(onRail), qGreen(onRail), qBlue(onRail)) !=
                  qRgb(qRed(plainGutter), qGreen(plainGutter),
                       qBlue(plainGutter)) &&
              qRgb(qRed(insideSurface), qGreen(insideSurface),
                   qBlue(insideSurface)) !=
                  qRgb(qRed(plainGutter), qGreen(plainGutter),
                       qBlue(plainGutter)),
          QStringLiteral("the Edit Mode quote surface and rail should begin "
                         "at the line edge without painting the gutter"));

    editor.setReadMode(true);
    QApplication::processEvents();
    const QTextBlock firstReadQuote =
        MarkdownReadRenderer::blockForSourceBlock(editor.document(), 0);
    const QTextBlock secondReadQuote =
        MarkdownReadRenderer::blockForSourceBlock(editor.document(), 1);
    QImage readQuoteRender(editor.viewport()->size(),
                           QImage::Format_ARGB32_Premultiplied);
    readQuoteRender.fill(Qt::transparent);
    editor.viewport()->render(&readQuoteRender);
    const QRectF firstReadGeo =
        editor.document()->documentLayout()->blockBoundingRect(firstReadQuote);
    const QRectF secondReadGeo =
        editor.document()->documentLayout()->blockBoundingRect(secondReadQuote);
    const int upperCornerY =
        qBound(0, qFloor(firstReadGeo.top()) - 1,
               readQuoteRender.height() - 1);
    const int lowerCornerY =
        qBound(0, qCeil(secondReadGeo.bottom()),
               readQuoteRender.height() - 1);
    const int readGutterX = qMax(0, panelLeft - 8);
    const auto sameRenderedRgb = [&readQuoteRender](const QPoint &lhs,
                                                    const QPoint &rhs) {
        const QRgb left = readQuoteRender.pixel(lhs);
        const QRgb right = readQuoteRender.pixel(rhs);
        return qRgb(qRed(left), qGreen(left), qBlue(left)) ==
               qRgb(qRed(right), qGreen(right), qBlue(right));
    };
    check(sameRenderedRgb(QPoint(panelLeft, upperCornerY),
                          QPoint(readGutterX, upperCornerY)) &&
              sameRenderedRgb(QPoint(panelLeft, lowerCornerY),
                              QPoint(readGutterX, lowerCornerY)),
          QStringLiteral("Read Mode quote rails must not leak isolated pixels "
                         "past the quote surface corners"));
    check(quotePanelIsContinuous(firstReadQuote, secondReadQuote),
          QStringLiteral("consecutive Read Mode quote rows should have one "
                         "continuous painted surface"));
    editor.setReadMode(false);
    check(editor.toPlainText() == joinedQuoteSource,
          QStringLiteral("painting a joined quote panel must preserve source"));
    editor.setLineSpacing(100);

    // An Obsidian callout marker decorates only the first line of a quote
    // group. Off the active line its source marker melts into a bold title;
    // custom titles replace the type label and keep the type-specific accent.
    const QString calloutSource = QStringLiteral(
        "> [!tip]\n"
        "> A useful detail\n"
        "\n"
        "> [!warning] Read this first\n"
        "> Be careful\n"
        "> [!danger] continuation, not a new callout\n"
        "plain trailing line");
    editor.setPlainText(calloutSource);
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    const QTextBlock tipCallout = editor.document()->findBlockByNumber(0);
    const QTextBlock tipBody = editor.document()->findBlockByNumber(1);
    const QTextBlock warningCallout = editor.document()->findBlockByNumber(3);
    const QTextBlock continuationMarker =
        editor.document()->findBlockByNumber(5);
    settleLayout(editor, continuationMarker);
    const QTextCharFormat concealedCalloutOpen =
        highlighterFormatAt(tipCallout, 2);
    const QTextCharFormat renderedTip = highlighterFormatAt(tipCallout, 4);
    const QTextCharFormat warningTitle =
        highlighterFormatAt(warningCallout,
                            warningCallout.text().indexOf(
                                QStringLiteral("Read")));
    const QTextCharFormat ordinaryContinuation =
        highlighterFormatAt(continuationMarker, 4);
    check(concealedCalloutOpen.foreground().color().alpha() == 0 &&
              concealedCalloutOpen.fontLetterSpacingType() ==
                  QFont::AbsoluteSpacing &&
              concealedCalloutOpen.fontLetterSpacing() > 0.0,
          QStringLiteral("an inactive callout should reserve its [ marker for "
                         "the rendered emoji"));
    check(renderedTip.fontWeight() >= QFont::Bold &&
              !renderedTip.fontItalic() &&
              renderedTip.foreground().color() ==
                  MarkdownCallout::accent(QStringLiteral("tip")) &&
              renderedTip.fontCapitalization() == QFont::Capitalize,
          QStringLiteral("a marker-only callout should render its type as a "
                         "decorated title"));
    check(warningTitle.fontWeight() >= QFont::Bold &&
              !warningTitle.fontItalic() &&
              warningTitle.foreground().color() ==
                  MarkdownCallout::accent(QStringLiteral("warning")),
          QStringLiteral("a custom callout title should use the warning "
                         "decoration"));
    check(ordinaryContinuation.fontItalic() &&
              ordinaryContinuation.fontWeight() < QFont::Bold,
          QStringLiteral("a callout-looking marker after the first quote line "
                         "should remain ordinary quoted text"));
    check(editor.toPlainText() == calloutSource,
          QStringLiteral("callout decoration must preserve Markdown source"));
    check(!editor.document()->isModified(),
          QStringLiteral("derived callout grouping and palette properties must "
                         "not mark source modified"));
    check(qFuzzyIsNull(tipCallout.blockFormat().leftMargin()) &&
              qFuzzyIsNull(tipBody.blockFormat().leftMargin()),
          QStringLiteral("a top-level Edit Mode callout should share the normal "
                         "left content edge"));

    QSet<QString> supportedEmojis;
    bool everyCalloutHasEmoji = true;
    for (const QString &type : MarkdownCallout::supportedTypes()) {
        const QString icon = MarkdownCallout::emoji(type);
        everyCalloutHasEmoji = everyCalloutHasEmoji && !icon.isEmpty();
        supportedEmojis.insert(icon);
    }
    check(everyCalloutHasEmoji &&
              supportedEmojis.size() ==
                  MarkdownCallout::supportedTypes().size(),
          QStringLiteral("every supported callout type should have a unique "
                         "emoji"));

    // The editor painter extends each callout row through the paragraph space
    // before the next. Sample an empty strip on the right from the title's
    // visual center through the body's center: every pixel must be one of the
    // two callout surface colors, never the editor background.
    QImage calloutRender(editor.viewport()->size(),
                         QImage::Format_ARGB32_Premultiplied);
    calloutRender.fill(Qt::transparent);
    editor.viewport()->render(&calloutRender);
    QTextCursor tipCursor(tipCallout);
    QTextCursor tipBodyCursor(tipBody);
    const int titleY = editor.cursorRect(tipCursor).center().y();
    const int bodyY = editor.cursorRect(tipBodyCursor).center().y();
    const int sampleX = qMax(0, calloutRender.width() - 20);
    const QRgb tipTitleSurface =
        MarkdownCallout::surface(QStringLiteral("tip"), true).rgb();
    const QRgb tipBodySurface =
        MarkdownCallout::surface(QStringLiteral("tip"), false).rgb();
    bool continuousCalloutSurface = bodyY > titleY;
    for (int y = titleY; y <= bodyY && y < calloutRender.height(); ++y) {
        const QRgb pixel = calloutRender.pixel(sampleX, y);
        if (qRgb(qRed(pixel), qGreen(pixel), qBlue(pixel)) != tipTitleSurface &&
            qRgb(qRed(pixel), qGreen(pixel), qBlue(pixel)) != tipBodySurface) {
            continuousCalloutSurface = false;
            break;
        }
    }
    check(continuousCalloutSurface,
          QStringLiteral("the Edit Mode callout surface should have no gap "
                         "between its title and body"));

    // Recognition changes immediately when editing the previous line turns a
    // prospective title into a continuation of the same quote group.
    editor.setPlainText(QStringLiteral("plain\n> [!tip]"));
    editor.moveCursor(QTextCursor::Start);
    QApplication::processEvents();
    QTextBlock prospectiveCallout = editor.document()->findBlockByNumber(1);
    check(highlighterFormatAt(prospectiveCallout, 4).fontWeight() >= QFont::Bold,
          QStringLiteral("a callout after plain text should start a quote group"));
    QTextCursor makePreviousQuote(editor.document()->firstBlock());
    makePreviousQuote.insertText(QStringLiteral("> "));
    QApplication::processEvents();
    prospectiveCallout = editor.document()->findBlockByNumber(1);
    check(highlighterFormatAt(prospectiveCallout, 4).fontItalic() &&
              highlighterFormatAt(prospectiveCallout, 4).fontWeight() <
                  QFont::Bold,
          QStringLiteral("changing the previous quote depth should immediately "
                         "remove continuation-line callout decoration"));

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

    // HTML comments are visible author-only source in Edit Mode, disappear
    // completely from Read Mode and semantic links, and remain
    // literal inside code/math. Inline emphasis may cross a removed comment.
    const QString commentSource = QStringLiteral(
        "Visible <!-- private inline [[Hidden Inline]] --> tail\n"
        "**bold <!-- private emphasis --> still bold**\n"
        "<!--\n"
        "# private heading\n"
        "[[Hidden Block]]\n"
        "-->\n"
        "`<!-- literal inline code -->`\n"
        "```md\n"
        "<!-- literal fenced code -->\n"
        "```\n"
        "$$ <!-- literal math --> $$\n"
        "After comments");
    editor.setPlainText(commentSource);
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    const QTextBlock inlineCommentBlock = editor.document()->firstBlock();
    settleLayout(editor, inlineCommentBlock);
    const int inlineCommentStart =
        inlineCommentBlock.text().indexOf(QStringLiteral("<!--"));
    const QTextCharFormat inactiveComment =
        highlighterFormatAt(inlineCommentBlock, inlineCommentStart + 1);
    check(inactiveComment.foreground().color() == QColor("#5e7d6d") &&
              inactiveComment.fontItalic(),
          QStringLiteral("an inactive HTML comment should remain visible with "
                         "subdued Edit Mode styling"));
    const QTextBlock hiddenHeading = editor.document()->findBlockByNumber(3);
    settleLayout(editor, hiddenHeading);
    check(qFuzzyIsNull(hiddenHeading.blockFormat().topMargin()),
          QStringLiteral("Markdown-looking text inside a block comment must "
                         "not receive heading layout"));

    QTextCursor revealComment(inlineCommentBlock);
    revealComment.setPosition(inlineCommentBlock.position() +
                              inlineCommentStart + 5);
    editor.setTextCursor(revealComment);
    settleLayout(editor, inlineCommentBlock);
    const QTextCharFormat visibleComment =
        highlighterFormatAt(inlineCommentBlock, inlineCommentStart + 5);
    check(visibleComment.foreground().color() == QColor("#5e7d6d") &&
              visibleComment.fontItalic(),
          QStringLiteral("comment styling should remain stable while its "
                         "source is edited"));
    check(editor.bodyText().contains(QStringLiteral("Visible  tail")) &&
              !editor.bodyText().contains(QStringLiteral("private inline")) &&
              !editor.bodyText().contains(QStringLiteral("private heading")) &&
              editor.bodyText().contains(
                  QStringLiteral("`<!-- literal inline code -->`")) &&
              editor.bodyText().contains(
                  QStringLiteral("<!-- literal fenced code -->")) &&
              editor.bodyText().contains(
                  QStringLiteral("<!-- literal math -->")),
          QStringLiteral("comment-free body text should retain literal code "
                         "and math examples"));

    editor.setReadMode(true);
    QApplication::processEvents();
    const QString renderedComments = editor.document()->toPlainText();
    check(renderedComments.contains(QStringLiteral("Visible")) &&
              renderedComments.contains(QStringLiteral("tail")) &&
              renderedComments.contains(QStringLiteral("still bold")) &&
              renderedComments.contains(
                  QStringLiteral("<!-- literal inline code -->")) &&
              renderedComments.contains(QStringLiteral("After comments")) &&
              !renderedComments.contains(QStringLiteral("private inline")) &&
              !renderedComments.contains(QStringLiteral("private emphasis")) &&
              !renderedComments.contains(QStringLiteral("private heading")) &&
              !renderedComments.contains(QStringLiteral("Hidden Block")),
          QStringLiteral("Read Mode should omit inline and multi-line comments "
                         "without hiding code literals"));
    QTextCursor boldAfterComment =
        editor.document()->find(QStringLiteral("still bold"));
    boldAfterComment.setPosition(boldAfterComment.selectionStart());
    boldAfterComment.movePosition(QTextCursor::NextCharacter,
                                  QTextCursor::KeepAnchor);
    check(boldAfterComment.charFormat().fontWeight() >= QFont::Bold,
          QStringLiteral("inline bold styling should continue across a removed "
                         "comment"));
    bool sawCommentedWikiAnchor = false;
    for (QTextBlock block = editor.document()->firstBlock(); block.isValid();
         block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isAnchor()) {
                const QString target = MarkdownReadRenderer::wikiTargetFromHref(
                    fragment.charFormat().anchorHref());
                if (target == QStringLiteral("Hidden Inline") ||
                    target == QStringLiteral("Hidden Block"))
                    sawCommentedWikiAnchor = true;
            }
        }
    }
    check(!sawCommentedWikiAnchor,
          QStringLiteral("commented wiki links must not become Read Mode "
                         "anchors"));
    QTextCursor renderedTail = editor.document()->find(QStringLiteral("tail"));
    editor.setTextCursor(renderedTail);
    QApplication::processEvents();
    check(editor.sourceTextCursor().selectedText() == QStringLiteral("tail"),
          QStringLiteral("selection mapping after an omitted inline comment "
                         "should retain the exact source text"));
    editor.setReadMode(false);
    check(editor.toPlainText() == commentSource,
          QStringLiteral("comment rendering must never change Markdown source"));

    // Commented Markdown is inert in the editor's custom layout, painting and
    // smart-key paths as well: it must not become a list/callout/image or be
    // rewritten as a table merely because its private text resembles one.
    editor.setPlainText(QStringLiteral(
        "<!--\n"
        "- [ ] private task\n"
        "> private quote\n"
        "![private](missing.png)\n"
        "---\n"
        "-->\n"
        "Visible"));
    editor.moveCursor(QTextCursor::End);
    QApplication::processEvents();
    const QTextBlock commentedTask =
        editor.document()->findBlockByNumber(1);
    const QTextBlock commentedQuote =
        editor.document()->findBlockByNumber(2);
    const QTextBlock commentedImage =
        editor.document()->findBlockByNumber(3);
    settleLayout(editor, commentedTask);
    settleLayout(editor, commentedQuote);
    settleLayout(editor, commentedImage);
    check(!commentedTask.blockFormat().hasProperty(
              MarkdownStyle::ListDepthProperty) &&
              commentedQuote.blockFormat()
                      .property(MarkdownStyle::CalloutDepthProperty)
                      .toInt() == 0 &&
              commentedImage.blockFormat().lineHeightType() !=
                  QTextBlockFormat::FixedHeight,
          QStringLiteral("commented structures must not reserve list, quote, "
                         "or image-preview layout"));

    QTextCursor commentedEnter(commentedTask);
    commentedEnter.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(commentedEnter);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
            QStringLiteral("\n"));
    check(editor.toPlainText().contains(
              QStringLiteral("- [ ] private task\n\n> private quote")),
          QStringLiteral("Enter inside a comment should insert a plain newline "
                         "instead of continuing Markdown structure"));

    const QString commentedTable =
        QStringLiteral("<!--\n|a|b|\n-->\nVisible");
    editor.setPlainText(commentedTable);
    QTextCursor tableComment(editor.document()->findBlockByNumber(1));
    tableComment.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(tableComment);
    QTextCursor outsideComment(editor.document()->lastBlock());
    outsideComment.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(outsideComment);
    QApplication::processEvents();
    check(editor.toPlainText() == commentedTable,
          QStringLiteral("leaving a table-shaped comment must not auto-format "
                         "its private source"));

    editor.setPlainText(MascotSeed::line(42) +
                        QStringLiteral("\nBody <!-- private --> text"));
    check(editor.mascotSeed() == 42 &&
              editor.bodyText() == QStringLiteral("Body  text"),
          QStringLiteral("the mascot header should remain compatible while "
                         "ordinary comments are generalized"));

    // Standard Markdown links retain their link styling while inheriting both
    // explicit ~~strikethrough~~ and a completed task's strike effect.
    const QString struckInternetLinkSource = QStringLiteral(
        "plain\n"
        "~~before [Strike site](www.example.com) after~~\n"
        "- [x] Completed [task link](www.example.com)");
    editor.setPlainText(struckInternetLinkSource);
    editor.setTextCursor(QTextCursor(editor.document()->firstBlock()));
    const QTextBlock explicitStrikeLinkBlock =
        editor.document()->findBlockByNumber(1);
    const QTextBlock completedTaskLinkBlock =
        editor.document()->findBlockByNumber(2);
    settleLayout(editor, explicitStrikeLinkBlock);
    settleLayout(editor, completedTaskLinkBlock);
    const int explicitStrikeLinkOffset =
        explicitStrikeLinkBlock.text().indexOf(QStringLiteral("Strike site"));
    const int completedTaskLinkOffset =
        completedTaskLinkBlock.text().indexOf(QStringLiteral("task link"));
    check(explicitStrikeLinkOffset >= 0 &&
              highlighterFormatAt(explicitStrikeLinkBlock,
                                  explicitStrikeLinkOffset)
                  .fontUnderline() &&
              highlighterFormatAt(explicitStrikeLinkBlock,
                                  explicitStrikeLinkOffset)
                  .fontStrikeOut(),
          QStringLiteral("an internet link inside strikethrough should retain "
                         "its link style and be struck in Edit Mode"));
    check(completedTaskLinkOffset >= 0 &&
              highlighterFormatAt(completedTaskLinkBlock,
                                  completedTaskLinkOffset)
                  .fontUnderline() &&
              highlighterFormatAt(completedTaskLinkBlock,
                                  completedTaskLinkOffset)
                  .fontStrikeOut(),
          QStringLiteral("an internet link in a completed task should retain "
                         "its link style and be struck in Edit Mode"));

    editor.setReadMode(true);
    QApplication::processEvents();
    QTextCursor renderedStruckInternet =
        editor.document()->find(QStringLiteral("Strike site"));
    renderedStruckInternet.setPosition(
        renderedStruckInternet.selectionStart());
    renderedStruckInternet.movePosition(QTextCursor::NextCharacter,
                                        QTextCursor::KeepAnchor);
    QTextCursor renderedTaskLink =
        editor.document()->find(QStringLiteral("task link"));
    renderedTaskLink.setPosition(renderedTaskLink.selectionStart());
    renderedTaskLink.movePosition(QTextCursor::NextCharacter,
                                  QTextCursor::KeepAnchor);
    check(renderedStruckInternet.charFormat().isAnchor() &&
              renderedStruckInternet.charFormat().fontUnderline() &&
              renderedStruckInternet.charFormat().fontStrikeOut(),
          QStringLiteral("Read Mode should strike an internet link inside "
                         "strikethrough"));
    check(renderedTaskLink.charFormat().isAnchor() &&
              renderedTaskLink.charFormat().fontUnderline() &&
              renderedTaskLink.charFormat().fontStrikeOut(),
          QStringLiteral("Read Mode should strike an internet link inside a "
                         "completed task"));
    editor.setReadMode(false);
    check(editor.toPlainText() == struckInternetLinkSource,
          QStringLiteral("striking internet links must not change Markdown "
                         "source"));

    // Read Mode removes the caret, rejects editing keys, and turns plain arrow
    // navigation into viewport scrolling without relocating the text cursor.
    QStringList readingLines{
        QStringLiteral("# Rendered heading"),
        QStringLiteral("A **bold** paragraph with [[Target#Title2|wiki label]] and "
                       "[site](https://example.com), plus ==marked== and "
                       "$x^2$."),
        QStringLiteral("==word1 **word2== word3** and "
                       "~~asd[[Roadmap]]asd~~"),
        QStringLiteral("> [!tip]"),
        QStringLiteral("> A useful callout body"), QString(),
        QStringLiteral("> [!warning] Read this first"),
        QStringLiteral("> Be careful"), QString(),
        QStringLiteral("> A quoted paragraph"),
        QStringLiteral("> [!danger] continuation, not a title"),
        QStringLiteral("- A list item"),
        QStringLiteral("- [x] A completed task"),
        QStringLiteral("---"),
        QStringLiteral("![Wide preview](wide.png)"),
        QStringLiteral("| Name | Value | Formula |"),
        QStringLiteral("| :--- | ---: | :---: |"),
        QStringLiteral("| [[Table Note|Alpha]] | 10 | `x|y` |"),
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
    const int sourceBoldStart =
        readingSource.indexOf(QStringLiteral("bold"));
    readingCursor.setPosition(sourceBoldStart + 4);
    readingCursor.setPosition(sourceBoldStart, QTextCursor::KeepAnchor);
    editor.setTextCursor(readingCursor);
    const QTextBlock sourceInlineBlock =
        sourceDocument->findBlock(sourceBoldStart);
    settleLayout(editor, sourceInlineBlock);
    const int sourceHighlightOffset =
        sourceInlineBlock.text().indexOf(QStringLiteral("marked"));
    const QTextCharFormat editHighlight =
        highlighterFormatAt(sourceInlineBlock, sourceHighlightOffset);
    const QTextBlock sourceMixedBlock =
        sourceDocument->findBlockByNumber(2);
    settleLayout(editor, sourceMixedBlock);
    const int sourceWikiOffset =
        sourceMixedBlock.text().indexOf(QStringLiteral("Roadmap"));
    check(sourceWikiOffset >= 0 &&
              highlighterFormatAt(sourceMixedBlock, sourceWikiOffset)
                  .fontStrikeOut(),
          QStringLiteral("a wiki link inside strikethrough should be struck "
                         "in Edit Mode"));
    editor.setReadMode(true);
    QApplication::processEvents();
    check(editor.readMode() && editor.isReadOnly(),
          QStringLiteral("Read Mode should make the editor read-only"));
    check(editor.cursorWidth() == 0,
          QStringLiteral("Read Mode should hide the text caret"));
    check(editor.accessibleName() == QStringLiteral("Note reader") &&
              editor.accessibleDescription().contains(
                  QStringLiteral("selected and copied")),
          QStringLiteral("Read Mode should expose a concise assistive name and "
                         "read-only interaction description"));
    check(editor.sourceDocument() == sourceDocument &&
              editor.document() != sourceDocument,
          QStringLiteral("Read Mode should display a separate document while "
                         "retaining the Markdown source document"));
    const QString renderedReading = editor.document()->toPlainText();
    check(renderedReading.contains(QStringLiteral("Rendered heading")) &&
              renderedReading.contains(QStringLiteral("bold")) &&
              renderedReading.contains(QStringLiteral("wiki label")) &&
              renderedReading.contains(QStringLiteral("Tip")) &&
              renderedReading.contains(
                  MarkdownCallout::emoji(QStringLiteral("tip")) +
                  QStringLiteral(" Tip")) &&
              renderedReading.contains(QStringLiteral("Read this first")) &&
              renderedReading.contains(
                  QStringLiteral("[!danger] continuation, not a title")) &&
              !renderedReading.contains(QStringLiteral("# Rendered")) &&
              !renderedReading.contains(QStringLiteral("**bold**")) &&
              !renderedReading.contains(QStringLiteral("[[Target")) &&
              !renderedReading.contains(QStringLiteral("```")) &&
              !renderedReading.contains(QStringLiteral("$x^2$")) &&
              !renderedReading.contains(QStringLiteral("$$")) &&
              !renderedReading.contains(QStringLiteral("[!tip]")) &&
              !renderedReading.contains(QStringLiteral("[!warning]")) &&
              !renderedReading.contains(QStringLiteral("| :---")),
          QStringLiteral("the Read Mode document should contain presentation "
                         "text without Markdown source markers"));
    check(editor.textCursor().selectedText() == QStringLiteral("bold") &&
              editor.sourceTextCursor().selectedText() ==
                  QStringLiteral("bold") &&
              editor.textCursor().anchor() > editor.textCursor().position(),
          QStringLiteral("entering Read Mode should preserve an exact source "
                         "selection and its direction through stripped "
                         "emphasis markers"));
    const QTextBlock renderedHeading = editor.document()->firstBlock();
    QTextCursor renderedHeadingText(renderedHeading);
    renderedHeadingText.movePosition(QTextCursor::NextCharacter,
                                     QTextCursor::KeepAnchor);
    check(renderedHeadingText.charFormat().fontWeight() >= QFont::Bold &&
              renderedHeadingText.charFormat().fontPointSize() >
                  editor.font().pointSizeF(),
          QStringLiteral("the rendered document should retain heading visual "
                         "hierarchy without source markers"));
    QTextCursor renderedHighlight =
        editor.document()->find(QStringLiteral("marked"));
    renderedHighlight.setPosition(renderedHighlight.selectionStart());
    renderedHighlight.movePosition(QTextCursor::NextCharacter,
                                   QTextCursor::KeepAnchor);
    check(renderedHighlight.charFormat().background() ==
                  editHighlight.background() &&
              renderedHighlight.charFormat().foreground() ==
                  editHighlight.foreground(),
          QStringLiteral("Read Mode highlights should use the same foreground "
                         "and background colors as Edit Mode"));

    QTextCursor renderedMixedBold =
        editor.document()->find(QStringLiteral("word2"));
    renderedMixedBold.setPosition(renderedMixedBold.selectionStart());
    renderedMixedBold.movePosition(QTextCursor::NextCharacter,
                                   QTextCursor::KeepAnchor);
    QTextCursor renderedTrailingBold =
        editor.document()->find(QStringLiteral("word3"));
    renderedTrailingBold.setPosition(renderedTrailingBold.selectionStart());
    renderedTrailingBold.movePosition(QTextCursor::NextCharacter,
                                      QTextCursor::KeepAnchor);
    QTextCursor renderedStruckWiki =
        editor.document()->find(QStringLiteral("Roadmap"));
    renderedStruckWiki.setPosition(renderedStruckWiki.selectionStart());
    renderedStruckWiki.movePosition(QTextCursor::NextCharacter,
                                    QTextCursor::KeepAnchor);
    check(renderedMixedBold.charFormat().fontWeight() >= QFont::Bold &&
              renderedMixedBold.charFormat().background().color() ==
                  MarkdownStyle::highlightBackground() &&
              renderedTrailingBold.charFormat().fontWeight() >= QFont::Bold &&
              renderedTrailingBold.charFormat().background().style() ==
                  Qt::NoBrush,
          QStringLiteral("Read Mode should preserve crossing highlight and "
                         "bold spans"));
    check(renderedStruckWiki.charFormat().isAnchor() &&
              renderedStruckWiki.charFormat().fontStrikeOut(),
          QStringLiteral("Read Mode should strike a semantic wiki link inside "
                         "strikethrough"));

    QTextCursor renderedCallout = editor.document()->find(QStringLiteral("Tip"));
    renderedCallout.setPosition(renderedCallout.selectionStart());
    renderedCallout.movePosition(QTextCursor::NextCharacter,
                                 QTextCursor::KeepAnchor);
    check(renderedCallout.charFormat().fontWeight() >= QFont::Bold &&
              !renderedCallout.charFormat().fontItalic() &&
              renderedCallout.charFormat().foreground().color() ==
                  MarkdownCallout::accent(QStringLiteral("tip")) &&
              renderedCallout.blockFormat().background().color() ==
                  MarkdownCallout::surface(QStringLiteral("tip"), true),
          QStringLiteral("Read Mode should render a marker-only callout as a "
                         "bold, tinted title row"));
    const QTextBlock renderedCalloutTitle = renderedCallout.block();
    const QTextBlock renderedCalloutBody = renderedCalloutTitle.next();
    check(renderedCalloutBody.isValid() &&
              renderedCalloutBody.blockFormat().background().color() ==
                  MarkdownCallout::surface(QStringLiteral("tip"), false) &&
              qFuzzyIsNull(renderedCalloutTitle.blockFormat().leftMargin()) &&
              qFuzzyIsNull(renderedCalloutBody.blockFormat().leftMargin()),
          QStringLiteral("Read Mode should propagate the callout palette "
                         "through its body and align the group left"));
    const QRectF readTitleGeometry =
        editor.document()->documentLayout()->blockBoundingRect(
            renderedCalloutTitle);
    const QRectF readBodyGeometry =
        editor.document()->documentLayout()->blockBoundingRect(
            renderedCalloutBody);
    check(qFuzzyIsNull(renderedCalloutTitle.blockFormat().bottomMargin()) &&
              qAbs(readTitleGeometry.bottom() - readBodyGeometry.top()) < 0.1,
          QStringLiteral("Read Mode should leave no unpainted paragraph gap "
                         "between a callout title and body"));
    editor.setTextCursor(renderedCallout);
    check(editor.sourceTextCursor().selectedText() == QStringLiteral("t"),
          QStringLiteral("a generated callout title should map back to its "
                         "exact source type"));

    bool sawImageObject = false;
    bool sawInlineMathObject = false;
    bool sawDisplayMathObject = false;
    bool sawRuleObject = false;
    bool sawCheckboxObject = false;
    bool sawCodeObject = false;
    bool sawWikiAnchor = false;
    bool sawWikiTooltip = false;
    bool sawExternalAnchor = false;
    bool sawTableWikiAnchor = false;
    int renderedWikiPosition = -1;
    QTextBlock imageObjectBlock;
    QTextBlock checkboxObjectBlock;
    QTextBlock codeObjectBlock;
    QTextCharFormat checkboxObjectFormat;
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
            if (format.isAnchor()) {
                const QString wikiTarget =
                    MarkdownReadRenderer::wikiTargetFromHref(
                        format.anchorHref());
                if (wikiTarget == QStringLiteral("Target#Title2")) {
                    sawWikiAnchor = true;
                    sawWikiTooltip = sawWikiTooltip || !format.toolTip().isEmpty();
                    renderedWikiPosition = fragment.position();
                } else if (wikiTarget == QStringLiteral("Table Note")) {
                    sawTableWikiAnchor = true;
                    sawWikiTooltip = sawWikiTooltip || !format.toolTip().isEmpty();
                } else if (format.anchorHref() ==
                           QStringLiteral("https://example.com")) {
                    sawExternalAnchor = true;
                }
            }
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
                checkboxObjectBlock = block;
                checkboxObjectFormat = format;
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
    check(checkboxObjectFormat.verticalAlignment() ==
              QTextCharFormat::AlignMiddle,
          QStringLiteral("Read Mode checkboxes should request middle inline "
                         "alignment with their labels"));
    check(sawWikiAnchor && sawExternalAnchor && sawTableWikiAnchor,
          QStringLiteral("Read Mode should retain semantic wiki and external "
                         "anchors, including links inside table cells"));
    check(!sawWikiTooltip,
          QStringLiteral("hovering a rendered wiki link should not show its "
                         "raw note target as a tooltip"));
    check(MarkdownReadObjectRenderer::codeText(codeObjectFormat) ==
              QStringLiteral("const int answer = 42;"),
          QStringLiteral("the code-card object should retain exact copyable "
                         "source without its Markdown fences"));
    check(MarkdownReadObjectRenderer::accessibleText(codeObjectFormat) ==
              QStringLiteral("const int answer = 42;") &&
              !codeObjectFormat.toolTip().isEmpty(),
          QStringLiteral("custom Read Mode objects should expose readable text "
                         "and a descriptive tooltip"));

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

        QTextCursor tableSelection = editor.document()->find(
            QStringLiteral("Alpha"));
        editor.setTextCursor(tableSelection);
        check(tableSelection.hasSelection() &&
                  editor.sourceTextCursor().selectedText() ==
                      QStringLiteral("Alpha"),
              QStringLiteral("a selection in a semantic table cell should map "
                             "to the exact wiki-link alias in Markdown source"));
        QApplication::clipboard()->clear();
        editor.copy();
        check(QApplication::clipboard()->text() == QStringLiteral("Alpha"),
              QStringLiteral("programmatic copy should emit clean table-cell "
                             "presentation text in Read Mode"));
    }

    // Copy emits presentation text rather than Markdown syntax. A mapped
    // selection remains exact on the hidden source side and survives reflow.
    QTextCursor wikiLabelSelection =
        editor.document()->find(QStringLiteral("wiki label"));
    editor.setTextCursor(wikiLabelSelection);
    QApplication::clipboard()->clear();
    sendKey(editor, QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier,
            QStringLiteral("c"));
    check(QApplication::clipboard()->text() == QStringLiteral("wiki label"),
          QStringLiteral("copying a Read Mode link label should produce clean "
                         "display text"));
    const QString mappedWikiLabel =
        editor.sourceTextCursor().selectedText();
    check(mappedWikiLabel == QStringLiteral("wiki label"),
          QStringLiteral("a selected Read Mode link label should retain its "
                         "exact source mapping (mapped: '%1')")
              .arg(mappedWikiLabel));

    // Read Mode hit testing uses the semantic anchor range, so a plain mouse
    // click follows the displayed label without consulting hidden Markdown.
    QString readLinkTarget;
    QObject::connect(&editor, &MarkdownEditor::linkClicked,
                     [&readLinkTarget](const QString &target) {
                         readLinkTarget = target;
                     });
    if (renderedWikiPosition >= 0) {
        const QTextBlock wikiBlock =
            editor.document()->findBlock(renderedWikiPosition);
        settleLayout(editor, wikiBlock);
        editor.verticalScrollBar()->setValue(0);
        QApplication::processEvents();
        QTextCursor left(editor.document());
        left.setPosition(renderedWikiPosition);
        QTextCursor right(left);
        right.movePosition(QTextCursor::NextCharacter);
        const QRect leftRect = editor.cursorRect(left);
        const QRect rightRect = editor.cursorRect(right);
        clickEditor(editor,
                    QPoint((leftRect.left() + rightRect.left()) / 2,
                           leftRect.center().y()));
    }
    check(readLinkTarget == QStringLiteral("Target#Title2"),
          QStringLiteral("a plain click should open a semantic Read Mode "
                         "wiki anchor with its heading destination"));

    // Quick Jump reads the same anchors and therefore also reaches links in a
    // QTextTable cell. The first visible link keeps the QWERTY-first Q hint.
    editor.activateWindow();
    editor.setFocus();
    editor.verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    readLinkTarget.clear();
    beginQuickJump(editor);
    QImage readQuickJumpRender(editor.viewport()->size(),
                               QImage::Format_ARGB32_Premultiplied);
    readQuickJumpRender.fill(Qt::transparent);
    {
        QPainter painter(&readQuickJumpRender);
        editor.viewport()->render(&painter);
    }
    bool paintedQuickJumpBadge = false;
    const QRgb expectedBadgePixel = QColor(0x39, 0xd9, 0x83).rgba();
    for (int y = 0;
         y < readQuickJumpRender.height() && !paintedQuickJumpBadge; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(
            readQuickJumpRender.constScanLine(y));
        for (int x = 0; x < readQuickJumpRender.width(); ++x) {
            if (line[x] == expectedBadgePixel) {
                paintedQuickJumpBadge = true;
                break;
            }
        }
    }
    check(paintedQuickJumpBadge,
          QStringLiteral("Read Mode Quick Jump should paint a visible hint "
                         "badge beside each semantic link"));
    sendKey(editor, QEvent::KeyPress, Qt::Key_Q, Qt::AltModifier,
            QStringLiteral("q"));
    check(readLinkTarget == QStringLiteral("Target#Title2"),
          QStringLiteral("Read Mode Quick Jump Q should open the first "
                         "semantic link with its heading destination"));
    endQuickJump(editor);

    QTextCursor currentTableLink =
        editor.document()->find(QStringLiteral("Alpha"));
    QTextTable *currentTable = currentTableLink.currentTable();
    if (currentTable) {
        const QTextBlock tableLinkBlock =
            currentTable->cellAt(1, 0).firstCursorPosition().block();
        settleLayout(editor, tableLinkBlock);
        const QRectF tableLinkDocumentRect =
            editor.document()->documentLayout()->blockBoundingRect(
                tableLinkBlock);
        editor.verticalScrollBar()->setValue(
            qRound(tableLinkDocumentRect.top()));
        QApplication::processEvents();
        readLinkTarget.clear();
        beginQuickJump(editor);
        sendKey(editor, QEvent::KeyPress, Qt::Key_Q, Qt::AltModifier,
                QStringLiteral("q"));
        check(readLinkTarget == QStringLiteral("Table Note"),
              QStringLiteral("Read Mode Quick Jump should enumerate semantic "
                             "links inside table cells"));
        endQuickJump(editor);
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
        QTextCursor selectedCodeObject(codeObjectBlock);
        selectedCodeObject.movePosition(QTextCursor::NextCharacter,
                                        QTextCursor::KeepAnchor);
        editor.setTextCursor(selectedCodeObject);
        QApplication::clipboard()->clear();
        sendKey(editor, QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier,
                QStringLiteral("c"));
        const QString mappedCodeSource =
            editor.sourceTextCursor().selectedText();
        check(QApplication::clipboard()->text() ==
                  QStringLiteral("const int answer = 42;") &&
                  mappedCodeSource.startsWith(QStringLiteral("```cpp")) &&
                  mappedCodeSource.endsWith(QStringLiteral("```")),
              QStringLiteral("copying a selected code card should emit its "
                             "code while mapping the object to its complete "
                             "fenced source span"));

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

    // Checkbox clicks intentionally rebuild the Read Mode document, so exercise
    // them only after every assertion that retains a block/table pointer into
    // the original rendered document.
    int readCheckboxChanges = 0;
    QObject::connect(&editor, &MarkdownEditor::sourceChanged,
                     [&readCheckboxChanges] { ++readCheckboxChanges; });
    auto findReadCheckbox = [&editor] {
        for (QTextBlock block = editor.document()->firstBlock(); block.isValid();
             block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (fragment.isValid() &&
                    MarkdownReadObjectRenderer::kind(fragment.charFormat()) ==
                        MarkdownReadObjectRenderer::Kind::Checkbox)
                    return block;
            }
        }
        return QTextBlock();
    };
    auto positionReadCheckbox = [&editor](const QTextBlock &block) {
        if (!block.isValid())
            return QRectF();
        settleLayout(editor, block);
        const QRectF documentRect =
            editor.document()->documentLayout()->blockBoundingRect(block);
        editor.verticalScrollBar()->setValue(
            qMax(0, qRound(documentRect.top()) - 20));
        QApplication::processEvents();
        return firstObjectViewportRect(editor, block);
    };
    if (checkboxObjectBlock.isValid()) {
        QRectF checkboxRect = positionReadCheckbox(checkboxObjectBlock);
        QImage readCheckboxRender(editor.viewport()->size(),
                                  QImage::Format_ARGB32_Premultiplied);
        readCheckboxRender.fill(Qt::transparent);
        editor.viewport()->render(&readCheckboxRender);
        QRect paintedReadCheckbox;
        const QRect scan = checkboxRect.adjusted(-2, -2, 2, 2)
                               .toAlignedRect()
                               .intersected(readCheckboxRender.rect());
        for (int y = scan.top(); y <= scan.bottom(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(
                readCheckboxRender.constScanLine(y));
            for (int x = scan.left(); x <= scan.right(); ++x) {
                if (line[x] != QColor(0x2b, 0xbf, 0x74).rgba())
                    continue;
                paintedReadCheckbox =
                    paintedReadCheckbox.isNull()
                        ? QRect(x, y, 1, 1)
                        : paintedReadCheckbox.united(QRect(x, y, 1, 1));
            }
        }
        QTextCursor readTaskLabel(checkboxObjectBlock);
        readTaskLabel.setPosition(checkboxObjectBlock.position() + 2);
        const QRect labelCell = editor.cursorRect(readTaskLabel);
        check(!paintedReadCheckbox.isNull() &&
                  qAbs(paintedReadCheckbox.center().y() -
                       labelCell.center().y()) <= 2,
              QStringLiteral("the Read Mode checkbox should be vertically "
                             "centered on its text line"));

        clickEditor(editor, checkboxRect.center().toPoint());
        check(editor.sourceDocument()->toPlainText().contains(
                  QStringLiteral("- [ ] A completed task")) &&
                  readCheckboxChanges == 1,
              QStringLiteral("clicking a Read Mode checkbox should update its "
                             "Markdown source and request autosave"));

        const QTextBlock uncheckedBlock = findReadCheckbox();
        checkboxRect = positionReadCheckbox(uncheckedBlock);
        if (checkboxRect.isValid())
            clickEditor(editor, checkboxRect.center().toPoint());
        check(editor.sourceDocument()->toPlainText() == readingSource &&
                  readCheckboxChanges == 2,
              QStringLiteral("a second Read Mode checkbox click should restore "
                             "the checked source state"));
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

    // Restore a collapsed source-driven cursor after the programmatic
    // off-screen selection checks above. This mirrors a normal note load and
    // keeps QTextEdit's native selection reveal out of scrolling assertions.
    QTextCursor stableReadSourceCursor(sourceDocument);
    stableReadSourceCursor.setPosition(sourceBoldStart);
    editor.setSourceTextCursor(stableReadSourceCursor);
    waitForMs(10);
    editor.verticalScrollBar()->setValue(100);
    const int cursorBeforeScroll = editor.textCursor().position();
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
                         "pixel position (before %1, during %2, target %3)")
              .arg(scrollBeforeDown)
              .arg(scrollDuringDown)
              .arg(downTarget));
    waitForMs(100);
    check(editor.verticalScrollBar()->value() == downTarget &&
              !editor.smoothScrollActive(),
          QStringLiteral("Read Mode Down should finish at its pixel target "
                         "(actual %1, target %2, active %3)")
              .arg(editor.verticalScrollBar()->value())
              .arg(downTarget)
              .arg(editor.smoothScrollActive()));
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
          QStringLiteral("Read Mode Up should finish at its pixel target "
                         "(actual %1, target %2, active %3)")
              .arg(editor.verticalScrollBar()->value())
              .arg(upTarget)
              .arg(editor.smoothScrollActive()));

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
                         "target (actual %1, target %2, active %3)")
              .arg(editor.verticalScrollBar()->value())
              .arg(secondWheelTarget)
              .arg(editor.smoothScrollActive()));

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
    QTextCursor exitSelection =
        editor.document()->find(QStringLiteral("wiki label"));
    editor.setTextCursor(exitSelection);
    editor.setLineSpacing(101);
    waitForMs(10);
    check(editor.textCursor().selectedText() == QStringLiteral("wiki label"),
          QStringLiteral("rebuilding the rendered document should preserve "
                         "both sides of a mapped selection"));
    check(editor.sourceTextCursor().selectedText() ==
              QStringLiteral("wiki label"),
          QStringLiteral("the active Read Mode selection should stay synced to "
                         "the authoritative source cursor"));
    const int readScrollBeforeExit = editor.verticalScrollBar()->value();
    editor.setReadMode(false);
    QApplication::processEvents();
    check(!editor.isReadOnly() && editor.cursorWidth() > 0 &&
              editor.document() == sourceDocument,
          QStringLiteral("leaving Read Mode should restore the source document, "
                         "editing, and caret"));
    check(editor.textCursor().selectedText() == QStringLiteral("wiki label") &&
              editor.toPlainText() == readingSource,
          QStringLiteral("leaving Read Mode should restore the mapped source "
                         "selection and exact Markdown text"));
    check(readScrollBeforeExit == 0 || editor.verticalScrollBar()->value() > 0,
          QStringLiteral("leaving Read Mode should preserve reading progress "
                         "instead of jumping to the top"));

    // Following a wiki link replaces the current note synchronously from the
    // linkClicked handler. Build the replacement off-screen and swap it once:
    // rendering into the installed QTextDocument causes every inserted block to
    // invalidate the live viewport and leaves a large queue of layout work.
    const QString replacedWhileReading =
        QStringLiteral("## Rebuilt note\nText with ~~old~~ and `code`.");
    editor.setReadMode(true);
    QPointer<QTextDocument> renderedBeforeReplace = editor.document();
    bool replacedFromWikiClick = false;
    const QMetaObject::Connection replaceConnection = QObject::connect(
        &editor, &MarkdownEditor::linkClicked, &editor,
        [&](const QString &target) {
            if (target != QStringLiteral("Target#Title2"))
                return;
            replacedFromWikiClick = true;
            editor.setPlainText(replacedWhileReading);
        });
    QTextCursor replacementLink =
        editor.document()->find(QStringLiteral("wiki label"));
    if (!replacementLink.isNull()) {
        replacementLink.setPosition(replacementLink.selectionStart());
        editor.verticalScrollBar()->setValue(0);
        settleLayout(editor, replacementLink.block());
        const QRect left = editor.cursorRect(replacementLink);
        replacementLink.movePosition(QTextCursor::NextCharacter);
        const QRect right = editor.cursorRect(replacementLink);
        clickEditor(editor,
                    QPoint((left.left() + right.left()) / 2,
                           left.center().y()));
    }
    QApplication::processEvents();
    QObject::disconnect(replaceConnection);
    check(replacedFromWikiClick && renderedBeforeReplace.isNull() &&
              editor.document() != editor.sourceDocument() &&
              editor.toPlainText() == replacedWhileReading &&
              editor.document()->toPlainText().contains(
                  QStringLiteral("Rebuilt note")) &&
              !editor.document()->toPlainText().contains(QStringLiteral("##")) &&
              !editor.document()->toPlainText().contains(QStringLiteral("~~")),
          QStringLiteral("clicking a Read Mode wiki link should atomically "
                         "replace the separate presentation document"));
    editor.setReadMode(false);
    const QTextBlock replacedSourceBlock =
        editor.document()->findBlockByNumber(1);
    settleLayout(editor, replacedSourceBlock);
    check(highlighterFormatAt(
              replacedSourceBlock,
              replacedSourceBlock.text().indexOf(QStringLiteral("old")))
              .fontStrikeOut() &&
              highlighterFormatAt(
                  replacedSourceBlock,
                  replacedSourceBlock.text().indexOf(QStringLiteral("code")))
                      .foreground()
                      .color() == QColor(QStringLiteral("#7ee0b0")),
          QStringLiteral("leaving Read Mode after replacing a note should "
                         "restore full source highlighting"));

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

    // Read Mode headings use the same source-backed section boundaries as the
    // editor, so their gutter control can collapse and expand rendered blocks
    // without changing Markdown source.
    const QString readFoldSource = QStringLiteral(
        "# Read fold\nbody\n## Child\nchild body\n# Next\nnext body");
    editor.setPlainText(readFoldSource);
    editor.setReadMode(true);
    QApplication::processEvents();
    const QTextBlock readFoldHeading =
        MarkdownReadRenderer::blockForSourceBlock(editor.document(), 0);
    const QTextBlock readFoldBody =
        MarkdownReadRenderer::blockForSourceBlock(editor.document(), 1);
    const QTextBlock readFoldChildBody =
        MarkdownReadRenderer::blockForSourceBlock(editor.document(), 3);
    const QTextBlock readFoldNext =
        MarkdownReadRenderer::blockForSourceBlock(editor.document(), 4);
    settleLayout(editor, readFoldHeading);
    const QPoint readFoldPoint(
        5, editor.cursorRect(QTextCursor(readFoldHeading)).center().y());
    clickEditor(editor, readFoldPoint);
    check(readFoldHeading.isVisible() && !readFoldBody.isVisible() &&
              !readFoldChildBody.isVisible() && readFoldNext.isVisible(),
          QStringLiteral("clicking a Read Mode heading should collapse its "
                         "rendered section only"));
    clickEditor(editor, readFoldPoint);
    check(readFoldBody.isVisible() && readFoldChildBody.isVisible() &&
              editor.toPlainText() == readFoldSource,
          QStringLiteral("clicking a folded Read Mode heading should restore "
                         "its section without changing source"));
    editor.setReadMode(false);

    // Ctrl+Shift+H edits highlights through the rendered selection while the
    // Markdown source remains authoritative. If every selected word is already
    // highlighted it removes only that selected portion; otherwise it fills
    // every gap and coalesces the result with highlights it touches.
    {
        MarkdownEditor highlightEditor;
        highlightEditor.resize(700, 420);
        highlightEditor.show();
        highlightEditor.activateWindow();
        highlightEditor.setFocus();
        int highlightChanges = 0;
        QObject::connect(&highlightEditor, &MarkdownEditor::sourceChanged,
                         [&highlightChanges] { ++highlightChanges; });
        auto selectRendered = [&](const QString &first,
                                  const QString &last = QString()) {
            QTextCursor start =
                highlightEditor.document()->find(first);
            check(!start.isNull(),
                  QStringLiteral("Read Mode highlight test should find '%1'")
                      .arg(first));
            if (start.isNull())
                return;
            if (last.isEmpty()) {
                highlightEditor.setTextCursor(start);
                return;
            }
            const QTextCursor end =
                highlightEditor.document()->find(last, start);
            check(!end.isNull(),
                  QStringLiteral("Read Mode highlight test should find '%1'")
                      .arg(last));
            if (end.isNull())
                return;
            start.setPosition(start.selectionStart());
            start.setPosition(end.selectionEnd(), QTextCursor::KeepAnchor);
            highlightEditor.setTextCursor(start);
        };
        auto toggleHighlight = [&] {
            sendKey(highlightEditor, QEvent::KeyPress, Qt::Key_H,
                    Qt::ControlModifier | Qt::ShiftModifier,
                    QStringLiteral("h"));
            QApplication::processEvents();
        };

        highlightEditor.setPlainText(
            QStringLiteral("Alpha beta ==gamma delta== epsilon"));
        highlightEditor.setReadMode(true);
        selectRendered(QStringLiteral("beta"));
        toggleHighlight();
        check(highlightEditor.toPlainText() ==
                  QStringLiteral("Alpha ==beta== ==gamma delta== epsilon"),
              QStringLiteral("Ctrl+Shift+H should highlight a plain Read Mode "
                             "selection"));
        check(highlightEditor.textCursor().selectedText() ==
                  QStringLiteral("beta"),
              QStringLiteral("highlighting should preserve the rendered "
                             "selection (actual '%1')")
                  .arg(highlightEditor.textCursor().selectedText()));

        selectRendered(QStringLiteral("gamma"));
        toggleHighlight();
        check(highlightEditor.toPlainText() ==
                  QStringLiteral("Alpha ==beta== gamma ==delta== epsilon"),
              QStringLiteral("selecting part of a highlighted run should "
                             "unhighlight only that part"));

        selectRendered(QStringLiteral("beta"), QStringLiteral("gamma"));
        toggleHighlight();
        check(highlightEditor.toPlainText() ==
                  QStringLiteral("Alpha ==beta gamma== ==delta== epsilon"),
              QStringLiteral("one unhighlighted word should make the shortcut "
                             "fill gaps and preserve existing highlights"));
        selectRendered(QStringLiteral("beta"), QStringLiteral("delta"));
        toggleHighlight();
        check(highlightEditor.toPlainText() ==
                  QStringLiteral("Alpha beta gamma delta epsilon"),
              QStringLiteral("an entirely highlighted selection should remove "
                             "the complete highlight"));

        // Marker insertions use rendered boundaries, so partial toggles remain
        // correctly nested inside other inline Markdown rather than crossing
        // the ** delimiters.
        highlightEditor.setPlainText(QStringLiteral("==**bold**=="));
        selectRendered(QStringLiteral("bo"));
        toggleHighlight();
        check(highlightEditor.toPlainText() ==
                  QStringLiteral("**bo==ld==**"),
              QStringLiteral("partial removal should preserve nested bold "
                             "Markdown"));
        selectRendered(QStringLiteral("bold"));
        toggleHighlight();
        check(highlightEditor.toPlainText() ==
                  QStringLiteral("**==bold==**"),
              QStringLiteral("a mixed nested selection should fill its "
                             "unhighlighted portion"));
        selectRendered(QStringLiteral("bold"));
        toggleHighlight();
        check(highlightEditor.toPlainText() == QStringLiteral("**bold**"),
              QStringLiteral("a fully highlighted nested selection should "
                             "toggle off cleanly"));

        highlightEditor.setPlainText(
            QStringLiteral("- first words\n> second phrase"));
        selectRendered(QStringLiteral("first"), QStringLiteral("phrase"));
        toggleHighlight();
        check(highlightEditor.toPlainText() ==
                  QStringLiteral("- ==first words==\n> ==second phrase=="),
              QStringLiteral("multi-line Read Mode highlights should wrap each "
                             "line without consuming list or quote markers"));
        selectRendered(QStringLiteral("first"), QStringLiteral("phrase"));
        toggleHighlight();
        check(highlightEditor.toPlainText() ==
                  QStringLiteral("- first words\n> second phrase"),
              QStringLiteral("the same multi-line selection should toggle all "
                             "of its highlights off"));

        highlightEditor.setPlainText(
            QStringLiteral("Open [[Target|wiki label]] now"));
        selectRendered(QStringLiteral("wiki label"));
        toggleHighlight();
        check(highlightEditor.toPlainText() ==
                  QStringLiteral("Open [[Target|==wiki label==]] now"),
              QStringLiteral("highlighting a wiki alias should modify only its "
                             "visible label, not the hidden target"));
        selectRendered(QStringLiteral("wiki label"));
        toggleHighlight();
        check(highlightEditor.toPlainText() ==
                  QStringLiteral("Open [[Target|wiki label]] now"),
              QStringLiteral("a highlighted wiki alias should toggle off "
                             "without damaging link syntax"));

        highlightEditor.setPlainText(QStringLiteral(
            "| Name | Value |\n| --- | --- |\n| Alpha | Beta |"));
        selectRendered(QStringLiteral("Alpha"));
        toggleHighlight();
        check(highlightEditor.toPlainText() == QStringLiteral(
                  "| Name | Value |\n| --- | --- |\n| ==Alpha== | Beta |"),
              QStringLiteral("highlighting a semantic table cell should keep "
                             "the table structure intact"));
        selectRendered(QStringLiteral("Alpha"));
        toggleHighlight();
        check(highlightEditor.toPlainText() == QStringLiteral(
                  "| Name | Value |\n| --- | --- |\n| Alpha | Beta |"),
              QStringLiteral("a highlighted table cell should toggle off "
                             "without consuming its pipes"));

        QTextCursor noSelection = highlightEditor.textCursor();
        noSelection.clearSelection();
        highlightEditor.setTextCursor(noSelection);
        const QString beforeNoSelection = highlightEditor.toPlainText();
        const int changesBeforeNoSelection = highlightChanges;
        toggleHighlight();
        check(highlightEditor.toPlainText() == beforeNoSelection &&
                  highlightChanges == changesBeforeNoSelection,
              QStringLiteral("Ctrl+Shift+H without selected text should be a "
                             "no-op"));
        check(highlightChanges == 13,
              QStringLiteral("each successful Read Mode highlight edit should "
                             "emit exactly one autosave signal (actual %1)")
                  .arg(highlightChanges));
    }

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

    // A rendered wiki link can occupy several visual lines while remaining one
    // QTextBlock. Every visible segment should have its own clickable x range.
    editor.resize(180, 240);
    QString wrappedTarget;
    QTextCursor trailing;
    QTextBlock wrappedBlock;
    QTextLayout *wrappedLayout = nullptr;
    bool fixtureReady = false;
    int legacyLeft = 0;
    int legacyRight = 0;

    // Font metrics differ across platforms. Pick a target length whose final
    // wrapped segment is shorter than an earlier one instead of assuming that
    // a fixed character count always reproduces the old whole-token hit box.
    for (int length = 32; length <= 96 && !fixtureReady; ++length) {
        const QString candidate(length, QLatin1Char('W'));
        editor.setPlainText(QStringLiteral("[[") + candidate +
                            QStringLiteral("]]\nplain trailing line"));
        trailing = QTextCursor(editor.document()->findBlockByNumber(1));
        editor.setTextCursor(trailing); // conceal the link on block zero
        wrappedBlock = editor.document()->firstBlock();
        settleLayout(editor, wrappedBlock);
        wrappedLayout = wrappedBlock.layout();
        if (!wrappedLayout || wrappedLayout->lineCount() < 3)
            continue;

        QTextCursor sourceStart(wrappedBlock);
        sourceStart.setPosition(wrappedBlock.position());
        QTextCursor sourceEnd(wrappedBlock);
        sourceEnd.setPosition(wrappedBlock.position() + wrappedBlock.length() - 1);
        legacyLeft = editor.cursorRect(sourceStart).left();
        legacyRight = editor.cursorRect(sourceEnd).left();

        const int displayStart = 2;
        const int displayEnd = displayStart + candidate.size();
        for (int i = 0; i < wrappedLayout->lineCount(); ++i) {
            const QTextLine line = wrappedLayout->lineAt(i);
            const int overlapStart = qMax(displayStart, line.textStart());
            const int overlapEnd =
                qMin(displayEnd, line.textStart() + line.textLength());
            if (overlapStart >= overlapEnd)
                continue;
            QTextCursor before(wrappedBlock);
            before.setPosition(wrappedBlock.position() + overlapEnd - 1);
            QTextCursor after(wrappedBlock);
            after.setPosition(wrappedBlock.position() + overlapEnd);
            const int x = (editor.cursorRect(before).left() +
                           editor.cursorRect(after).left()) /
                          2;
            if (x < legacyLeft || x > legacyRight) {
                wrappedTarget = candidate;
                fixtureReady = true;
                break;
            }
        }
    }
    check(fixtureReady,
          QStringLiteral("the wrapped-link fixture should span visual lines "
                         "outside the old whole-token x range"));
    if (fixtureReady && wrappedLayout) {
        const int displayStart = 2;
        const int displayEnd = displayStart + wrappedTarget.size();
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

            jumpedTo.clear();
            editor.setTextCursor(trailing);
            sendMousePress(editor, click);
            check(jumpedTo == wrappedTarget,
                  QStringLiteral("wrapped wiki-link visual line %1 should be "
                                 "clickable")
                      .arg(i));
        }
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

    jumpedTo.clear();
    beginQuickJump(editor);
    sendKey(editor, QEvent::KeyPress, Qt::Key_X, Qt::AltModifier,
            QStringLiteral("x"));
    check(jumpedTo.isEmpty(),
          QStringLiteral("Quick Jump must reserve X for the shortcut "
                         "cheatsheet"));
    sendKey(editor, QEvent::KeyPress, Qt::Key_Q, Qt::AltModifier,
            QStringLiteral("q"));
    check(jumpedTo == QStringLiteral("First"),
          QStringLiteral("a reserved X should not poison the next valid Quick "
                         "Jump hint"));
    endQuickJump(editor);

    // Code-looking links and Markdown images are not navigable targets; an
    // external target after them still participates in the same ordering.
    QString notice;
    QObject::connect(&editor, &MarkdownEditor::noticeRequested,
                     [&notice](const QString &text) { notice = text; });
    editor.setPlainText(QStringLiteral(
        "`[[Code]]` ![image](missing.png) ![[photo.png|120]] [[Real]] "
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

    // X is reserved, leaving 25 hint keys. More than 25 visible links switch
    // the whole overlay to fixed-width hints; the first is QQ and item 26 is
    // WQ in the same physical-key order.
    QStringList manyLinks;
    for (int i = 1; i <= 26; ++i)
        manyLinks << QStringLiteral("[[Note %1]]").arg(i);
    editor.setPlainText(manyLinks.join(QLatin1Char('\n')));
    QApplication::processEvents();

    jumpedTo.clear();
    beginQuickJump(editor);
    sendKey(editor, QEvent::KeyPress, Qt::Key_Q, Qt::AltModifier,
            QStringLiteral("q"));
    check(jumpedTo.isEmpty(),
          QStringLiteral("a fixed-width Quick Jump hint should wait for key "
                         "two"));
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
    check(jumpedTo == QStringLiteral("Note 26"),
          QStringLiteral("Quick Jump WQ should open link 26"));
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
