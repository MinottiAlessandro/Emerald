#include "MarkdownEditor.h"

#include "AppTheme.h"
#include "MarkdownCallout.h"
#include "MarkdownHighlighter.h"
#include "MarkdownReadObjectRenderer.h"
#include "MarkdownReadRenderer.h"
#include "MarkdownStyle.h"
#include "MathRender.h"
#include "core/ContentSecurity.h"
#include "core/MarkdownComment.h"
#include "core/MarkdownImage.h"
#include "core/MascotSeed.h"
#include "core/Perf.h"
#include "core/SpellChecker.h"
#include "core/WikiLink.h"

#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QImageReader>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QStyleHints>
#include <QStringListModel>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector>
#include <QWheelEvent>
#include <QtMath>
#include <QUrl>
#include <algorithm>
#include <limits>
#include <utility>

namespace {
constexpr int CommentBlockState = 3; // MarkdownHighlighter::StateComment

MarkdownComment::LineAnalysis commentAnalysisForBlock(
    const QTextBlock &block) {
    const bool continuesFromPrevious =
        block.previous().isValid() &&
        block.previous().userState() == CommentBlockState;
    return MarkdownComment::analyzeLine(block.text(), continuesFromPrevious);
}

QString commentMaskedBlockText(const QTextBlock &block) {
    return commentAnalysisForBlock(block).masked(block.text());
}

bool cursorInsideComment(const QTextCursor &cursor) {
    if (cursor.isNull() || !cursor.block().isValid())
        return false;
    const MarkdownComment::LineAnalysis comments =
        commentAnalysisForBlock(cursor.block());
    const int column = cursor.positionInBlock();
    return comments.contains(column) ||
           (comments.continuesComment &&
            column == cursor.block().text().size());
}

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

// Hint labels follow the physical rows of a QWERTY keyboard rather than
// alphabetical order, keeping the most convenient keys spatially grouped.
// X belongs to the application-wide Alt+X shortcut cheatsheet. Keeping it out
// of this alphabet means Quick Jump never advertises or resolves the reserved
// chord as a link hint.
constexpr char QuickJumpKeys[] = "QWERTYUIOPASDFGHJKLZCVBNM";
constexpr int QuickJumpKeyCount = int(sizeof(QuickJumpKeys)) - 1;
constexpr int QuickJumpHoldMs = 100;

QColor ordinaryQuoteSurface(const QPalette &palette) {
    QColor accent = palette.color(QPalette::Highlight);
    if (!accent.isValid())
        accent = AppTheme::color(QColor(0x2b, 0xbf, 0x74));
    const QColor base = palette.color(QPalette::Base);
    const auto blend = [](int background, int foreground) {
        constexpr int alpha = 10;
        return (background * (255 - alpha) + foreground * alpha + 127) / 255;
    };
    return QColor(blend(base.red(), accent.red()),
                  blend(base.green(), accent.green()),
                  blend(base.blue(), accent.blue()));
}

QString quickJumpHint(int index, int width) {
    QString hint(width, QLatin1Char('Q'));
    for (int pos = width - 1; pos >= 0; --pos) {
        hint[pos] = QLatin1Char(QuickJumpKeys[index % QuickJumpKeyCount]);
        index /= QuickJumpKeyCount;
    }
    return hint;
}

QFont quickJumpFont(const QFont &base) {
    QFont result(base);
    result.setBold(true);
    if (base.pointSizeF() > 0)
        result.setPointSizeF(qMax(8.0, base.pointSizeF() * 0.78));
    return result;
}

QPixmap imagePreviewPixmap(const QString &path, QSize logicalMax, qreal dpr,
                           bool exactSize = false) {
    const QFileInfo info(path);
    if (!info.isFile() || logicalMax.isEmpty())
        return {};

    // Small resize changes should reuse the same decoded thumbnail instead of
    // filling QPixmapCache with nearly identical dimensions.
    logicalMax.setWidth(qMax(8, logicalMax.width() / 8 * 8));
    logicalMax.setHeight(qMax(8, logicalMax.height() / 8 * 8));

    const QSize deviceMax = (QSizeF(logicalMax) * dpr).toSize();
    const QString key =
        QStringLiteral("note-image:%1:%2:%3:%4:%5x%6:%7")
            .arg(info.absoluteFilePath())
            .arg(info.size())
            .arg(info.lastModified().toMSecsSinceEpoch())
            .arg(dpr)
            .arg(deviceMax.width())
            .arg(deviceMax.height())
            .arg(exactSize);

    QPixmap cached;
    if (QPixmapCache::find(key, &cached))
        return cached;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        reader.setScaledSize(exactSize
                                 ? deviceMax
                                 : sourceSize.scaled(deviceMax,
                                                     Qt::KeepAspectRatio));
    } else {
        reader.setScaledSize(deviceMax);
    }

    QImage image = reader.read();
    if (image.isNull())
        return {};
    if (image.size().width() > deviceMax.width() ||
        image.size().height() > deviceMax.height()) {
        image = image.scaled(deviceMax, exactSize ? Qt::IgnoreAspectRatio
                                                   : Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    QPixmap pm = QPixmap::fromImage(image);
    pm.setDevicePixelRatio(dpr);
    QPixmapCache::insert(key, pm);
    return pm;
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
    const QString t = text.trimmed();
    const QList<int> pipes = MarkdownHighlighter::tablePipePositions(t);
    QStringList cells;
    for (int i = 0; i + 1 < pipes.size(); ++i)
        cells.append(t.mid(pipes[i] + 1, pipes[i + 1] - pipes[i] - 1));
    for (QString &c : cells)
        c = c.trimmed();
    return cells;
}
int tableCellIndex(const QTextCursor &cursor) {
    const QList<int> pipes =
        MarkdownHighlighter::tablePipePositions(cursor.block().text());
    if (pipes.size() < 2)
        return 0;
    int pipesBeforeCaret = 0;
    for (int pipe : pipes)
        if (pipe < cursor.positionInBlock())
            ++pipesBeforeCaret;
    return qBound(0, pipesBeforeCaret - 1, int(pipes.size()) - 2);
}
int sepAlign(const QString &cell) { // 0 left, 1 right, 2 centre, 3 explicit-left
    const QString t = cell.trimmed();
    const bool l = t.startsWith(QLatin1Char(':'));
    const bool r = t.endsWith(QLatin1Char(':'));
    return (l && r) ? 2 : r ? 1 : l ? 3 : 0;
}
QString padCell(const QString &s, int width, int align) {
    const int pad =
        qMax(0, width - MarkdownHighlighter::inlinePreviewColumnCount(s));
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

QStringList imageFilePathsFromMimeData(const QMimeData *mime) {
    QStringList paths;
    if (!mime || !mime->hasUrls())
        return paths;

    for (const QUrl &url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (paths.contains(path))
            continue;
        const QFileInfo info(path);
        if (!info.isFile())
            continue;
        QImageReader reader(path);
        if (reader.canRead())
            paths.append(path);
    }
    return paths;
}

struct ListPrefix {
    int contentStart = -1;
    int markerStart = -1;
    int depth = 0;

    bool valid() const { return contentStart >= 0; }
};

ListPrefix listPrefix(const QString &text) {
    // Keep this syntax in step with continueList(): indentation, a bullet or
    // ordinal, and an optional task marker all belong to the hanging prefix.
    static const QRegularExpression re(QStringLiteral(
        "^(\\s*)(?:[-*+]|\\d+[.)])\\s+(?:\\[[ xX]\\]\\s+)?"));
    const auto match = re.match(text);
    if (!match.hasMatch())
        return {};

    int columns = 0;
    for (const QChar ch : match.captured(1))
        columns += ch == QLatin1Char('\t') ? 2 : 1;
    return {int(match.capturedEnd()), int(match.capturedEnd(1)),
            (columns + 1) / 2};
}

using QuotePrefix = MarkdownCallout::QuotePrefix;

QuotePrefix quotePrefix(const QString &text) {
    return MarkdownCallout::quotePrefix(text);
}

struct InlineHighlightSpan {
    int openStart = 0;
    int contentStart = 0;
    int contentEnd = 0;
};

// Find the ==...== pairs that Read Mode actually renders. This follows the
// same precedence as MarkdownReadRenderer::insertInline: link destinations,
// code and formula source are skipped, while link labels and nested emphasis
// remain eligible for highlighting.
void collectInlineHighlightSpans(const QString &text, int sourceOffset,
                                 QList<InlineHighlightSpan> *spans,
                                 const MarkdownImage::References &images) {
    if (!spans)
        return;
    int pos = 0;
    while (pos < text.size()) {
        if (text.at(pos) == QLatin1Char('\\') && pos + 1 < text.size()) {
            pos += 2;
            continue;
        }

        const MarkdownImage::Image image =
            MarkdownImage::imageAt(text, pos, images);
        if (image.valid) {
            pos += image.length;
            continue;
        }

        if (text.mid(pos, 2) == QStringLiteral("[[")) {
            const int end = text.indexOf(QStringLiteral("]]"), pos + 2);
            if (end >= 0) {
                const QString inside = text.mid(pos + 2, end - pos - 2);
                const int separator = inside.indexOf(QLatin1Char('|'));
                const int labelStart = separator >= 0 ? separator + 1 : 0;
                collectInlineHighlightSpans(
                    inside.mid(labelStart),
                    sourceOffset + pos + 2 + labelStart, spans, images);
                pos = end + 2;
                continue;
            }
        }

        if (text.at(pos) == QLatin1Char('[') &&
            (pos == 0 || text.at(pos - 1) != QLatin1Char('!'))) {
            const int labelEnd = text.indexOf(QStringLiteral("]("), pos + 1);
            if (labelEnd >= 0) {
                const int targetEnd =
                    text.indexOf(QLatin1Char(')'), labelEnd + 2);
                if (targetEnd >= 0) {
                    collectInlineHighlightSpans(
                        text.mid(pos + 1, labelEnd - pos - 1),
                        sourceOffset + pos + 1, spans, images);
                    pos = targetEnd + 1;
                    continue;
                }
            }
        }

        QString pairedMarker;
        bool highlight = false;
        if (text.mid(pos, 2) == QStringLiteral("**") ||
            text.mid(pos, 2) == QStringLiteral("__") ||
            text.mid(pos, 2) == QStringLiteral("~~")) {
            pairedMarker = text.mid(pos, 2);
        } else if (text.mid(pos, 2) == QStringLiteral("==")) {
            pairedMarker = QStringLiteral("==");
            highlight = true;
        }
        if (!pairedMarker.isEmpty()) {
            const int end = text.indexOf(pairedMarker, pos + 2);
            if (end > pos + 1) {
                if (highlight) {
                    spans->append({sourceOffset + pos, sourceOffset + pos + 2,
                                   sourceOffset + end});
                } else {
                    collectInlineHighlightSpans(
                        text.mid(pos + 2, end - pos - 2),
                        sourceOffset + pos + 2, spans, images);
                }
                pos = end + 2;
                continue;
            }
        }

        if (text.at(pos) == QLatin1Char('`')) {
            const int end = text.indexOf(QLatin1Char('`'), pos + 1);
            if (end > pos) {
                pos = end + 1;
                continue;
            }
        }

        if (text.at(pos) == QLatin1Char('$')) {
            const auto math = MathRender::pattern().match(text, pos);
            if (math.hasMatch() && math.capturedStart(0) == pos) {
                pos = math.capturedEnd(0);
                continue;
            }
        }

        if (text.at(pos) == QLatin1Char('*') ||
            text.at(pos) == QLatin1Char('_')) {
            const QString marker(text.at(pos));
            const int end = text.indexOf(marker, pos + 1);
            if (end > pos) {
                collectInlineHighlightSpans(
                    text.mid(pos + 1, end - pos - 1),
                    sourceOffset + pos + 1, spans, images);
                pos = end + 1;
                continue;
            }
        }
        ++pos;
    }
}

int highlightableContentStart(const QTextBlock &block) {
    if (!block.isValid())
        return 0;
    const QString text = commentMaskedBlockText(block);
    const QuotePrefix quote = quotePrefix(text);
    if (quote.depth > 0) {
        const int previousDepth =
            block.previous().isValid()
                ? quotePrefix(commentMaskedBlockText(block.previous())).depth
                : 0;
        const MarkdownCallout::TitleLine title =
            MarkdownCallout::titleLine(text, previousDepth);
        if (title.valid())
            return title.hasCustomTitle() ? title.titleStart : text.size();
        return quote.contentStart;
    }
    const ListPrefix list = listPrefix(text);
    if (list.valid())
        return list.contentStart;
    static const QRegularExpression heading(
        QStringLiteral("^#{1,6}\\s+"));
    const auto match = heading.match(text);
    return match.hasMatch() ? int(match.capturedEnd()) : 0;
}

struct HeadingSpacing {
    qreal topLines;
    qreal bottomLines;
};

HeadingSpacing headingSpacing(int level) {
    switch (level) {
    case 1:  return {0.90, 0.40};
    case 2:  return {0.75, 0.34};
    case 3:  return {0.62, 0.28};
    case 4:  return {0.50, 0.24};
    case 5:  return {0.40, 0.20};
    default: return {0.32, 0.18};
    }
}

}

MarkdownEditor::MarkdownEditor(QWidget *parent) : QTextEdit(parent) {
    setObjectName(QStringLiteral("editor"));
    setAccessibleName(tr("Note editor"));
    setAccessibleDescription(
        tr("Markdown note editor. Text can be selected and edited."));

    setFrameStyle(QFrame::NoFrame);
    setAcceptRichText(false);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // text always wraps
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    // QTextEdit deletes its internally-owned document when another one is
    // installed. Reparent the source under a neutral child owner so swapping to
    // the Read Mode document can never destroy source text or undo history.
    m_documentOwner = new QObject(this);
    m_sourceDocument = document();
    m_sourceDocument->setParent(m_documentOwner);
    watchScrollPastEnd(m_sourceDocument);
    m_readObjectRenderer = new MarkdownReadObjectRenderer(this);
    m_readDocument = createReadDocument();
    m_sourceCursor = QTextCursor(m_sourceDocument);
    // Keep the existing reading-friendly overscroll: one extra viewport lets
    // the final line rise to the top. QTextEdit's range is pixel-addressable,
    // unlike QPlainTextEdit's visual-line range, so it can also be animated.
    connect(verticalScrollBar(), &QAbstractSlider::rangeChanged, this,
            [this](int, int max) {
                if (m_adjustingScroll)
                    return;
                applyScrollPastEndRange(max);
            });
    scheduleScrollPastEndRangeUpdate();

    m_smoothScroll = new QVariantAnimation(this);
    m_smoothScroll->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_smoothScroll, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
                m_settingAnimatedScrollValue = true;
                verticalScrollBar()->setValue(qRound(value.toReal()));
                m_settingAnimatedScrollValue = false;
            });
    connect(m_smoothScroll, &QVariantAnimation::finished, this, [this] {
        m_smoothScrollTarget = verticalScrollBar()->value();
    });
    connect(verticalScrollBar(), &QAbstractSlider::sliderPressed, this,
            &MarkdownEditor::stopSmoothScroll);
    connect(verticalScrollBar(), &QAbstractSlider::valueChanged, this,
            [this](int value) {
                if (m_settingAnimatedScrollValue)
                    return;
                if (m_smoothScroll->state() == QAbstractAnimation::Running)
                    m_smoothScroll->stop();
                m_smoothScrollTarget = value;
            });

    // Default to the platform's UI monospace face (the CSS "ui-monospace"
    // idea): SF Mono / Menlo on macOS, Cascadia / Consolas on Windows, the
    // common mono faces on Linux, with the generic "monospace" alias last so a
    // fixed-width font is always found. The Monospace style hint lets the font
    // system pick a sensible substitute when none of the named families exist.
    QFont font;
    font.setFamilies({QStringLiteral("SF Mono"), QStringLiteral("Menlo"),
                      QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                      QStringLiteral("DejaVu Sans Mono"),
                      QStringLiteral("Liberation Mono"),
                      QStringLiteral("monospace")});
    font.setPointSize(12);
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    document()->setDefaultFont(font);
    document()->setDocumentMargin(16);

    // MainWindow caps and centers the editor's column to a comfortable reading
    // measure, so the editor itself just expands to fill it.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_highlighter = new MarkdownHighlighter(document());
    m_spellChecker = new SpellChecker(this);
    m_highlighter->setSpellChecker(m_spellChecker);

    m_quickJumpTimer = new QTimer(this);
    m_quickJumpTimer->setSingleShot(true);
    m_quickJumpTimer->setInterval(QuickJumpHoldMs);
    connect(m_quickJumpTimer, &QTimer::timeout, this,
            &MarkdownEditor::activateQuickJump);

    connect(this, &QTextEdit::cursorPositionChanged, this, [this] {
        if (m_switchingDocuments)
            return;
        if (m_readMode) {
            syncSourceCursorFromReadSelection();
            viewport()->update();
            return;
        }
        dismissCompletionIfOutOfContext();
        updateActiveHighlight();
        viewport()->update(); // repaint bullets as the active line moves
        updateMascotLineState(); // reveal/hide the header line as the caret moves

        const int cur = textCursor().blockNumber();
        if (!m_prettifying && cur != m_lastCursorBlock) {
            const QTextBlock prev = document()->findBlockByNumber(m_lastCursorBlock);
            // If the caret just left a table, align it.
            if (prev.isValid() && !insideCodeBlock(prev) &&
                isTableRow(commentMaskedBlockText(prev))) {
                int first = m_lastCursorBlock, last = m_lastCursorBlock;
                while (document()->findBlockByNumber(first - 1).isValid() &&
                       !insideCodeBlock(
                           document()->findBlockByNumber(first - 1)) &&
                       isTableRow(commentMaskedBlockText(
                           document()->findBlockByNumber(first - 1))))
                    --first;
                while (document()->findBlockByNumber(last + 1).isValid() &&
                       !insideCodeBlock(
                           document()->findBlockByNumber(last + 1)) &&
                       isTableRow(commentMaskedBlockText(
                           document()->findBlockByNumber(last + 1))))
                    ++last;
                if (cur < first || cur > last)
                    prettifyTableAt(m_lastCursorBlock);
            }
        }
        m_lastCursorBlock = textCursor().blockNumber();
    });
    // A selection can change without the caret position moving (e.g. extending
    // the anchor while the caret end stays put), and code / $$ blocks reveal
    // their raw source while a selection covers them — so refresh the
    // highlighter's active span on selection changes too, not just caret moves.
    // setActiveBlock early-returns when nothing actually changed, so the overlap
    // with cursorPositionChanged costs nothing.
    connect(this, &QTextEdit::selectionChanged, this, [this] {
        if (m_switchingDocuments)
            return;
        if (m_readMode) {
            syncSourceCursorFromReadSelection();
            viewport()->update();
            return;
        }
        dismissCompletionIfOutOfContext();
        updateActiveHighlight();
        viewport()->update();
    });
    // Keep folded sections hidden as the document is edited.
    connect(document(), &QTextDocument::contentsChanged, this, [this] {
        if (m_readMode)
            return;
        // Some edits move the caret to a new line without emitting
        // cursorPositionChanged — notably Ctrl+Backspace joining two lines. Re-
        // sync the active (revealed) block here too, or the merged line keeps
        // its markup concealed with no glyph painted (showing nothing at all).
        // Pass the anchor as well so this never collapses a live selection's
        // span (a $$ block stays revealed while selected).
        updateActiveHighlight();
        if (!m_applyingFolds && !m_folds.isEmpty())
            reapplyFolds();
        updateMascotLineState(); // keep the header hidden and the seed in sync
    });
    connect(document(), &QTextDocument::contentsChange, this,
            [this](int position, int charsRemoved, int charsAdded) {
                if (m_readMode || m_applyingVisualBlockFormats)
                    return;
                Q_UNUSED(charsRemoved);
                scheduleVisualBlockFormats(position, qMax(1, charsAdded));
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

void MarkdownEditor::setPlainText(const QString &text) {
    // Completion belongs to the old cursor context. QCompleter does not always
    // dismiss its popup when the editor document is replaced (notably when New
    // Note clears an unfinished [[link), and a stale visible popup would then
    // claim Enter in the new empty document.
    if (m_completer) {
        m_completer->popup()->hide();
        m_completer->setCompletionPrefix(QString());
    }

    if (m_readMode) {
        m_sourceDocument->setPlainText(text);
        updateImageReferences();
        m_sourceCursor = QTextCursor(m_sourceDocument);
        m_readCursorChanged = false;
        m_sourceDocument->clearUndoRedoStacks();
        m_sourceDocument->setModified(false);
        // The source document is hidden in Read Mode. QTextDocument already
        // notifies QSyntaxHighlighter about the replacement, so forcing a full
        // synchronous rehighlight here only parses every wiki link a second
        // time before the rendered document can be shown.
        rebuildReadDocument(0.0);
        updateMascotLineState();
        return;
    }

    QTextEdit::setPlainText(text);
    updateImageReferences();
    // A document replacement intentionally starts a fresh undo history. Finish
    // the derived block layout synchronously, then remove any paragraph-format
    // command Qt recorded after its own reset.
    applyVisualBlockFormats();
    document()->clearUndoRedoStacks();
    document()->setModified(false);
    // QTextEdit emitted replacement signals before the synchronous layout pass
    // above. Any already-queued pass is now presentation-only and must retain
    // this freshly-reset source state.
    if (m_visualFormatQueued)
        m_pendingVisualPreserveModification = true;
}

void MarkdownEditor::clear() { setPlainText(QString()); }

QString MarkdownEditor::toPlainText() const {
    return m_sourceDocument ? m_sourceDocument->toPlainText() : QString();
}

QTextCursor MarkdownEditor::sourceTextCursor() const {
    return m_readMode ? m_sourceCursor : textCursor();
}

void MarkdownEditor::setSourceTextCursor(const QTextCursor &cursor) {
    if (!m_sourceDocument)
        return;
    QTextCursor sourceCursor(m_sourceDocument);
    sourceCursor.setPosition(
        qBound(0, cursor.anchor(),
               qMax(0, m_sourceDocument->characterCount() - 1)));
    sourceCursor.setPosition(
        qBound(0, cursor.position(),
               qMax(0, m_sourceDocument->characterCount() - 1)),
        QTextCursor::KeepAnchor);
    m_sourceCursor = sourceCursor;
    if (!m_readMode) {
        setTextCursor(sourceCursor);
    } else if (m_readDocument) {
        const QTextCursor readCursor = MarkdownReadRenderer::mapToReadCursor(
            m_readDocument, sourceCursor);
        const bool wasSwitching = m_switchingDocuments;
        m_switchingDocuments = true;
        setTextCursor(readCursor);
        m_switchingDocuments = wasSwitching;
        m_readCursorChanged = false;
    }
}

void MarkdownEditor::undo() {
    if (m_readMode)
        return;
    const QString sourceBefore = toPlainText();
    while (document()->isUndoAvailable()) {
        QTextEdit::undo();
        if (toPlainText() != sourceBefore)
            break;
    }
}

void MarkdownEditor::redo() {
    if (m_readMode)
        return;
    const QString sourceBefore = toPlainText();
    while (document()->isRedoAvailable()) {
        QTextEdit::redo();
        if (toPlainText() != sourceBefore)
            break;
    }
}

void MarkdownEditor::copy() {
    if (m_readMode)
        copyReadSelection();
    else
        QTextEdit::copy();
}

void MarkdownEditor::setCompletions(const QStringList &titles) {
    m_completionModel->setStringList(titles);
}

void MarkdownEditor::setImagePaths(const QString &basePath,
                                   const QString &vaultRoot) {
    if (m_imageBasePath == basePath && m_imageRootPath == vaultRoot)
        return;
    m_imageBasePath = basePath;
    m_imageRootPath = vaultRoot;
    m_imageSizeCache.clear();
    m_imageSizeCacheOrder.clear();
    // MainWindow updates paths immediately before setPlainText when opening a
    // note; that single source replacement rebuilds Read Mode with the new
    // paths. Avoid parsing the old note once here and the new note again there.
    if (!m_readMode)
        applyImagePreviewFormats();
    viewport()->update();
}

void MarkdownEditor::applyFont(const QFont &font) {
    setFont(font);
    m_sourceDocument->setDefaultFont(font);
    if (m_highlighter)
        m_highlighter->setBaseSize(font.pointSizeF());
    if (m_readMode)
        rebuildReadDocument(currentScrollRatio());
    else
        applyLineSpacing(); // extra leading is measured in font line-heights
}

void MarkdownEditor::applyTheme() {
    if (m_highlighter)
        m_highlighter->applyTheme();
    if (m_readMode)
        rebuildReadDocument(currentScrollRatio());
    else
        applyVisualBlockFormats();
    viewport()->update();
}

void MarkdownEditor::setLineSpacing(int percent) {
    m_lineSpacing = qBound(100, percent, 300);
    applyLineSpacing();
}

void MarkdownEditor::applyLineSpacing() {
    if (m_readMode)
        rebuildReadDocument(currentScrollRatio());
    else
        applyVisualBlockFormats();
}

void MarkdownEditor::scheduleVisualBlockFormats(int position, int charsChanged,
                                                bool preserveModification) {
    if (m_readMode)
        return;
    const int start = qMax(0, position);
    const int end = start + qMax(1, charsChanged);
    m_pendingVisualFormatStart = m_pendingVisualFormatStart < 0
                                     ? start
                                     : qMin(m_pendingVisualFormatStart, start);
    m_pendingVisualFormatEnd = qMax(m_pendingVisualFormatEnd, end);
    if (m_visualFormatQueued) {
        m_pendingVisualPreserveModification =
            m_pendingVisualPreserveModification && preserveModification;
        return;
    }

    m_visualFormatQueued = true;
    m_pendingVisualPreserveModification = preserveModification;
    QTimer::singleShot(0, this, [this] {
        m_visualFormatQueued = false;
        const int pendingStart = m_pendingVisualFormatStart;
        const int pendingEnd = m_pendingVisualFormatEnd;
        const bool preserveModification =
            m_pendingVisualPreserveModification;
        m_pendingVisualFormatStart = -1;
        m_pendingVisualFormatEnd = -1;
        m_pendingVisualPreserveModification = false;
        // The callback may have been queued by the source document immediately
        // before Read Mode swapped in its presentation document.
        if (m_readMode)
            return;
        if (pendingStart >= 0) {
            QTextDocument *const formattedDocument = document();
            const bool wasModified = formattedDocument->isModified();
            const bool referencesChanged = updateImageReferences();
            if (referencesChanged)
                applyVisualBlockFormats();
            else
                applyVisualBlockFormats(pendingStart,
                                        qMax(1, pendingEnd - pendingStart));
            if (preserveModification && document() == formattedDocument)
                formattedDocument->setModified(wasModified);
        }
    });
}

bool MarkdownEditor::updateImageReferences() {
    if (!m_sourceDocument)
        return false;
    const MarkdownImage::References references =
        MarkdownImage::collectReferences(m_sourceDocument->toPlainText());
    if (references == m_imageReferences)
        return false;
    m_imageReferences = references;
    if (m_highlighter)
        m_highlighter->setImageReferences(m_imageReferences);
    return true;
}

void MarkdownEditor::applyVisualBlockFormats(int position, int charsChanged) {
    if (m_applyingVisualBlockFormats || !document())
        return;

    QTextBlock first = charsChanged < 0
                           ? document()->firstBlock()
                           : document()->findBlock(qMax(0, position));
    if (!first.isValid())
        return;
    if (charsChanged >= 0 && first.previous().isValid())
        first = first.previous(); // list/quote group edge spacing is contextual
    const int endPosition =
        charsChanged < 0
            ? qMax(0, document()->characterCount() - 1)
            : qMin(qMax(0, document()->characterCount() - 1),
                   position + qMax(1, charsChanged));
    QTextBlock last = document()->findBlock(endPosition);
    if (charsChanged >= 0 && last.next().isValid())
        last = last.next();

    // A callout title controls every subsequent quote row in its contiguous
    // group. Recompute that group as one unit after an edit, then cache the
    // result on each block for constant-time painting.
    while (first.previous().isValid() &&
           quotePrefix(commentMaskedBlockText(first.previous())).depth > 0 &&
           !insideCodeBlock(first.previous()))
        first = first.previous();
    while (last.next().isValid() &&
           quotePrefix(commentMaskedBlockText(last)).depth > 0 &&
           quotePrefix(commentMaskedBlockText(last.next())).depth > 0 &&
           !insideCodeBlock(last.next()))
        last = last.next();

    // List ancestry is contextual too. Expand a partial update to the edges of
    // its consecutive list run so every row receives an accurate parent block
    // even after indenting, outdenting, inserting, or deleting an item.
    const auto isStructuralList = [this](const QTextBlock &block) {
        const QString structure =
            commentAnalysisForBlock(block).masked(block.text());
        return block.isValid() && !insideCodeBlock(block) &&
               quotePrefix(structure).depth == 0 &&
               listPrefix(structure).valid();
    };
    while (first.previous().isValid() && isStructuralList(first) &&
           isStructuralList(first.previous()))
        first = first.previous();
    while (last.next().isValid() && isStructuralList(last) &&
           isStructuralList(last.next()))
        last = last.next();

    static const QRegularExpression fenceRe(
        QStringLiteral("^\\s*(```|~~~)"));
    bool inFence = false;
    QString fence;
    for (QTextBlock block = document()->firstBlock();
         block.isValid() && block != first; block = block.next()) {
        const auto match = fenceRe.match(block.text());
        if (!match.hasMatch())
            continue;
        if (!inFence) {
            inFence = true;
            fence = match.captured(1);
        } else if (match.captured(1) == fence) {
            inFence = false;
            fence.clear();
        }
    }

    m_applyingVisualBlockFormats = true;
    QTextCursor formatCursor = textCursor();
    bool formatEditOpen = false;
    const qreal extra = QFontMetricsF(font()).lineSpacing() *
                        (m_lineSpacing - 100) / 100.0;
    const qreal bodyLineHeight = QFontMetricsF(font()).lineSpacing();
    const qreal available =
        qMax(qreal(0), viewport()->width() - document()->documentMargin() * 2);
    QVector<QString> calloutTypes(1);
    struct ListAncestor {
        int depth = 0;
        int blockNumber = -1;
    };
    QVector<ListAncestor> listAncestors;
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        const auto fenceMatch = fenceRe.match(block.text());
        const bool insideFence = inFence;
        bool openingFence = false;
        bool closingFence = false;
        if (fenceMatch.hasMatch()) {
            if (!inFence) {
                openingFence = true;
                inFence = true;
                fence = fenceMatch.captured(1);
            } else if (fenceMatch.captured(1) == fence) {
                closingFence = true;
                inFence = false;
                fence.clear();
            }
        }
        const bool codeRegion = insideFence || openingFence;
        const MarkdownComment::LineAnalysis blockComments =
            codeRegion ? MarkdownComment::LineAnalysis{}
                       : commentAnalysisForBlock(block);
        const QString structureText =
            codeRegion ? block.text()
                       : blockComments.masked(block.text());
        const QuotePrefix quote =
            codeRegion ? QuotePrefix{} : quotePrefix(structureText);
        if (quote.depth == 0) {
            calloutTypes.resize(1);
            calloutTypes[0].clear();
        } else {
            calloutTypes.resize(quote.depth + 1);
        }
        const int previousCalloutQuoteDepth =
            block.previous().isValid() && !codeRegion
                ? quotePrefix(commentAnalysisForBlock(block.previous())
                                  .masked(block.previous().text()))
                      .depth
                : 0;
        const MarkdownCallout::TitleLine calloutTitle =
            quote.depth > 0
                ? MarkdownCallout::titleLine(structureText,
                                             previousCalloutQuoteDepth)
                : MarkdownCallout::TitleLine{};
        if (calloutTitle.valid())
            calloutTypes[quote.depth] = calloutTitle.type;
        int calloutDepth = 0;
        QString calloutType;
        for (int candidate = quote.depth; candidate > 0; --candidate) {
            if (!calloutTypes.at(candidate).isEmpty()) {
                calloutDepth = candidate;
                calloutType = calloutTypes.at(candidate);
                break;
            }
        }
        const bool isCalloutTitle =
            calloutTitle.valid() && calloutTitle.quote.depth == calloutDepth;
        const ListPrefix list = codeRegion || quote.depth > 0
                                    ? ListPrefix{}
                                    : listPrefix(structureText);
        int listParentBlock = -1;
        if (list.valid()) {
            while (!listAncestors.isEmpty() &&
                   listAncestors.constLast().depth >= list.depth)
                listAncestors.removeLast();
            if (!listAncestors.isEmpty())
                listParentBlock = listAncestors.constLast().blockNumber;
            listAncestors.append({list.depth, block.blockNumber()});
        } else {
            listAncestors.clear();
        }
        const int contentStart =
            quote.depth > 0 ? quote.contentStart
                            : list.contentStart;
        qreal prefixWidth = 0.0;
        qreal leadingWidth = 0.0;
        if (contentStart > 0) {
            // MarkdownHighlighter changes the advance of concealed markers
            // (notably the custom-painted task checkbox). Measure the prefix
            // from the actual shaped line so wrapped text starts where the
            // rendered content starts, whether this block is active or not.
            document()->documentLayout()->blockBoundingRect(block);
            QTextLayout *layout = block.layout();
            if (layout && layout->lineCount() > 0) {
                const QTextLine firstLine = layout->lineAt(0);
                const int firstEnd = firstLine.textStart() +
                                     firstLine.textLength();
                if (contentStart > firstLine.textStart() &&
                    contentStart <= firstEnd) {
                    prefixWidth = firstLine.cursorToX(contentStart) -
                                  firstLine.x();
                    if (list.valid() && list.markerStart > 0)
                        leadingWidth = firstLine.cursorToX(list.markerStart) -
                                       firstLine.x();
                }
            }
            if (prefixWidth <= 0.0 || prefixWidth >= available - 1.0)
                prefixWidth = 0.0;
        }

        QTextBlockFormat format = block.blockFormat();
        const int level = codeRegion ? 0 : headingLevel(structureText);
        const HeadingSpacing spacing = headingSpacing(qMax(1, level));
        const qreal quoteIndent = bodyLineHeight * 1.18;
        const qreal listIndent = bodyLineHeight * 1.05;
        const qreal markerWidth = qMax(qreal(0), prefixWidth - leadingWidth);
        const qreal codePadding = bodyLineHeight * 0.72;
        const qreal leftMargin =
            codeRegion ? codePadding
            : quote.depth > 0
                ? (quote.depth - 1) * quoteIndent +
                      (block.blockNumber() >= m_visualSelectionFirst &&
                               block.blockNumber() <= m_visualSelectionLast
                           ? prefixWidth
                           : 0.0)
                : list.valid() ? list.depth * listIndent + markerWidth : 0.0;
        const qreal rightMargin = codeRegion ? codePadding : 0.0;
        const qreal textIndent = codeRegion ? 0.0 : -prefixWidth;
        const int previousQuoteDepth =
            block.previous().isValid()
                ? quotePrefix(commentAnalysisForBlock(block.previous())
                                  .masked(block.previous().text()))
                      .depth
                : 0;
        const int nextQuoteDepth =
            block.next().isValid()
                ? quotePrefix(commentAnalysisForBlock(block.next())
                                  .masked(block.next().text()))
                      .depth
                : 0;
        const qreal quoteTop = quote.depth > 0 && previousQuoteDepth == 0
                                   ? bodyLineHeight * 0.18
                                   : 0.0;
        const qreal quoteBottom = quote.depth > 0 && nextQuoteDepth == 0
                                      ? bodyLineHeight * 0.18
                                      : 0.0;
        const bool previousIsList = block.previous().isValid() &&
                                    listPrefix(commentAnalysisForBlock(
                                                   block.previous())
                                                   .masked(block.previous().text()))
                                        .valid();
        const bool nextIsList = block.next().isValid() &&
                                listPrefix(commentAnalysisForBlock(block.next())
                                               .masked(block.next().text()))
                                    .valid();
        const qreal listTop = list.valid() && !previousIsList
                                  ? bodyLineHeight * 0.10
                                  : 0.0;
        const qreal listBottom = list.valid() && !nextIsList
                                     ? bodyLineHeight * 0.10
                                     : 0.0;
        const qreal codeTop = openingFence ? bodyLineHeight * 0.30 : 0.0;
        const qreal codeBottom = closingFence ? bodyLineHeight * 0.30 : 0.0;
        const bool imageLine =
            !codeRegion &&
            MarkdownImage::standaloneImage(structureText, m_imageReferences)
                .valid;
        const bool imageActive = !m_readMode &&
                                 block.blockNumber() >= m_visualSelectionFirst &&
                                 block.blockNumber() <= m_visualSelectionLast;
        // Keep a selected image block's geometry stable only while the mouse
        // endpoint is moving. Collapsing a tall preview during a bottom-up drag
        // moves the text beneath the pointer and can repeatedly flip between
        // source and preview. Once the button is released, the selected source
        // returns to ordinary text height so it does not leave a large gap.
        const bool reserveImageHeight =
            imageLine &&
            (!imageActive ||
             (m_mouseSelectionDrag && textCursor().hasSelection()));
        const qreal imageLineHeight =
            reserveImageHeight ? imagePreviewContentHeight(block) + 24.0
                               : 0.0;
        const int lineHeightType = reserveImageHeight
                                       ? QTextBlockFormat::FixedHeight
                                       : QTextBlockFormat::SingleHeight;
        const qreal topMargin =
            (level > 0 ? bodyLineHeight * spacing.topLines : 0.0) + quoteTop +
            listTop + codeTop;
        // Consecutive quote rows are one visual panel. Ordinary paragraph
        // bottom margins are outside QTextBlock backgrounds, so retaining the
        // configurable inter-row margin here would expose the editor canvas.
        const qreal rowExtra =
            quote.depth > 0 && nextQuoteDepth > 0 ? 0.0 : extra;
        const qreal bottomMargin =
            rowExtra +
            (level > 0 ? bodyLineHeight * spacing.bottomLines : 0.0) +
            quoteBottom + listBottom + codeBottom;
        // Quote surfaces are painted against the document content edge in
        // drawQuotePanels(). QTextDocument expands a block background all the
        // way to the viewport edge in Edit Mode, which makes a quote appear to
        // begin before the line itself.
        const QBrush blockBackground(Qt::NoBrush);
        const bool listMetadataChanged =
            list.valid()
                ? !format.hasProperty(MarkdownStyle::ListDepthProperty) ||
                      !format.hasProperty(
                          MarkdownStyle::ListParentBlockProperty) ||
                      format.property(MarkdownStyle::ListDepthProperty)
                              .toInt() != list.depth ||
                      format.property(
                                MarkdownStyle::ListParentBlockProperty)
                              .toInt() != listParentBlock
                : format.hasProperty(MarkdownStyle::ListDepthProperty) ||
                      format.hasProperty(
                          MarkdownStyle::ListParentBlockProperty);
        const bool changed = !qFuzzyCompare(format.leftMargin() + 1.0,
                                            leftMargin + 1.0) ||
                             !qFuzzyCompare(format.textIndent() + 1.0,
                                            textIndent + 1.0) ||
                             !qFuzzyCompare(format.rightMargin() + 1.0,
                                            rightMargin + 1.0) ||
                             !qFuzzyCompare(format.lineHeight() + 1.0,
                                            imageLineHeight + 1.0) ||
                             format.lineHeightType() != lineHeightType ||
                             !qFuzzyCompare(format.topMargin() + 1.0,
                                            topMargin + 1.0) ||
                             !qFuzzyCompare(format.bottomMargin() + 1.0,
                                            bottomMargin + 1.0) ||
                             format.background() != blockBackground ||
                             format.property(MarkdownStyle::CalloutTypeProperty)
                                     .toString() !=
                                 calloutType ||
                             format.property(MarkdownStyle::CalloutDepthProperty)
                                     .toInt() !=
                                 calloutDepth ||
                             format.property(MarkdownStyle::CalloutTitleProperty)
                                     .toBool() !=
                                 isCalloutTitle ||
                             listMetadataChanged;
        if (changed) {
            if (!formatEditOpen) {
                // Paragraph layout is derived from the source edit that caused
                // it. Join when Qt has already finalized that edit; source-aware
                // undo()/redo() also skip any format-only command Qt retains.
                formatCursor.joinPreviousEditBlock();
                formatEditOpen = true;
            }
            format.setLeftMargin(leftMargin);
            format.setTextIndent(textIndent);
            format.setRightMargin(rightMargin);
            format.setLineHeight(imageLineHeight, lineHeightType);
            format.setTopMargin(topMargin);
            format.setBottomMargin(bottomMargin);
            format.setBackground(blockBackground);
            if (calloutDepth > 0) {
                format.setProperty(MarkdownStyle::CalloutTypeProperty,
                                   calloutType);
                format.setProperty(MarkdownStyle::CalloutDepthProperty,
                                   calloutDepth);
                format.setProperty(MarkdownStyle::CalloutTitleProperty,
                                   isCalloutTitle);
            } else {
                format.clearProperty(MarkdownStyle::CalloutTypeProperty);
                format.clearProperty(MarkdownStyle::CalloutDepthProperty);
                format.clearProperty(MarkdownStyle::CalloutTitleProperty);
            }
            if (list.valid()) {
                format.setProperty(MarkdownStyle::ListDepthProperty,
                                   list.depth);
                format.setProperty(MarkdownStyle::ListParentBlockProperty,
                                   listParentBlock);
            } else {
                format.clearProperty(MarkdownStyle::ListDepthProperty);
                format.clearProperty(
                    MarkdownStyle::ListParentBlockProperty);
            }
            formatCursor.setPosition(block.position());
            formatCursor.setBlockFormat(format);
        }
        if (block == last)
            break;
    }
    if (formatEditOpen)
        formatCursor.endEditBlock();
    m_applyingVisualBlockFormats = false;
    viewport()->update();
}

bool MarkdownEditor::smoothScrollActive() const {
    return m_smoothScroll &&
           m_smoothScroll->state() == QAbstractAnimation::Running;
}

void MarkdownEditor::stopSmoothScroll() {
    if (m_smoothScroll &&
        m_smoothScroll->state() == QAbstractAnimation::Running)
        m_smoothScroll->stop();
    m_smoothScrollTarget = verticalScrollBar()->value();
}

void MarkdownEditor::watchScrollPastEnd(QTextDocument *watchedDocument) {
    if (!watchedDocument || !watchedDocument->documentLayout())
        return;
    connect(watchedDocument->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged, this,
            [this, watchedDocument](const QSizeF &) {
                if (document() == watchedDocument)
                    scheduleScrollPastEndRangeUpdate();
            });
}

void MarkdownEditor::applyScrollPastEndRange(int naturalMaximum) {
    if (m_adjustingScroll)
        return;
    QScrollBar *const bar = verticalScrollBar();
    const int extra = qMax(0, bar->pageStep() - 1);
    const int boundedNatural = qBound(
        0, naturalMaximum, std::numeric_limits<int>::max() - extra);
    const int extendedMaximum = boundedNatural + extra;
    if (bar->maximum() == extendedMaximum)
        return;

    m_adjustingScroll = true; // setMaximum re-emits rangeChanged
    bar->setMaximum(extendedMaximum);
    m_adjustingScroll = false;
}

void MarkdownEditor::scheduleScrollPastEndRangeUpdate() {
    if (m_scrollRangeUpdateQueued)
        return;
    m_scrollRangeUpdateQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_scrollRangeUpdateQueued = false;
        updateScrollPastEndRange();
    });
}

void MarkdownEditor::updateScrollPastEndRange() {
    if (!document() || !document()->documentLayout())
        return;
    const int viewportHeight = verticalScrollBar()->pageStep();
    const int documentHeight =
        document()->documentLayout()->documentSize().toSize().height();
    applyScrollPastEndRange(qMax(0, documentHeight - viewportHeight));
}

void MarkdownEditor::smoothScrollBy(qreal pixels, int durationMs) {
    QScrollBar *bar = verticalScrollBar();
    if (!bar || qFuzzyIsNull(pixels))
        return;

    const int base = smoothScrollActive() ? m_smoothScrollTarget : bar->value();
    m_smoothScrollTarget =
        qBound(bar->minimum(), qRound(base + pixels), bar->maximum());
    if (m_smoothScrollTarget == bar->value()) {
        stopSmoothScroll();
        return;
    }

    m_smoothScroll->stop();
    m_smoothScroll->setDuration(qBound(80, durationMs, 220));
    m_smoothScroll->setStartValue(bar->value());
    m_smoothScroll->setEndValue(m_smoothScrollTarget);
    m_smoothScroll->start();
}

QRectF MarkdownEditor::blockViewportRect(const QTextBlock &block) const {
    if (!block.isValid() || !document() || !document()->documentLayout())
        return {};
    QRectF rect = document()->documentLayout()->blockBoundingRect(block);
    rect.translate(-horizontalScrollBar()->value(),
                   -verticalScrollBar()->value());
    return rect;
}

QList<QRectF> MarkdownEditor::textRangeViewportRects(const QTextBlock &block,
                                                      int start,
                                                      int length) const {
    QList<QRectF> rects;
    if (!block.isValid() || length <= 0 || !block.layout())
        return rects;
    QTextLayout *layout = block.layout();
    if (layout->lineCount() == 0)
        return rects;

    QTextCursor origin(block);
    const QTextLine firstLine = layout->lineAt(0);
    const QRectF originRect = cursorRect(origin);
    const qreal xOffset = originRect.left() - firstLine.cursorToX(0);
    const qreal yOffset = originRect.top() - firstLine.y();
    const int end = start + length;
    for (int i = 0; i < layout->lineCount(); ++i) {
        const QTextLine line = layout->lineAt(i);
        const int lineStart = line.textStart();
        const int lineEnd = lineStart + line.textLength();
        const int from = qMax(start, lineStart);
        const int to = qMin(end, lineEnd);
        if (from >= to)
            continue;
        const qreal x1 = xOffset + line.cursorToX(from);
        const qreal x2 = xOffset + line.cursorToX(to);
        rects.append(QRectF(qMin(x1, x2), yOffset + line.y(),
                            qAbs(x2 - x1), line.height()));
    }
    return rects;
}

MarkdownImage::Image
MarkdownEditor::imageForBlock(const QTextBlock &block) const {
    if (!block.isValid() || insideCodeBlock(block))
        return {};
    return MarkdownImage::standaloneImage(commentMaskedBlockText(block),
                                           m_imageReferences);
}

QString MarkdownEditor::resolvedImagePath(const QTextBlock &block) const {
    if (!block.isValid() || m_imageBasePath.isEmpty() ||
        m_imageRootPath.isEmpty())
        return {};
    const MarkdownImage::Image image = imageForBlock(block);
    if (!image.valid || image.target.isEmpty())
        return {};
    return ContentSecurity::resolveLocalImage(image.target, m_imageBasePath,
                                              m_imageRootPath);
}

QSize MarkdownEditor::imageSourceSize(const QString &path) const {
    const QFileInfo info(path);
    if (!info.isFile())
        return {};
    const QString key = QStringLiteral("%1:%2:%3")
                            .arg(info.absoluteFilePath())
                            .arg(info.size())
                            .arg(info.lastModified().toMSecsSinceEpoch());
    const auto cached = m_imageSizeCache.constFind(key);
    if (cached != m_imageSizeCache.constEnd())
        return cached.value();

    QImageReader reader(info.absoluteFilePath());
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    m_imageSizeCache.insert(key, size);
    m_imageSizeCacheOrder.append(key);
    constexpr int MaxImageMetadataEntries = 256;
    while (m_imageSizeCacheOrder.size() > MaxImageMetadataEntries)
        m_imageSizeCache.remove(m_imageSizeCacheOrder.takeFirst());
    return size;
}

QSizeF MarkdownEditor::imagePreviewSize(const QTextBlock &block) const {
    const qreal margin = document()->documentMargin();
    const qreal maxWidth =
        qMax(qreal(48), viewport()->width() - margin * 2.0 - 24.0);
    const qreal maxHeight =
        qBound(qreal(120), viewport()->height() * 0.62, qreal(520));
    const MarkdownImage::Image image = imageForBlock(block);
    const QSize source = imageSourceSize(resolvedImagePath(block));
    QSizeF preferred;
    if (image.dimensions.width > 0) {
        const qreal width = image.dimensions.width;
        qreal height = image.dimensions.height;
        if (height <= 0 && source.isValid())
            height = width * source.height() / source.width();
        if (height <= 0)
            height = 96.0;
        preferred = QSizeF(width, height);
        if (preferred.width() <= maxWidth && preferred.height() <= maxHeight)
            return preferred;
    } else if (source.isValid()) {
        preferred = source;
    } else {
        preferred = QSizeF(qMin(qreal(360), maxWidth), 96.0);
    }
    return preferred.scaled(QSizeF(maxWidth, maxHeight),
                            Qt::KeepAspectRatio);
}

qreal MarkdownEditor::imagePreviewContentHeight(const QTextBlock &block) const {
    return imagePreviewSize(block).height();
}

QRectF MarkdownEditor::imagePreviewArea(const QTextBlock &block) const {
    QRectF geo = blockViewportRect(block);
    // QTextDocument's block bounding rect follows the concealed source glyphs
    // and can therefore be only a couple of pixels tall even though the fixed
    // paragraph line height reserves the full preview. Use that explicit
    // height for the custom-painted image surface.
    const QTextBlockFormat format = block.blockFormat();
    if (format.lineHeightType() == QTextBlockFormat::FixedHeight)
        geo.setHeight(qMax(geo.height(), format.lineHeight()));
    const qreal margin = document()->documentMargin() + 12.0;
    return QRectF(margin, geo.top() + 12.0,
                  qMax(qreal(0), viewport()->width() - margin * 2.0),
                  qMax(qreal(0), geo.height() - 24.0));
}

void MarkdownEditor::applyImagePreviewFormats() {
    if (m_applyingVisualBlockFormats || !document())
        return;
    m_applyingVisualBlockFormats = true;
    QTextCursor formatCursor = textCursor();
    bool formatEditOpen = false;
    for (QTextBlock block = document()->firstBlock(); block.isValid();
         block = block.next()) {
        if (!imageForBlock(block).valid)
            continue;
        const bool active = !m_readMode &&
                            block.blockNumber() >= m_visualSelectionFirst &&
                            block.blockNumber() <= m_visualSelectionLast;
        const bool reserveHeight =
            !active || (m_mouseSelectionDrag && textCursor().hasSelection());
        const qreal height = reserveHeight
                                 ? imagePreviewContentHeight(block) + 24.0
                                 : 0.0;
        const int type = reserveHeight ? QTextBlockFormat::FixedHeight
                                       : QTextBlockFormat::SingleHeight;
        QTextBlockFormat format = block.blockFormat();
        if (qFuzzyCompare(format.lineHeight() + 1.0, height + 1.0) &&
            format.lineHeightType() == type)
            continue;
        if (!formatEditOpen) {
            formatCursor.joinPreviousEditBlock();
            formatEditOpen = true;
        }
        format.setLineHeight(height, type);
        formatCursor.setPosition(block.position());
        formatCursor.setBlockFormat(format);
    }
    if (formatEditOpen)
        formatCursor.endEditBlock();
    m_applyingVisualBlockFormats = false;
}

QTextBlock MarkdownEditor::firstVisibleTextBlock() const {
    QTextBlock block = cursorForPosition(QPoint(0, 0)).block();
    if (!block.isValid())
        block = document()->firstBlock();
    while (block.previous().isValid() && block.previous().isVisible() &&
           blockViewportRect(block.previous()).bottom() > 0)
        block = block.previous();
    while (block.isValid() && !block.isVisible())
        block = block.next();
    return block;
}

void MarkdownEditor::jumpToMatch(const QString &text) {
    if (text.isEmpty())
        return;
    moveCursor(QTextCursor::Start);
    findAndCenter(text);
}

bool MarkdownEditor::findAndCenter(const QString &text,
                                   QTextDocument::FindFlags flags) {
    if (text.isEmpty() || !QTextEdit::find(text, flags))
        return false;
    centerCursor();
    // Revealing a selected Markdown construct can change paragraph geometry,
    // and a newly-opened note may not have completed its first viewport layout.
    // Re-centre once on the settled layout; the context object cancels this
    // safely if the editor is destroyed first.
    QTimer::singleShot(0, this, [this] { centerCursor(); });
    return true;
}

void MarkdownEditor::centerCursor() {
    stopSmoothScroll();
    const int delta = cursorRect().center().y() - viewport()->height() / 2;
    verticalScrollBar()->setValue(verticalScrollBar()->value() + delta);
}

void MarkdownEditor::setReadMode(bool enabled) {
    if (m_readMode == enabled)
        return;

    stopSmoothScroll();
    m_mouseSelectionDrag = false;
    const ScrollAnchor scrollAnchor = captureScrollAnchor();
    if (enabled) {
        m_sourceCursor = textCursor();
        m_editCursorWidth = qMax(1, cursorWidth());
        if (m_completer)
            m_completer->popup()->hide();
        cancelQuickJump();
        m_readMode = true;
        // The source document is about to be detached and remains authoritative
        // but invisible. Leave its existing highlighter formats alone instead
        // of synchronously re-parsing the entire note before rendering it.
        if (m_highlighter)
            m_highlighter->setSuspended(true);
        rebuildReadDocument();

        m_switchingDocuments = true;
        setDocument(m_readDocument);
        scheduleScrollPastEndRangeUpdate();
        m_switchingDocuments = false;
        reapplyFolds();
        setReadOnly(true);
        setCursorWidth(0);
        setSourceTextCursor(m_sourceCursor);
        setAccessibleName(tr("Note reader"));
        setAccessibleDescription(
            tr("Rendered note. Text can be selected and copied, or highlighted "
               "with Ctrl+Shift+H; task checkboxes can be toggled."));
        // QTextEdit::setDocument() marks the newly installed document dirty as
        // part of resetting its control. Presentation is derived state, never
        // a saveable edit, so normalize that Qt bookkeeping immediately.
        m_readDocument->setModified(false);
        restoreScrollAnchor(scrollAnchor);
    } else {
        if (m_readCursorChanged) {
            m_sourceCursor = MarkdownReadRenderer::mapToSourceCursor(
                m_sourceDocument, textCursor());
        }
        m_readCursorChanged = false;
        m_readMode = false;
        if (m_highlighter)
            m_highlighter->setSuspended(false);
        // A source edit can be followed immediately by the mode switch before
        // the queued incremental layout pass runs. Refresh cross-note image
        // definitions now so returning to Edit Mode cannot use a stale target.
        updateImageReferences();
        m_switchingDocuments = true;
        setDocument(m_sourceDocument);
        scheduleScrollPastEndRangeUpdate();
        setTextCursor(m_sourceCursor);
        m_switchingDocuments = false;
        reapplyFolds();
        setReadOnly(false);
        setCursorWidth(m_editCursorWidth);
        setAccessibleName(tr("Note editor"));
        setAccessibleDescription(
            tr("Markdown note editor. Text can be selected and edited."));

        updateActiveHighlight();
        if (m_highlighter)
            m_highlighter->rehighlight();
        applyVisualBlockFormats();
        updateMascotLineState();
        restoreScrollAnchor(scrollAnchor);
        // Release the rendered text promptly; edit mode keeps only the small
        // empty document shell until Read Mode is entered again.
        m_readDocument->clear();
        m_readDocument->setModified(false);
    }
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->update();
}

MarkdownEditor::ScrollAnchor MarkdownEditor::captureScrollAnchor() const {
    ScrollAnchor anchor;
    anchor.fallbackRatio = currentScrollRatio();
    if (!m_sourceDocument || !document())
        return anchor;

    // Anchor the visual line crossing the top of the viewport, not merely its
    // block. A long list item can wrap differently after its Markdown marker
    // is replaced in Read Mode; retaining the exact mapped character prevents
    // that reflow from advancing to a neighbouring row on every round trip.
    QTextCursor visible = cursorForPosition(
        QPoint(qMax(0, viewport()->width() / 2), 0));
    if (visible.isNull())
        return anchor;
    visible.clearSelection();

    QTextCursor source = visible;
    if (document() == m_readDocument) {
        source = MarkdownReadRenderer::mapToSourceCursor(m_sourceDocument,
                                                         visible);
    }
    if (source.isNull())
        return anchor;

    anchor.sourcePosition = qBound(
        0, source.position(), qMax(0, m_sourceDocument->characterCount() - 1));
    anchor.viewportOffset = cursorRect(visible).top();
    return anchor;
}

void MarkdownEditor::restoreScrollAnchor(const ScrollAnchor &anchor) {
    QTextDocument *const expected = document();
    const quint64 generation = ++m_scrollRestoreGeneration;
    const auto restore = [this, anchor, expected, generation] {
        if (document() != expected ||
            generation != m_scrollRestoreGeneration)
            return;
        if (anchor.sourcePosition < 0 || !m_sourceDocument) {
            const qreal ratio = qBound(0.0, anchor.fallbackRatio, 1.0);
            verticalScrollBar()->setValue(
                qRound(ratio * verticalScrollBar()->maximum()));
            m_smoothScrollTarget = verticalScrollBar()->value();
            return;
        }

        QTextCursor source(m_sourceDocument);
        source.setPosition(qBound(
            0, anchor.sourcePosition,
            qMax(0, m_sourceDocument->characterCount() - 1)));
        const QTextCursor target = document() == m_readDocument
                                       ? MarkdownReadRenderer::mapToReadCursor(
                                             m_readDocument, source)
                                       : source;
        if (target.isNull() || !target.block().isValid() ||
            !target.block().isVisible()) {
            const qreal ratio = qBound(0.0, anchor.fallbackRatio, 1.0);
            verticalScrollBar()->setValue(
                qRound(ratio * verticalScrollBar()->maximum()));
            m_smoothScrollTarget = verticalScrollBar()->value();
            return;
        }

        // cursorRect() is viewport-relative. Applying its delta to the current
        // scrollbar value avoids depending on QTextDocument's internal content
        // offset and keeps the same mapped character at the same pixel.
        const qreal currentOffset = cursorRect(target).top();
        verticalScrollBar()->setValue(qRound(verticalScrollBar()->value() +
                                             currentOffset -
                                             anchor.viewportOffset));
        m_smoothScrollTarget = verticalScrollBar()->value();
    };
    restore();
    // QTextDocument lays out lazily after setDocument(). Repeat once after the
    // event loop so a newly established scrollbar range cannot displace the
    // anchor. The document guard makes rapid mode toggles cancel stale work.
    QTimer::singleShot(0, this, restore);
}

qreal MarkdownEditor::currentScrollRatio() const {
    const int maximum = verticalScrollBar()->maximum();
    return maximum > 0
               ? qBound(0.0, qreal(verticalScrollBar()->value()) / maximum, 1.0)
               : 0.0;
}

void MarkdownEditor::restoreScrollRatio(qreal ratio) {
    if (ratio < 0.0)
        return;
    ratio = qBound(0.0, ratio, 1.0);
    QTextDocument *expected = document();
    const quint64 generation = ++m_scrollRestoreGeneration;
    const auto restore = [this, ratio, expected, generation] {
        if (document() != expected ||
            generation != m_scrollRestoreGeneration)
            return;
        verticalScrollBar()->setValue(
            qRound(ratio * verticalScrollBar()->maximum()));
        m_smoothScrollTarget = verticalScrollBar()->value();
    };
    restore();
    QTimer::singleShot(0, this, restore);
}

QTextDocument *MarkdownEditor::createReadDocument() {
    auto *readDocument = new QTextDocument(m_documentOwner);
    readDocument->documentLayout()->registerHandler(
        MarkdownReadObjectRenderer::ObjectType, m_readObjectRenderer);
    watchScrollPastEnd(readDocument);
    return readDocument;
}

void MarkdownEditor::rebuildReadDocument(qreal scrollRatio) {
    if (!m_readDocument || !m_sourceDocument)
        return;
    if (scrollRatio < 0.0 && document() == m_readDocument)
        scrollRatio = currentScrollRatio();
    const bool readDocumentInstalled = document() == m_readDocument;
    const bool cursorWasChanged = m_readCursorChanged;
    QTextCursor sourceCursor = m_sourceCursor;
    if (readDocumentInstalled && cursorWasChanged) {
        sourceCursor = MarkdownReadRenderer::mapToSourceCursor(
            m_sourceDocument, textCursor());
    }
    MarkdownReadRenderer::Options options;
    options.baseFont = font();
    options.lineSpacing = m_lineSpacing;
    options.imageBasePath = m_imageBasePath;
    options.vaultRootPath = m_imageRootPath;
    options.fallbackWidth = qMax(80, viewport()->width());
    options.maxImageHeight =
        qBound(qreal(120), viewport()->height() * 0.62, qreal(520));
    // Rebuilding the document currently installed in QTextEdit makes every
    // inserted block invalidate the live layout. A link navigation can then
    // enqueue thousands of viewport updates while the rendered note is being
    // assembled, leaving Read Mode sluggish long after the click. Build into a
    // detached document and swap it in once, so the visible editor observes one
    // layout change regardless of note size.
    QTextDocument *renderTarget =
        readDocumentInstalled ? createReadDocument() : m_readDocument;
    MarkdownReadRenderer::render(renderTarget,
                                 m_sourceDocument->toPlainText(), options);

    const bool wasSwitching = m_switchingDocuments;
    m_switchingDocuments = true;
    if (readDocumentInstalled) {
        QTextDocument *oldReadDocument = m_readDocument;
        m_readDocument = renderTarget;
        setDocument(m_readDocument);
        scheduleScrollPastEndRangeUpdate();
        setTextCursor(MarkdownReadRenderer::mapToReadCursor(
            m_readDocument, sourceCursor));
        // Presentation is derived state. QTextEdit may mark a newly installed
        // document dirty while resetting its control, but it must never look
        // like an editable/saveable note.
        m_readDocument->setModified(false);
        delete oldReadDocument;
        reapplyFolds();
    }
    m_switchingDocuments = wasSwitching;
    m_sourceCursor = sourceCursor;
    m_readCursorChanged = cursorWasChanged;
    if (readDocumentInstalled)
        restoreScrollRatio(scrollRatio);
}

void MarkdownEditor::syncSourceCursorFromReadSelection() {
    if (!m_readMode || !m_readDocument || document() != m_readDocument)
        return;
    m_sourceCursor = MarkdownReadRenderer::mapToSourceCursor(
        m_sourceDocument, textCursor());
    m_readCursorChanged = true;
}

QString MarkdownEditor::readSelectionText(
    const QTextCursor &selection) const {
    if (!selection.hasSelection() || selection.document() != m_readDocument)
        return {};

    QString result;
    result.reserve(selection.selectionEnd() - selection.selectionStart());
    for (int position = selection.selectionStart();
         position < selection.selectionEnd(); ++position) {
        const QChar character = m_readDocument->characterAt(position);
        if (character == QChar::ObjectReplacementCharacter) {
            QTextCursor object(m_readDocument);
            object.setPosition(position);
            object.movePosition(QTextCursor::NextCharacter,
                                QTextCursor::KeepAnchor);
            result += MarkdownReadObjectRenderer::accessibleText(
                object.charFormat());
        } else if (character == QChar::ParagraphSeparator) {
            result += QLatin1Char('\n');
        } else if (!character.isNull()) {
            result += character;
        }
    }
    return result;
}

void MarkdownEditor::copyReadSelection() {
    const QString text = readSelectionText(textCursor());
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

bool MarkdownEditor::toggleReadHighlight() {
    if (!m_readMode || !m_readDocument || !m_sourceDocument ||
        document() != m_readDocument)
        return false;
    const QTextCursor readSelection = textCursor();
    if (!readSelection.hasSelection())
        return false;

    const int selectionStart = readSelection.selectionStart();
    const int selectionEnd = readSelection.selectionEnd();
    const QColor highlightBackground = MarkdownStyle::highlightBackground();
    auto highlightedAt = [&](int position) {
        QTextCursor character(m_readDocument);
        character.setPosition(position);
        character.movePosition(QTextCursor::NextCharacter,
                               QTextCursor::KeepAnchor);
        return character.charFormat().background().color() ==
               highlightBackground;
    };
    auto rangeHasVisibleText = [&](int start, int end) {
        for (int position = start; position < end; ++position) {
            const QChar character = m_readDocument->characterAt(position);
            if (!character.isNull() && !character.isSpace() &&
                character != QChar::ObjectReplacementCharacter &&
                character != QChar::ParagraphSeparator)
                return true;
        }
        return false;
    };

    // The user's rule is word-oriented: punctuation or a paragraph separator
    // between highlighted words must not turn an unhighlight operation into an
    // add operation. If a selection contains no letters/numbers, fall back to
    // its other visible characters so a punctuation-only selection still acts
    // predictably.
    bool sawWordCharacter = false;
    bool everyWordCharacterHighlighted = true;
    bool sawVisibleCharacter = false;
    bool everyVisibleCharacterHighlighted = true;
    for (int position = selectionStart; position < selectionEnd; ++position) {
        const QChar character = m_readDocument->characterAt(position);
        if (character.isNull() || character.isSpace() ||
            character == QChar::ObjectReplacementCharacter ||
            character == QChar::ParagraphSeparator)
            continue;
        const bool highlighted = highlightedAt(position);
        sawVisibleCharacter = true;
        everyVisibleCharacterHighlighted &= highlighted;
        if (character.isLetterOrNumber()) {
            sawWordCharacter = true;
            everyWordCharacterHighlighted &= highlighted;
        }
    }
    if (!sawVisibleCharacter)
        return false;
    const bool removeHighlight =
        sawWordCharacter ? everyWordCharacterHighlighted
                         : everyVisibleCharacterHighlighted;

    struct ReadBlockSelection {
        QTextBlock readBlock;
        QTextBlock sourceBlock;
        int readStart = 0;
        int readEnd = 0;
    };
    QList<ReadBlockSelection> blockSelections;
    for (QTextBlock block = m_readDocument->findBlock(selectionStart);
         block.isValid() && block.position() < selectionEnd;
         block = block.next()) {
        const int readStart = qMax(selectionStart, block.position());
        const int readEnd =
            qMin(selectionEnd, block.position() + block.length() - 1);
        if (readStart >= readEnd ||
            !rangeHasVisibleText(readStart, readEnd))
            continue;

        QTextCursor part(m_readDocument);
        part.setPosition(readStart);
        part.setPosition(readEnd, QTextCursor::KeepAnchor);
        const QTextCursor sourcePart = MarkdownReadRenderer::mapToSourceCursor(
            m_sourceDocument, part);
        const QTextBlock sourceFirst =
            m_sourceDocument->findBlock(sourcePart.selectionStart());
        const QTextBlock sourceLast = m_sourceDocument->findBlock(
            qMax(sourcePart.selectionStart(), sourcePart.selectionEnd() - 1));
        if (!sourcePart.hasSelection() || !sourceFirst.isValid() ||
            sourceFirst != sourceLast)
            continue; // rendered objects and synthetic rows are not prose

        const int contentStart =
            sourceFirst.position() + highlightableContentStart(sourceFirst);
        if (sourcePart.selectionEnd() <= contentStart)
            continue; // generated bullet/callout title only
        blockSelections.append(
            {block, sourceFirst, readStart, readEnd});
    }
    if (blockSelections.isEmpty())
        return false;

    struct ReadHighlightSpan {
        InlineHighlightSpan source;
        int readStart = 0;
        int readEnd = 0;
    };
    QHash<int, int> removals;
    QSet<int> insertions;
    auto removeMarkerPair = [&](const InlineHighlightSpan &span) {
        removals.insert(span.openStart, 2);
        removals.insert(span.contentEnd, 2);
    };
    auto addMarkerPair = [&](int start, int end) {
        if (start >= end)
            return;
        insertions.insert(start);
        insertions.insert(end);
    };

    for (const ReadBlockSelection &selected : std::as_const(blockSelections)) {
        QList<InlineHighlightSpan> sourceSpans;
        collectInlineHighlightSpans(selected.sourceBlock.text(),
                                    selected.sourceBlock.position(),
                                    &sourceSpans, m_imageReferences);
        QList<ReadHighlightSpan> currentHighlights;
        for (const InlineHighlightSpan &span : std::as_const(sourceSpans)) {
            QTextCursor sourceSpan(m_sourceDocument);
            sourceSpan.setPosition(span.contentStart);
            sourceSpan.setPosition(span.contentEnd,
                                   QTextCursor::KeepAnchor);
            const QTextCursor rendered = MarkdownReadRenderer::mapToReadCursor(
                m_readDocument, sourceSpan);
            if (!rendered.hasSelection())
                continue;
            const int readStart = rendered.selectionStart();
            const int readEnd = rendered.selectionEnd();
            if (m_readDocument->findBlock(readStart) != selected.readBlock ||
                m_readDocument->findBlock(qMax(readStart, readEnd - 1)) !=
                    selected.readBlock)
                continue;
            bool visiblyHighlighted = false;
            for (int position = readStart; position < readEnd; ++position) {
                const QChar character =
                    m_readDocument->characterAt(position);
                if (!character.isSpace() &&
                    character != QChar::ObjectReplacementCharacter &&
                    highlightedAt(position)) {
                    visiblyHighlighted = true;
                    break;
                }
            }
            if (visiblyHighlighted)
                currentHighlights.append({span, readStart, readEnd});
        }

        auto sourceRangeForReadRange = [&](int readStart, int readEnd,
                                           int *sourceStart,
                                           int *sourceEnd) {
            // Keep Markdown markers snug against actual text. In particular,
            // removing the first word from ==first second== should produce
            // "first ==second==", not "first== second==".
            while (readStart < readEnd &&
                   m_readDocument->characterAt(readStart).isSpace())
                ++readStart;
            while (readEnd > readStart &&
                   m_readDocument->characterAt(readEnd - 1).isSpace())
                --readEnd;
            if (readStart >= readEnd)
                return false;
            QTextCursor rendered(m_readDocument);
            rendered.setPosition(readStart);
            rendered.setPosition(readEnd, QTextCursor::KeepAnchor);
            const QTextCursor source = MarkdownReadRenderer::mapToSourceCursor(
                m_sourceDocument, rendered);
            const int start = qMax(
                source.selectionStart(),
                selected.sourceBlock.position() +
                    highlightableContentStart(selected.sourceBlock));
            const int end = qMin(source.selectionEnd(),
                                 selected.sourceBlock.position() +
                                     selected.sourceBlock.text().size());
            if (start >= end)
                return false;
            *sourceStart = start;
            *sourceEnd = end;
            return true;
        };

        if (removeHighlight) {
            for (const ReadHighlightSpan &highlight :
                 std::as_const(currentHighlights)) {
                if (highlight.readEnd <= selected.readStart ||
                    highlight.readStart >= selected.readEnd)
                    continue;
                removeMarkerPair(highlight.source);

                const int prefixEnd =
                    qMin(selected.readStart, highlight.readEnd);
                if (highlight.readStart < prefixEnd &&
                    rangeHasVisibleText(highlight.readStart, prefixEnd)) {
                    int sourceStart = 0, sourceEnd = 0;
                    if (sourceRangeForReadRange(highlight.readStart, prefixEnd,
                                                &sourceStart, &sourceEnd))
                        addMarkerPair(sourceStart, sourceEnd);
                }

                const int suffixStart =
                    qMax(selected.readEnd, highlight.readStart);
                if (suffixStart < highlight.readEnd &&
                    rangeHasVisibleText(suffixStart, highlight.readEnd)) {
                    int sourceStart = 0, sourceEnd = 0;
                    if (sourceRangeForReadRange(suffixStart, highlight.readEnd,
                                                &sourceStart, &sourceEnd))
                        addMarkerPair(sourceStart, sourceEnd);
                }
            }
        } else {
            int targetStart = selected.readStart;
            int targetEnd = selected.readEnd;
            bool expanded = true;
            while (expanded) {
                expanded = false;
                for (const ReadHighlightSpan &highlight :
                     std::as_const(currentHighlights)) {
                    if (highlight.readEnd < targetStart ||
                        highlight.readStart > targetEnd)
                        continue;
                    const int nextStart =
                        qMin(targetStart, highlight.readStart);
                    const int nextEnd = qMax(targetEnd, highlight.readEnd);
                    if (nextStart != targetStart || nextEnd != targetEnd) {
                        targetStart = nextStart;
                        targetEnd = nextEnd;
                        expanded = true;
                    }
                }
            }
            for (const ReadHighlightSpan &highlight :
                 std::as_const(currentHighlights)) {
                if (highlight.readEnd < targetStart ||
                    highlight.readStart > targetEnd)
                    continue;
                removeMarkerPair(highlight.source);
            }
            int sourceStart = 0, sourceEnd = 0;
            if (sourceRangeForReadRange(targetStart, targetEnd, &sourceStart,
                                        &sourceEnd))
                addMarkerPair(sourceStart, sourceEnd);
        }
    }

    if (removals.isEmpty() && insertions.isEmpty())
        return false;

    const QTextCursor originalSourceSelection =
        MarkdownReadRenderer::mapToSourceCursor(m_sourceDocument,
                                                readSelection);
    const bool selectionForward =
        readSelection.anchor() <= readSelection.position();
    const int originalSelectionStart =
        originalSourceSelection.selectionStart();
    const int originalSelectionEnd = originalSourceSelection.selectionEnd();
    QList<int> editPositions = removals.keys();
    for (int position : std::as_const(insertions))
        if (!editPositions.contains(position))
            editPositions.append(position);

    std::sort(editPositions.begin(), editPositions.end());
    auto transformedBoundary = [&](int original, bool afterInsertionAtBoundary) {
        int delta = 0;
        for (int position : std::as_const(editPositions)) {
            const int removeLength = removals.value(position, 0);
            const int insertLength = insertions.contains(position) ? 2 : 0;
            if (original < position)
                break;
            if (original == position) {
                return position + delta +
                       (afterInsertionAtBoundary ? insertLength : 0);
            }
            if (removeLength > 0 && original < position + removeLength)
                return position + delta + insertLength;
            delta += insertLength - removeLength;
        }
        return original + delta;
    };
    const int transformedSelectionStart =
        transformedBoundary(originalSelectionStart, true);
    const int transformedSelectionEnd =
        transformedBoundary(originalSelectionEnd, false);

    std::sort(editPositions.begin(), editPositions.end(), std::greater<int>());

    QTextCursor edit(m_sourceDocument);
    edit.beginEditBlock();
    for (int position : std::as_const(editPositions)) {
        edit.setPosition(position);
        const int removeLength = removals.value(position, 0);
        if (removeLength > 0)
            edit.setPosition(position + removeLength,
                             QTextCursor::KeepAnchor);
        edit.insertText(insertions.contains(position)
                            ? QStringLiteral("==")
                            : QString());
    }
    edit.endEditBlock();

    QTextCursor restoredSourceSelection(m_sourceDocument);
    restoredSourceSelection.setPosition(selectionForward
                                            ? transformedSelectionStart
                                            : transformedSelectionEnd);
    restoredSourceSelection.setPosition(selectionForward
                                            ? transformedSelectionEnd
                                            : transformedSelectionStart,
                                        QTextCursor::KeepAnchor);
    m_sourceCursor = restoredSourceSelection;
    // The installed Read document still carries mappings for the pre-edit
    // source. Tell rebuildReadDocument to use the transformed source cursor
    // above instead of mapping that stale rendered selection one more time.
    m_readCursorChanged = false;
    const qreal scrollRatio = currentScrollRatio();
    rebuildReadDocument(scrollRatio);
    emit sourceChanged();
    return true;
}

QTextCharFormat MarkdownEditor::readObjectFormat(
    const QTextBlock &block) const {
    if (!block.isValid() || block.text().isEmpty() ||
        block.text().at(0) != QChar::ObjectReplacementCharacter)
        return {};
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    return cursor.charFormat();
}

QRectF MarkdownEditor::readObjectRect(const QTextBlock &block) const {
    if (!block.isValid() || !block.layout() ||
        block.layout()->lineCount() == 0)
        return {};
    const QTextLine line = block.layout()->lineAt(0);
    QTextCursor origin(block);
    const qreal xOffset = cursorRect(origin).left() - line.cursorToX(0);
    const qreal x1 = xOffset + line.cursorToX(0);
    const qreal x2 = xOffset + line.cursorToX(1);
    return QRectF(qMin(x1, x2), blockViewportRect(block).top() + line.y(),
                  qAbs(x2 - x1), line.height());
}

QTextBlock MarkdownEditor::readCheckboxBlockAt(const QPoint &pos) const {
    if (!m_readMode)
        return {};
    const QTextBlock block = cursorForPosition(pos).block();
    if (MarkdownReadObjectRenderer::kind(readObjectFormat(block)) !=
        MarkdownReadObjectRenderer::Kind::Checkbox)
        return {};
    return readObjectRect(block).adjusted(-2, -2, 2, 2).contains(pos)
               ? block
               : QTextBlock();
}

bool MarkdownEditor::toggleReadCheckboxAt(const QPoint &pos) {
    const QTextBlock readBlock = readCheckboxBlockAt(pos);
    if (!readBlock.isValid() || !m_sourceDocument)
        return false;

    QTextCursor readCursor(readBlock);
    const QTextCursor mapped = MarkdownReadRenderer::mapToSourceCursor(
        m_sourceDocument, readCursor);
    const QTextBlock sourceBlock = mapped.block();
    const auto match = taskRe().match(sourceBlock.text());
    if (!match.hasMatch())
        return false;

    const int statusPosition = match.capturedStart(2);
    const bool checked = sourceBlock.text().at(statusPosition).toLower() ==
                         QLatin1Char('x');
    QTextCursor edit(sourceBlock);
    edit.setPosition(sourceBlock.position() + statusPosition);
    edit.setPosition(sourceBlock.position() + statusPosition + 1,
                     QTextCursor::KeepAnchor);
    edit.insertText(checked ? QStringLiteral(" ") : QStringLiteral("x"));

    const bool nowChecked = !checked;
    const QFont objectFont = readObjectFormat(readBlock).font();
    QTextCursor objectCursor(readBlock);
    objectCursor.movePosition(QTextCursor::NextCharacter,
                              QTextCursor::KeepAnchor);
    objectCursor.setCharFormat(
        MarkdownReadObjectRenderer::checkboxFormat(objectFont, nowChecked));

    struct ReadFormatSpan {
        int start;
        int length;
        QTextCharFormat format;
    };
    QList<ReadFormatSpan> labelFormats;
    const int labelStart = readBlock.position() + 2; // object + compact space
    const int blockEnd = readBlock.position() + readBlock.length() - 1;
    for (auto it = readBlock.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid())
            continue;
        const int start = qMax(labelStart, fragment.position());
        const int end = qMin(blockEnd, fragment.position() + fragment.length());
        if (start >= end)
            continue;
        QTextCharFormat format = fragment.charFormat();
        format.setFontStrikeOut(nowChecked);
        const QColor foreground = format.foreground().color();
        const QColor body(0xd7, 0xee, 0xe2);
        const QColor completed(0x78, 0x93, 0x84);
        if (nowChecked && foreground == body)
            format.setForeground(completed);
        else if (!nowChecked && foreground == completed)
            format.setForeground(body);
        labelFormats.append({start, end - start, format});
    }
    for (const ReadFormatSpan &span : std::as_const(labelFormats)) {
        QTextCursor label(m_readDocument);
        label.setPosition(span.start);
        label.setPosition(span.start + span.length, QTextCursor::KeepAnchor);
        label.setCharFormat(span.format);
    }
    m_readDocument->setModified(false);
    emit sourceChanged();
    viewport()->update();
    return true;
}

bool MarkdownEditor::isOverReadCodeCopyButton(const QPoint &pos) const {
    if (!m_readMode)
        return false;
    const QTextBlock block = cursorForPosition(pos).block();
    const QTextCharFormat format = readObjectFormat(block);
    if (MarkdownReadObjectRenderer::kind(format) !=
        MarkdownReadObjectRenderer::Kind::CodeBlock)
        return false;
    const QRectF objectRect = readObjectRect(block);
    return objectRect.isValid() &&
           MarkdownReadObjectRenderer::codeCopyButtonRect(objectRect)
               .contains(pos);
}

bool MarkdownEditor::copyReadCodeBlockAt(const QPoint &pos) {
    if (!isOverReadCodeCopyButton(pos))
        return false;
    const QTextCharFormat format =
        readObjectFormat(cursorForPosition(pos).block());
    QApplication::clipboard()->setText(
        MarkdownReadObjectRenderer::codeText(format));
    emit noticeRequested(tr("Code copied"));
    return true;
}

void MarkdownEditor::updateActiveHighlight() {
    if (!m_highlighter)
        return;
    const int oldFirst = m_visualSelectionFirst;
    const int oldLast = m_visualSelectionLast;
    if (m_readMode) {
        // No active editing line: conceal Markdown source markers everywhere.
        m_highlighter->setActiveBlock(-1, -1, -1, false);
        m_visualSelectionFirst = -1;
        m_visualSelectionLast = -1;
        return;
    } else {
        const QTextCursor tc = textCursor();
        m_visualSelectionFirst = qMin(
            tc.blockNumber(), document()->findBlock(tc.anchor()).blockNumber());
        m_visualSelectionLast = qMax(
            tc.blockNumber(), document()->findBlock(tc.anchor()).blockNumber());
        m_highlighter->setActiveBlock(
            tc.blockNumber(),
            document()->findBlock(tc.anchor()).blockNumber(),
            tc.position() - tc.block().position(), tc.hasSelection());
    }

    // Revealing or concealing list markup changes the rendered width of its
    // prefix. Refresh just the union of the previous and current selection
    // spans so hanging indents follow the highlighter without scanning a large
    // note on every caret move.
    int firstNumber = m_visualSelectionFirst;
    int lastNumber = m_visualSelectionLast;
    if (oldFirst >= 0) {
        firstNumber = firstNumber < 0 ? oldFirst : qMin(firstNumber, oldFirst);
        lastNumber = qMax(lastNumber, oldLast);
    }
    if (firstNumber >= 0) {
        const QTextBlock first = document()->findBlockByNumber(firstNumber);
        const QTextBlock last = document()->findBlockByNumber(lastNumber);
        if (first.isValid() && last.isValid())
            scheduleVisualBlockFormats(first.position(),
                                       last.position() + last.length() -
                                           first.position(),
                                       true);
    }
}

void MarkdownEditor::setSpellCheckingEnabled(bool enabled) {
    if (!m_spellChecker || m_spellChecker->isEnabled() == enabled)
        return;
    m_spellChecker->setEnabled(enabled);
    if (m_highlighter)
        m_highlighter->rehighlight();
}

bool MarkdownEditor::spellCheckingEnabled() const {
    return m_spellChecker && m_spellChecker->isEnabled();
}

bool MarkdownEditor::setSpellCheckingLanguages(const QStringList &locales,
                                               QString *error) {
    if (!m_spellChecker)
        return false;
    if (m_spellChecker->languages() == locales && m_spellChecker->isReady())
        return true;
    if (!m_spellChecker->setLanguages(locales, error))
        return false;
    if (m_highlighter)
        m_highlighter->rehighlight();
    return true;
}

QStringList MarkdownEditor::spellCheckingLanguages() const {
    return m_spellChecker ? m_spellChecker->languages() : QStringList{};
}

bool MarkdownEditor::setSpellCheckingLanguage(const QString &locale,
                                              QString *error) {
    return setSpellCheckingLanguages({locale}, error);
}

QString MarkdownEditor::spellCheckingLanguage() const {
    return m_spellChecker ? m_spellChecker->language() : QString();
}

void MarkdownEditor::setSpellCheckingOptions(bool ignoreWordsWithNumbers,
                                             bool ignoreAllCaps) {
    if (!m_spellChecker)
        return;
    m_spellChecker->setOptions(ignoreWordsWithNumbers, ignoreAllCaps);
    if (m_highlighter)
        m_highlighter->rehighlight();
}

QString MarkdownEditor::misspelledWordAt(const QPoint &viewportPosition) const {
    if (m_readMode || !m_spellChecker || !m_spellChecker->isEnabled() ||
        !m_spellChecker->isReady())
        return {};
    const QTextCursor cursor = cursorForPosition(viewportPosition);
    const QTextBlock block = cursor.block();
    if (!block.isValid() || block.userState() == 1 || block.userState() == 2)
        return {};
    const int column = cursor.position() - block.position();
    const QString spellText = commentMaskedBlockText(block);
    for (const SpellChecker::WordRange &range :
         SpellChecker::wordsInMarkdown(spellText)) {
        if (column >= range.start && column <= range.start + range.length &&
            !m_spellChecker->isCorrect(range.word))
            return range.word;
    }
    return {};
}

QStringList MarkdownEditor::spellingSuggestions(const QString &word) const {
    return m_spellChecker ? m_spellChecker->suggestions(word) : QStringList{};
}

bool MarkdownEditor::replaceMisspelledWordAt(
    const QPoint &viewportPosition, const QString &expectedWord,
    const QString &replacement) {
    if (m_readMode || expectedWord.isEmpty() || replacement.isEmpty())
        return false;
    QTextCursor point = cursorForPosition(viewportPosition);
    const QTextBlock block = point.block();
    if (!block.isValid())
        return false;
    const int column = point.position() - block.position();
    const QString spellText = commentMaskedBlockText(block);
    for (const SpellChecker::WordRange &range :
         SpellChecker::wordsInMarkdown(spellText)) {
        if (column < range.start || column > range.start + range.length ||
            range.word != expectedWord)
            continue;
        QTextCursor edit(document());
        edit.setPosition(block.position() + range.start);
        edit.setPosition(block.position() + range.start + range.length,
                         QTextCursor::KeepAnchor);
        edit.insertText(replacement);
        setTextCursor(edit);
        return true;
    }
    return false;
}

bool MarkdownEditor::addToPersonalDictionary(const QString &word,
                                             QString *error) {
    if (!m_spellChecker ||
        !m_spellChecker->addToPersonalDictionary(word, error))
        return false;
    if (m_highlighter)
        m_highlighter->rehighlight();
    return true;
}

void MarkdownEditor::ignoreSpellingForSession(const QString &word) {
    if (!m_spellChecker)
        return;
    m_spellChecker->ignoreForSession(word);
    if (m_highlighter)
        m_highlighter->rehighlight();
}

QTextBlock MarkdownEditor::mascotBlock() const {
    const QTextBlock first = m_sourceDocument->firstBlock();
    return (first.isValid() && MascotSeed::fromLine(first.text()) != 0)
               ? first
               : QTextBlock();
}

quint64 MarkdownEditor::mascotSeed() const {
    return MascotSeed::fromLine(m_sourceDocument->firstBlock().text());
}

QString MarkdownEditor::mascotKind() const {
    return MascotSeed::kindFromLine(m_sourceDocument->firstBlock().text());
}

int MarkdownEditor::firstContentPosition() const {
    const QTextBlock mb = mascotBlock();
    if (mb.isValid() && mb.next().isValid())
        return mb.next().position();
    return 0;
}

QString MarkdownEditor::bodyText() const {
    // Preserve the historic mascot-body hash: its dedicated first line and
    // trailing newline disappear as one unit, then ordinary comments are
    // removed while retaining their structural newlines.
    return MarkdownComment::strip(MascotSeed::strip(toPlainText()));
}

void MarkdownEditor::setMascot(quint64 seed, const QString &kind) {
    const QTextBlock mb = mascotBlock();
    QTextCursor c(m_sourceDocument);
    c.beginEditBlock();
    if (seed == 0) {
        if (mb.isValid()) { // drop the header line and its trailing newline
            c.movePosition(QTextCursor::Start);
            c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
            c.removeSelectedText();
        }
    } else if (mb.isValid()) { // replace the existing header line's text
        c.setPosition(mb.position());
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        c.insertText(MascotSeed::line(seed, kind));
    } else { // insert a fresh header line above all existing content
        c.movePosition(QTextCursor::Start);
        c.insertText(MascotSeed::line(seed, kind) + QLatin1Char('\n'));
    }
    c.endEditBlock();
    if (m_readMode)
        rebuildReadDocument(currentScrollRatio());
    updateMascotLineState(); // hide the line + emit mascotSeedChanged
}

void MarkdownEditor::updateMascotLineState() {
    QTextBlock mb = mascotBlock();

    // Keep the header line hidden unless the caret rests on it (revealed via Up
    // at the top of the file). Toggling visibility needs a full relayout.
    if (mb.isValid()) {
        const bool onIt = !m_readMode && textCursor().blockNumber() == 0;
        if (mb.isVisible() != onIt) {
            mb.setVisible(onIt);
            m_sourceDocument->markContentsDirty(
                0, m_sourceDocument->characterCount());
            viewport()->update();
        }
    }

    const quint64 seed = mb.isValid() ? MascotSeed::fromLine(mb.text()) : 0;
    const QString kind = mb.isValid() ? MascotSeed::kindFromLine(mb.text()) : QString();
    if (seed != m_mascotSeed || kind != m_mascotKind) {
        m_mascotSeed = seed;
        m_mascotKind = kind;
        emit mascotSeedChanged(seed); // MainWindow re-reads the kind from us
    }
}

QString MarkdownEditor::wikiContextPrefix(bool *inContext) const {
    *inContext = false;
    const QTextCursor cursor = textCursor();
    const MarkdownComment::LineAnalysis comments =
        commentAnalysisForBlock(cursor.block());
    if (comments.contains(qMax(0, cursor.positionInBlock() - 1)))
        return {};
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

void MarkdownEditor::dismissCompletionIfOutOfContext() {
    if (!m_completer || !m_completer->popup()->isVisible())
        return;
    bool inContext = false;
    wikiContextPrefix(&inContext);
    if (inContext && !textCursor().hasSelection())
        return;
    m_completer->popup()->hide();
    m_completer->setCompletionPrefix(QString());
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

bool MarkdownEditor::pointInTextRange(const QPoint &pos,
                                      const QTextBlock &block, int startCol,
                                      int endCol) const {
    // cursorForPosition snaps a click in the blank area past the end of a line
    // onto the nearest character — for a line that ends with a [[link]] that is
    // the link itself. Confirm the click x actually lies within the token's
    // rendered horizontal span so the clickable area stops at the text. (The
    // concealed brackets collapse to ~0 width, so [start,end] tracks what's
    // visible.)
    const auto rects = textRangeViewportRects(block, startCol,
                                              qMax(0, endCol - startCol));
    return std::any_of(rects.cbegin(), rects.cend(), [&pos](const QRectF &rect) {
        return rect.adjusted(-1.0, 0.0, 1.0, 0.0).contains(pos);
    });
}

QString MarkdownEditor::linkAt(const QPoint &pos) const {
    if (m_readMode) {
        return MarkdownReadRenderer::wikiTargetFromHref(anchorAt(pos));
    }

    const QTextCursor cursor = cursorForPosition(pos);
    const QTextBlock block = cursor.block();
    const int column = cursor.positionInBlock();

    auto it = WikiLink::pattern().globalMatch(block.text());
    const MarkdownComment::LineAnalysis comments =
        commentAnalysisForBlock(block);
    while (it.hasNext()) {
        const auto m = it.next();
        if (MarkdownComment::overlaps(comments.ranges, m.capturedStart(0),
                                      m.capturedEnd(0)))
            continue;
        if (column >= m.capturedStart(0) && column <= m.capturedEnd(0) &&
            pointInTextRange(pos, block, int(m.capturedStart(0)),
                             int(m.capturedEnd(0))))
            return WikiLink::cleanDestination(m.captured(1));
    }
    return {};
}

QString MarkdownEditor::internetLinkAt(const QPoint &pos) const {
    if (m_readMode) {
        const QString href = anchorAt(pos);
        return MarkdownReadRenderer::wikiTargetFromHref(href).isEmpty()
                   ? href
                   : QString();
    }

    const QTextCursor cursor = cursorForPosition(pos);
    const QTextBlock block = cursor.block();
    const int column = cursor.positionInBlock();

    auto it = mdLinkRe().globalMatch(block.text());
    const MarkdownComment::LineAnalysis comments =
        commentAnalysisForBlock(block);
    while (it.hasNext()) {
        const auto m = it.next();
        if (MarkdownComment::overlaps(comments.ranges, m.capturedStart(0),
                                      m.capturedEnd(0)))
            continue;
        if (column >= m.capturedStart(0) && column <= m.capturedEnd(0) &&
            pointInTextRange(pos, block, int(m.capturedStart(0)),
                             int(m.capturedEnd(0))))
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
#ifdef Q_OS_MACOS
    if (mods & Qt::MetaModifier)
        return true;
#endif
    if (m_readMode)
        return true;
    // A plain click follows the link only when it is rendered, i.e. on a line
    // other than the one the cursor (active line) is on, where the markup is
    // still visible for editing.
    return cursorForPosition(pos).blockNumber() != textCursor().blockNumber();
}

void MarkdownEditor::armQuickJump() {
    m_quickJumpAltHeld = true;
    m_quickJumpArmed = true;
    m_quickJumpActive = false;
    m_quickJumpPrefix.clear();
    m_quickJumpTargets.clear();
    m_quickJumpTimer->start();
}

void MarkdownEditor::activateQuickJump() {
    if (!m_quickJumpAltHeld || !m_quickJumpArmed || !hasFocus())
        return;

    m_quickJumpArmed = false;
    refreshQuickJumpTargets();
    m_quickJumpActive = !m_quickJumpTargets.isEmpty();
    viewport()->update();
}

void MarkdownEditor::cancelQuickJump() {
    m_quickJumpTimer->stop();
    m_quickJumpArmed = false;
    m_quickJumpActive = false;
    m_quickJumpPrefix.clear();
    m_quickJumpTargets.clear();
    viewport()->update();
}

void MarkdownEditor::suppressQuickJump() {
    m_quickJumpAltHeld = false;
    cancelQuickJump();
}

QRectF MarkdownEditor::visibleLinkRect(const QTextBlock &block, int startCol,
                                       int endCol) const {
    const QRectF visible(QPointF(0, 0), viewport()->size());
    const auto rects = textRangeViewportRects(block, startCol,
                                              qMax(0, endCol - startCol));
    for (const QRectF &rect : rects)
        if (rect.intersects(visible))
            return rect.intersected(visible);
    return {};
}

void MarkdownEditor::refreshQuickJumpTargets() {
    struct Candidate {
        int sourceStart = 0;
        int displayStart = 0;
        int displayEnd = 0;
        QString destination;
        QuickJumpKind kind = QuickJumpKind::Wiki;
    };

    QList<QuickJumpTarget> targets;
    const QRectF viewportRect(QPointF(0, 0), viewport()->size());
    for (QTextBlock block = firstVisibleTextBlock(); block.isValid();
         block = block.next()) {
        if (!block.isVisible() || (!m_readMode && insideCodeBlock(block)))
            continue;
        const QRectF blockGeo = blockViewportRect(block);
        if (blockGeo.top() > viewportRect.bottom())
            break;
        if (blockGeo.bottom() < viewportRect.top())
            continue;

        QList<Candidate> candidates;
        if (m_readMode) {
            for (auto fragmentIt = block.begin(); !fragmentIt.atEnd();
                 ++fragmentIt) {
                const QTextFragment fragment = fragmentIt.fragment();
                if (!fragment.isValid())
                    continue;
                const QTextCharFormat format = fragment.charFormat();
                const QString href = format.anchorHref();
                if (!format.isAnchor() || href.isEmpty())
                    continue;

                const int start = fragment.position() - block.position();
                const int end = start + fragment.length();
                const QString wikiTarget =
                    MarkdownReadRenderer::wikiTargetFromHref(href);
                const QuickJumpKind kind = wikiTarget.isEmpty()
                                               ? QuickJumpKind::External
                                               : QuickJumpKind::Wiki;
                const QString destination =
                    wikiTarget.isEmpty() ? href : wikiTarget;
                if (!candidates.isEmpty() &&
                    candidates.last().displayEnd == start &&
                    candidates.last().destination == destination &&
                    candidates.last().kind == kind) {
                    candidates.last().displayEnd = end;
                } else {
                    candidates.append(
                        {start, start, end, destination, kind});
                }
            }
        } else {
            const QString text = block.text();
            const MarkdownComment::LineAnalysis comments =
                commentAnalysisForBlock(block);
            QList<QPair<int, int>> codeSpans;
            static const QRegularExpression inlineCodeRe(
                QStringLiteral("`[^`]+`"));
            auto codeIt = inlineCodeRe.globalMatch(text);
            while (codeIt.hasNext()) {
                const auto match = codeIt.next();
                codeSpans.append(
                    {match.capturedStart(), match.capturedEnd()});
            }
            const auto overlapsCode = [&codeSpans](int start, int end) {
                return std::any_of(codeSpans.cbegin(), codeSpans.cend(),
                                   [start, end](const auto &span) {
                                       return start < span.second &&
                                              end > span.first;
                                   });
            };
            const QVector<MarkdownImage::Image> images =
                MarkdownImage::imagesInLine(text, m_imageReferences, true);
            const auto overlapsImage = [&images](int start, int end) {
                return std::any_of(
                    images.cbegin(), images.cend(),
                    [start, end](const MarkdownImage::Image &image) {
                        return start < image.start + image.length &&
                               end > image.start;
                    });
            };

            auto wikiIt = WikiLink::pattern().globalMatch(text);
            while (wikiIt.hasNext()) {
                const auto match = wikiIt.next();
                if (overlapsCode(match.capturedStart(), match.capturedEnd()) ||
                    overlapsImage(match.capturedStart(), match.capturedEnd()) ||
                    MarkdownComment::overlaps(
                        comments.ranges, match.capturedStart(),
                        match.capturedEnd()))
                    continue;
                const QString inner = match.captured(1);
                const int pipe = inner.indexOf(QLatin1Char('|'));
                const int displayStart = match.capturedStart(1) +
                                         (pipe >= 0 ? pipe + 1 : 0);
                candidates.append({int(match.capturedStart()), displayStart,
                                   int(match.capturedEnd(1)),
                                   WikiLink::cleanDestination(inner),
                                   QuickJumpKind::Wiki});
            }

            auto mdIt = mdLinkRe().globalMatch(text);
            while (mdIt.hasNext()) {
                const auto match = mdIt.next();
                const int start = match.capturedStart();
                if ((start > 0 &&
                     text.at(start - 1) == QLatin1Char('!')) ||
                    overlapsCode(start, match.capturedEnd()) ||
                    overlapsImage(start, match.capturedEnd()) ||
                    MarkdownComment::overlaps(comments.ranges, start,
                                              match.capturedEnd()))
                    continue;
                candidates.append({start, int(match.capturedStart(1)),
                                   int(match.capturedEnd(1)), match.captured(2),
                                   QuickJumpKind::External});
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate &left, const Candidate &right) {
                      return left.sourceStart < right.sourceStart;
                  });
        for (const Candidate &candidate : candidates) {
            if (candidate.destination.isEmpty())
                continue;
            const QRectF linkRect = visibleLinkRect(
                block, candidate.displayStart, candidate.displayEnd);
            if (linkRect.isEmpty())
                continue;
            QuickJumpTarget target;
            target.destination = candidate.destination;
            target.linkRect = linkRect;
            target.kind = candidate.kind;
            targets.append(target);
        }
    }

    int width = 1;
    qsizetype capacity = QuickJumpKeyCount;
    while (targets.size() > capacity) {
        ++width;
        capacity *= QuickJumpKeyCount;
    }

    const QFontMetricsF metrics(quickJumpFont(font()));
    for (int i = 0; i < targets.size(); ++i) {
        QuickJumpTarget &target = targets[i];
        target.hint = quickJumpHint(i, width);
        const qreal badgeWidth =
            qMax(16.0, metrics.horizontalAdvance(target.hint) + 8.0);
        const qreal badgeHeight = qMax(16.0, metrics.height() + 4.0);
        qreal x = target.linkRect.right() + 4.0;
        if (x + badgeWidth > viewportRect.right() - 2.0)
            x = target.linkRect.left() - badgeWidth - 4.0;
        x = qBound(2.0, x,
                   qMax(2.0, viewportRect.right() - badgeWidth - 2.0));
        const qreal y = qBound(
            2.0, target.linkRect.center().y() - badgeHeight / 2.0,
            qMax(2.0, viewportRect.bottom() - badgeHeight - 2.0));
        target.badgeRect = QRectF(x, y, badgeWidth, badgeHeight);
    }
    m_quickJumpTargets = std::move(targets);
}

void MarkdownEditor::drawQuickJumpOverlay(QPainter &painter) {
    if (!m_quickJumpActive)
        return;

    // Scrolling while Alt is held changes the visible target set. Rebuild only
    // for this transient overlay so normal painting stays allocation-free and
    // hints always remain attached to what is actually on screen.
    refreshQuickJumpTargets();
    const QFont oldFont = painter.font();
    painter.setFont(quickJumpFont(font()));
    painter.setPen(QPen(AppTheme::color(QColor(0x0b, 0x24, 0x18)), 1));
    for (const QuickJumpTarget &target : std::as_const(m_quickJumpTargets)) {
        if (!target.hint.startsWith(m_quickJumpPrefix))
            continue;
        painter.setBrush(AppTheme::color(QColor(0x39, 0xd9, 0x83)));
        painter.drawRoundedRect(target.badgeRect, 4, 4);
        painter.drawText(target.badgeRect, Qt::AlignCenter, target.hint);
    }
    painter.setFont(oldFont);
}

void MarkdownEditor::openQuickJumpTarget(const QuickJumpTarget &target) {
    const QString destination = target.destination;
    const QuickJumpKind kind = target.kind;
    cancelQuickJump();
    if (kind == QuickJumpKind::Wiki) {
        emit linkClicked(destination);
        return;
    }

    const QUrl safeUrl = ContentSecurity::externalUrl(destination);
    if (safeUrl.isValid())
        QDesktopServices::openUrl(safeUrl);
    else
        emit noticeRequested(tr("Blocked unsafe link"));
}

bool MarkdownEditor::handleQuickJumpKey(QKeyEvent *event) {
    if (!m_quickJumpActive)
        return false;

    if (event->key() == Qt::Key_Escape) {
        cancelQuickJump();
        return true;
    }

    // Existing Alt+arrow editing and history shortcuts keep precedence.
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down ||
        event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        cancelQuickJump();
        return false;
    }

    const int keyIndex = event->key() - Qt::Key_A;
    if (keyIndex < 0 || keyIndex >= 26) {
        cancelQuickJump();
        return false;
    }

    const QChar pressed = QLatin1Char(char('A' + keyIndex));
    m_quickJumpPrefix.append(pressed);
    for (const QuickJumpTarget &target : std::as_const(m_quickJumpTargets)) {
        if (target.hint == m_quickJumpPrefix) {
            openQuickJumpTarget(target);
            return true;
        }
    }

    const bool hasMatch = std::any_of(
        m_quickJumpTargets.cbegin(), m_quickJumpTargets.cend(),
        [this](const QuickJumpTarget &target) {
            return target.hint.startsWith(m_quickJumpPrefix);
        });
    if (!hasMatch)
        m_quickJumpPrefix.clear();
    viewport()->update();
    return true;
}

QTextBlock MarkdownEditor::taskCheckboxBlockAt(const QPoint &pos) const {
    const QTextBlock block = cursorForPosition(pos).block();
    if (block.blockNumber() == textCursor().blockNumber())
        return {}; // the active line shows raw markup; edit it normally
    const auto m = taskRe().match(commentMaskedBlockText(block));
    if (!m.hasMatch())
        return {};
    return taskCheckboxRect(block).adjusted(-3, -2, 3, 2).contains(pos)
               ? block
               : QTextBlock();
}

QRectF MarkdownEditor::taskCheckboxRect(const QTextBlock &block) const {
    if (!block.isValid())
        return {};
    const auto match = taskRe().match(commentMaskedBlockText(block));
    if (!match.hasMatch())
        return {};
    const int markerPosition = match.capturedLength(1);
    const auto markerRects =
        textRangeViewportRects(block, markerPosition, 1);
    if (markerRects.isEmpty())
        return {};
    const QRectF markerCell = markerRects.first();
    const QFontMetricsF metrics(font());
    const qreal size = metrics.ascent() * 0.92;
    return QRectF(markerCell.left(), markerCell.center().y() - size / 2.0,
                  size, size);
}

bool MarkdownEditor::toggleTaskAt(const QPoint &pos) {
    const QTextBlock block = taskCheckboxBlockAt(pos);
    if (!block.isValid())
        return false;
    const auto m = taskRe().match(commentMaskedBlockText(block));
    const int statusPos = m.capturedStart(2);
    QTextCursor edit(block);
    edit.setPosition(block.position() + statusPos);
    edit.setPosition(block.position() + statusPos + 1, QTextCursor::KeepAnchor);
    const bool done = block.text().at(statusPos).toLower() == QLatin1Char('x');
    edit.insertText(done ? QStringLiteral(" ") : QStringLiteral("x"));
    return true;
}

bool MarkdownEditor::isOverFoldControl(const QPoint &pos) const {
    const QTextBlock block = cursorForPosition(pos).block();
    return foldAnchorFoldable(block) && foldControlRect(block).contains(pos);
}

QRectF MarkdownEditor::foldControlRect(const QTextBlock &block) const {
    if (!block.isValid())
        return {};
    const QTextBlock sourceBlock = sourceBlockForDisplay(block);
    if (sourceBlock.isValid() &&
        listPrefix(commentMaskedBlockText(sourceBlock)).valid()) {
        const QRectF marker = listMarkerRect(block);
        if (marker.isValid()) {
            constexpr qreal Width = 12.0;
            constexpr qreal Gap = 2.0;
            return QRectF(qMax(qreal(0), marker.left() - Width - Gap),
                          marker.top(), Width, marker.height());
        }
    }
    const QRectF geo = blockViewportRect(block);
    return QRectF(0, geo.top(), document()->documentMargin(), geo.height());
}

QRectF MarkdownEditor::listMarkerRect(const QTextBlock &block) const {
    if (!block.isValid())
        return {};
    int markerPosition = 0;
    if (!m_readMode) {
        const ListPrefix prefix = listPrefix(commentMaskedBlockText(block));
        if (!prefix.valid())
            return {};
        markerPosition = prefix.markerStart;
    } else if (!block.blockFormat().hasProperty(
                   MarkdownStyle::ListDepthProperty)) {
        return {};
    }

    const QList<QRectF> markerRects =
        textRangeViewportRects(block, markerPosition, 1);
    if (!markerRects.isEmpty())
        return markerRects.first();
    QTextCursor marker(block);
    marker.setPosition(block.position() + markerPosition);
    return cursorRect(marker);
}

void MarkdownEditor::mousePressEvent(QMouseEvent *event) {
    stopSmoothScroll();
    if (event->button() == Qt::LeftButton)
        m_mouseSelectionDrag = false;
    if (m_quickJumpArmed || m_quickJumpActive)
        cancelQuickJump();
    if (event->button() == Qt::BackButton) {
        emit navigateBack();
        return;
    }
    if (event->button() == Qt::ForwardButton) {
        emit navigateForward();
        return;
    }
    if (m_readMode && event->button() == Qt::LeftButton &&
        toggleReadCheckboxAt(event->pos()))
        return;
    if (m_readMode && event->button() == Qt::LeftButton &&
        copyReadCodeBlockAt(event->pos()))
        return;
    // A click in the left margin next to a heading folds/unfolds its section.
    if (event->button() == Qt::LeftButton && isOverFoldControl(event->pos())) {
        toggleFoldAt(cursorForPosition(event->pos()).block());
        return;
    }
    if (event->button() == Qt::LeftButton && copyCodeBlockAt(event->pos()))
        return;
    if (!m_readMode && event->button() == Qt::LeftButton &&
        toggleTaskAt(event->pos()))
        return;
    if (event->button() == Qt::LeftButton &&
        followsLink(event->pos(), event->modifiers())) {
        const QString url = internetLinkAt(event->pos());
        if (!url.isEmpty()) {
            const QUrl safeUrl = ContentSecurity::externalUrl(url);
            if (safeUrl.isValid())
                QDesktopServices::openUrl(safeUrl);
            else
                emit noticeRequested(tr("Blocked unsafe link"));
        } else {
            emit linkClicked(linkAt(event->pos()));
        }
        return;
    }
    if (!m_readMode && event->button() == Qt::LeftButton)
        m_mouseSelectionDrag = true;
    QTextEdit::mousePressEvent(event);
}

void MarkdownEditor::mouseReleaseEvent(QMouseEvent *event) {
    const bool selectionDragEnded =
        event->button() == Qt::LeftButton && m_mouseSelectionDrag;
    QTextEdit::mouseReleaseEvent(event);
    if (!selectionDragEnded)
        return;

    m_mouseSelectionDrag = false;
    if (m_readMode)
        return;

    // Selection changes during the drag intentionally retained preview
    // geometry. Reapply the image format synchronously now so the released
    // selection shows compact Markdown source without a one-frame stale gap.
    updateActiveHighlight();
    applyImagePreviewFormats();
    viewport()->update();
}

void MarkdownEditor::mouseMoveEvent(QMouseEvent *event) {
    // Show the hand cursor over anything clickable: links, task checkboxes, the
    // heading fold control, and a code block's copy button.
    const QPoint p = event->pos();
    const bool clickable = followsLink(p, event->modifiers()) ||
                           (m_readMode && readCheckboxBlockAt(p).isValid()) ||
                           (m_readMode && isOverReadCodeCopyButton(p)) ||
                           (!m_readMode && taskCheckboxBlockAt(p).isValid()) ||
                           isOverFoldControl(p) || isOverCopyButton(p);
    viewport()->setCursor(clickable ? Qt::PointingHandCursor : Qt::IBeamCursor);
    QTextEdit::mouseMoveEvent(event);
}

void MarkdownEditor::wheelEvent(QWheelEvent *event) {
    const auto mods = event->modifiers();
    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        stopSmoothScroll();
        QTextEdit::wheelEvent(event);
        return;
    }

    // High-resolution trackpads already provide platform-tuned pixel deltas
    // and momentum. Apply those directly instead of adding a second animation.
    const QPoint pixelDelta = event->pixelDelta();
    if (!pixelDelta.isNull()) {
        stopSmoothScroll();
        verticalScrollBar()->setValue(verticalScrollBar()->value() -
                                      pixelDelta.y());
        m_smoothScrollTarget = verticalScrollBar()->value();
        event->accept();
        return;
    }

    const int angle = event->angleDelta().y();
    if (angle != 0) {
        const int wheelLines = qMax(1, QGuiApplication::styleHints()
                                           ->wheelScrollLines());
        const qreal linePixels = QFontMetricsF(font()).lineSpacing();
        smoothScrollBy(-qreal(angle) / 120.0 * wheelLines * linePixels);
        event->accept();
        return;
    }

    QTextEdit::wheelEvent(event);
}

bool MarkdownEditor::continueList() {
    QTextCursor cur = textCursor();
    if (cur.hasSelection())
        return false;

    const QString line = cur.block().text();
    const int caret = cur.positionInBlock();
    auto endConstruct = [&] { // drop the marker, stay on the now-empty line
        cur.beginEditBlock();
        cur.movePosition(QTextCursor::StartOfBlock);
        cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cur.removeSelectedText();
        cur.endEditBlock();
        setTextCursor(cur);
    };

    // Accept an empty marker even before its customary trailing space has been
    // typed ("-", "- [ ]", "1.", and so on). At any other caret position the
    // normal split behavior still wins, so editing inside a marker is safe.
    static const QRegularExpression emptyListRe(QStringLiteral(
        "^\\s*(?:[-*+]|\\d+[.)])(?:\\s+\\[[ xX]\\])?\\s*$"));
    if (caret == line.size() && emptyListRe.match(line).hasMatch()) {
        endConstruct();
        return true;
    }

    static const QRegularExpression listRe(QStringLiteral(
        "^(\\s*)(?:([-*+])|(\\d+)([.)]))\\s+(\\[[ xX]\\]\\s+)?(.*)$"));
    if (const auto m = listRe.match(line); m.hasMatch()) {
        // Enter inside the indent/marker (before the content) just splits the
        // line normally — don't manufacture a marker there.
        if (caret < m.capturedStart(6))
            return false;
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
        // At the line end this appends a fresh item; mid-item it splits the line,
        // carrying the text after the caret down onto the new (marked) item.
        cur.insertText(QStringLiteral("\n") + prefix);
        setTextCursor(cur);
        return true;
    }

    // Blockquotes behave like lists: Enter continues "> ", empty quote ends it.
    static const QRegularExpression quoteRe(
        QStringLiteral("^(\\s*)(>+)\\s?(.*)$"));
    if (const auto q = quoteRe.match(line); q.hasMatch()) {
        if (caret < q.capturedStart(3))
            return false;
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
    if (event->key() == Qt::Key_Alt &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
        // Modifier keys normally do not repeat, but ignoring a platform-issued
        // repeat keeps a long hold from accidentally cancelling an active mode.
        if (!event->isAutoRepeat() && !m_quickJumpAltHeld)
            armQuickJump();
        event->accept();
        return;
    }

    if (m_quickJumpArmed && !m_quickJumpActive) {
        // A chord started before the hold threshold: this is an existing Alt
        // shortcut or an Option-produced character, so do not enter jump mode.
        m_quickJumpTimer->stop();
        m_quickJumpArmed = false;
    }
    if (handleQuickJumpKey(event)) {
        event->accept();
        return;
    }

    if (m_readMode) {
        const auto mods = event->modifiers() &
                          ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
        if (mods == (Qt::ControlModifier | Qt::ShiftModifier) &&
            event->key() == Qt::Key_H) {
            stopSmoothScroll();
            toggleReadHighlight();
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Copy)) {
            copy();
            event->accept();
            return;
        }
        if (mods == Qt::AltModifier &&
            (event->key() == Qt::Key_Left ||
             event->key() == Qt::Key_Right)) {
            if (event->key() == Qt::Key_Left)
                emit navigateBack();
            else
                emit navigateForward();
            event->accept();
            return;
        }
        if (mods == Qt::NoModifier &&
            (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
            const qreal delta = QFontMetricsF(font()).lineSpacing() *
                                (event->key() == Qt::Key_Up ? -1.0 : 1.0);
            smoothScrollBy(delta, 105);
            event->accept();
            return;
        }
        stopSmoothScroll();
        // The base read-only handler retains selection/copy and standard page
        // navigation without reaching Emerald's editing-only shortcuts below.
        QTextEdit::keyPressEvent(event);
        return;
    }

    stopSmoothScroll();

    if (event->matches(QKeySequence::Undo)) {
        undo();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        event->accept();
        return;
    }

    if (m_completer->popup()->isVisible()) {
        // These keys belong to the popup while it is open.
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
            // A popup can outlive the cursor context that opened it (for
            // example after clicking onto an empty line). Only let it consume
            // Enter while the caret is still inside an unfinished [[link.
            {
                bool inContext = false;
                wikiContextPrefix(&inContext);
                if (!inContext) {
                    m_completer->popup()->hide();
                    break;
                }
            }
            event->ignore();
            return;
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            event->ignore();
            return;
        default:
            break;
        }
    }

#ifdef Q_OS_MACOS
    // macOS: ⌘+Backspace deletes from the caret to the start of the line (the
    // native Cocoa behaviour). Qt maps Qt::ControlModifier to ⌘ here; ⌥+Backspace
    // (delete word) stays Qt's default. If the caret is already at the line
    // start, fall through so the default joins with the previous line.
    if (event->key() == Qt::Key_Backspace &&
        (event->modifiers() & Qt::ControlModifier) &&
        !(event->modifiers() &
          (Qt::AltModifier | Qt::MetaModifier | Qt::ShiftModifier))) {
        QTextCursor c = textCursor();
        if (!c.hasSelection()) {
            c.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
            if (c.hasSelection()) {
                c.removeSelectedText();
                setTextCursor(c);
                return;
            }
        }
    }
#endif

    // Up at the very top of the body reveals the hidden mascot header line so it
    // can be read or edited. Moving the caret onto it un-hides it (see
    // updateMascotLineState); pressing Down again re-hides it.
    if (event->key() == Qt::Key_Up && !m_completer->popup()->isVisible() &&
        !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier |
                                Qt::AltModifier | Qt::MetaModifier))) {
        const QTextBlock mb = mascotBlock();
        const QTextCursor c = textCursor();
        if (mb.isValid() && !mb.isVisible() && !c.hasSelection() &&
            c.block() == mb.next()) {
            const QTextLine line =
                c.block().layout()->lineForTextPosition(c.positionInBlock());
            if (!line.isValid() || line.lineNumber() == 0) {
                QTextCursor m(mb);
                m.movePosition(QTextCursor::EndOfBlock);
                setTextCursor(m); // un-hides block 0 via updateMascotLineState
                ensureCursorVisible();
                return;
            }
        }
    }

    // Ctrl+Enter: open a new line below without splitting the current one. Move
    // to the line end first, then reuse the normal Enter logic so a list keeps
    // continuing (and an empty item still clears itself).
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        (event->modifiers() & Qt::ControlModifier)) {
        QTextCursor c = textCursor();
        const bool inComment = cursorInsideComment(c);
        c.movePosition(QTextCursor::EndOfBlock);
        setTextCursor(c);
        if (inComment || insideCodeBlock(c.block()) || !continueList()) {
            c = textCursor();
            c.insertText(QStringLiteral("\n"));
            setTextCursor(c);
        }
        ensureCursorVisible();
        event->accept();
        return;
    }

    // Enter at the very start of a collapsed heading/list item inserts the
    // blank line above without popping the section open. A plain insert would
    // split the anchor block, drift its fold handle onto the new empty line, and
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
            // Everything from the fold anchor down shifted one block lower.
            m_folds[fi].anchor = document()->findBlockByNumber(headNum + 1);
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
    // character — but Ctrl/Cmd mean a shortcut, so bail on those. Option/Alt is
    // allowed: on many non-US keyboards the pairing chars (e.g. [ ] { } `) are
    // typed with Option, and we key off the produced text, not the modifier.
    if (!(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) &&
        surroundSelection(event->text()))
        return;

    // Typing the third backtick of a fence (the line becomes "```") auto-creates
    // the matching closing ``` on the line below and leaves the caret on the
    // opening fence, so a language can be typed (Enter then drops into the body).
    // Only
    // fires on a line that is exactly "``" with the caret at its end and not
    // already inside a code block (so closing a block by hand still works).
    // Key off the produced text (a backtick), not the modifiers: on many non-US
    // Mac keyboards ` is typed with Option, so excluding Alt here would stop the
    // fence from auto-closing. Still bail on Ctrl/Cmd (those are shortcuts).
    if (event->text() == QStringLiteral("`") &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
        QTextCursor c = textCursor();
        if (!c.hasSelection() && c.atBlockEnd() &&
            c.block().text() == QStringLiteral("``") &&
            !insideCodeBlock(c.block()) && !cursorInsideComment(c)) {
            c.beginEditBlock();
            c.insertText(QStringLiteral("`\n```")); // finish open + closing fence
            c.movePosition(QTextCursor::Up);         // back to the opening fence,
            c.movePosition(QTextCursor::EndOfBlock); // caret there to type a language
            c.endEditBlock();
            setTextCursor(c);
            return;
        }
    }

    // Inside a fenced code block the text is verbatim: Enter and Tab insert a
    // plain newline / indent instead of continuing a list or folding markup.
    const bool inCode = insideCodeBlock(textCursor().block());
    const bool inComment = cursorInsideComment(textCursor());
    if (!inCode && !inComment) {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) &&
            handleTableHeaderEnter()) {
            return;
        }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) &&
            handleTableCellEnter()) {
            return;
        }
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

    // Markdown's source model is line-oriented, so Return must always create a
    // real paragraph break after the special table/list cases above decline it.
    // Relying on QTextEdit's final fallback left the key vulnerable to stale
    // completer state and platform-specific soft-line handling. Make the source
    // edit explicit and keep every ordinary, Shift, keypad, and code-block
    // Return on the same guaranteed path.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        QTextCursor cursor = textCursor();
        cursor.insertText(QStringLiteral("\n"));
        setTextCursor(cursor);
        ensureCursorVisible();
        event->accept();
        return;
    }

    // Editor keybindings. (On macOS Qt maps ControlModifier to ⌘, so these are
    // Cmd-based there automatically.)
    const auto mods = event->modifiers();
    if (mods == Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_B: wrapSelection(QStringLiteral("**")); return; // bold
        case Qt::Key_I: wrapSelection(QStringLiteral("*")); return;  // italic
        case Qt::Key_K: insertLink(); return;                        // [text](…)
        case Qt::Key_L: selectCurrentLine(); return;
        case Qt::Key_1:
        case Qt::Key_2:
        case Qt::Key_3:
        case Qt::Key_4:
        case Qt::Key_5:
        case Qt::Key_6:
            setHeadingLevel(event->key() - Qt::Key_0); return; // # … ###### heading
        default: break;
        }
    } else if ((mods & Qt::AltModifier) &&
               !(mods & (Qt::ControlModifier | Qt::MetaModifier |
                         Qt::ShiftModifier)) &&
               (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        // Masked check (not ==): macOS tags arrow keys with KeypadModifier, so an
        // exact "mods == AltModifier" test would miss Option+Arrow there.
        moveLines(event->key() == Qt::Key_Up);
        return;
    } else if ((mods & Qt::AltModifier) &&
               !(mods & (Qt::ControlModifier | Qt::MetaModifier |
                         Qt::ShiftModifier)) &&
               (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        // Browser-style history. Also bound as a window shortcut, but on macOS
        // Option+Arrow is a text-editing key (word left/right) that the QAction
        // shortcut never receives, so handle it here — where the editor has
        // focus — to make Alt/Option+Arrow navigate on every platform. (Masked
        // check for the same KeypadModifier reason as Alt+Up/Down above.)
        if (event->key() == Qt::Key_Left)
            emit navigateBack();
        else
            emit navigateForward();
        return;
    }

    QTextEdit::keyPressEvent(event);
    updateCompletionPopup();
}

void MarkdownEditor::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt) {
        m_quickJumpAltHeld = false;
        cancelQuickJump();
        event->accept();
        return;
    }
    QTextEdit::keyReleaseEvent(event);
}

void MarkdownEditor::focusOutEvent(QFocusEvent *event) {
    m_quickJumpAltHeld = false;
    cancelQuickJump();
    const bool selectionDragEnded =
        std::exchange(m_mouseSelectionDrag, false);
    QTextEdit::focusOutEvent(event);
    if (selectionDragEnded && !m_readMode) {
        updateActiveHighlight();
        applyImagePreviewFormats();
        viewport()->update();
    }
}

bool MarkdownEditor::canInsertFromMimeData(const QMimeData *source) const {
    if (m_readMode)
        return false;
    if (source && (source->hasImage() ||
                   !imageFilePathsFromMimeData(source).isEmpty()))
        return true;
    return QTextEdit::canInsertFromMimeData(source);
}

void MarkdownEditor::insertFromMimeData(const QMimeData *source) {
    if (m_readMode)
        return;
    if (source) {
        const QStringList imagePaths = imageFilePathsFromMimeData(source);
        if (!imagePaths.isEmpty()) {
            emit imageFilesInserted(imagePaths);
            return;
        }
        if (source->hasImage()) {
            const QImage image = qvariant_cast<QImage>(source->imageData());
            if (!image.isNull()) {
                emit imagePasted(image);
                return;
            }
        }
    }
    QTextEdit::insertFromMimeData(source);
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
    // A selection across several lines formats each line on its own.
    if (wrapSelectionByLine(marker, marker, /*toggle=*/true))
        return;
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
    static const QString pairs = QStringLiteral("*(_=['\"`~$");
    if (!pairs.contains(ch))
        return false;
    QTextCursor cur = textCursor();
    if (!cur.hasSelection())
        return false;
    const QChar close = ch == QLatin1Char('(')   ? QLatin1Char(')')
                        : ch == QLatin1Char('[') ? QLatin1Char(']')
                                                 : ch;
    // Across several lines, pair up each line's selected segment on its own —
    // except math: a $…$ spanning the whole selection (one opening, one closing
    // $) is what's wanted, not a $…$ per line.
    if (ch != QLatin1Char('$') &&
        wrapSelectionByLine(QString(ch), QString(close), /*toggle=*/false))
        return true;
    const QString inner = cur.selectedText();
    cur.insertText(QString(ch) + inner + close);
    // Re-select the original text, now sitting between the new pair.
    const int afterClose = cur.position();
    cur.setPosition(afterClose - 1 - inner.size());
    cur.setPosition(afterClose - 1, QTextCursor::KeepAnchor);
    setTextCursor(cur);
    return true;
}

// Per-line formatting for a multi-line selection. Each selected line gets the
// markers around its selected segment only: a fully selected line is wrapped
// end to end, a partially selected one just over the selected span. Whitespace
// at a segment's edges is left outside the markers (so they hug real text), and
// blank/whitespace-only segments are skipped. Returns false when the selection
// stays within a single line, leaving the caller's whole-selection path to run.
bool MarkdownEditor::wrapSelectionByLine(const QString &open,
                                         const QString &close, bool toggle) {
    QTextCursor cur = textCursor();
    if (!cur.hasSelection())
        return false;
    const int selStart = cur.selectionStart();
    const int selEnd = cur.selectionEnd();
    QTextDocument *doc = document();
    if (doc->findBlock(selStart).blockNumber() ==
        doc->findBlock(selEnd).blockNumber())
        return false; // single line: let the caller wrap the whole selection

    // The selected, whitespace-trimmed span on each touched line.
    struct Seg {
        int start;
        int end;
    };
    QList<Seg> segs;
    for (QTextBlock b = doc->findBlock(selStart);
         b.isValid() && b.position() <= selEnd; b = b.next()) {
        const int lineStart = b.position();
        const int lineEnd = lineStart + b.length() - 1; // exclude the newline
        int s = qMax(selStart, lineStart);
        int e = qMin(selEnd, lineEnd);
        const QString line = b.text();
        while (s < e && line.at(s - lineStart).isSpace())
            ++s;
        while (e > s && line.at(e - 1 - lineStart).isSpace())
            --e;
        if (s < e)
            segs.append({s, e});
    }
    if (segs.isEmpty())
        return false; // nothing but blank lines selected — fall back

    // For a toggle, only strip markers when every segment already carries them.
    const int no = open.size(), nc = close.size();
    bool allWrapped = toggle;
    for (int i = 0; toggle && i < segs.size(); ++i) {
        QTextCursor t(doc);
        t.setPosition(segs.at(i).start);
        t.setPosition(segs.at(i).end, QTextCursor::KeepAnchor);
        const QString s = t.selectedText();
        if (s.size() < no + nc || !s.startsWith(open) || !s.endsWith(close)) {
            allWrapped = false;
            break;
        }
    }

    cur.beginEditBlock();
    // Edit back to front so each segment's positions stay valid as we go.
    for (int i = segs.size() - 1; i >= 0; --i) {
        QTextCursor t(doc);
        t.setPosition(segs.at(i).start);
        t.setPosition(segs.at(i).end, QTextCursor::KeepAnchor);
        const QString inner = t.selectedText();
        t.insertText(allWrapped ? inner.mid(no, inner.size() - no - nc)
                                : open + inner + close);
    }
    cur.endEditBlock();

    // Re-select the whole affected run so a repeat press toggles it back. Every
    // segment shifts by the same delta, so the new bounds are exact.
    const int delta = allWrapped ? -(no + nc) : (no + nc);
    QTextCursor sel(doc);
    sel.setPosition(segs.first().start);
    sel.setPosition(segs.last().end + delta * segs.size(),
                    QTextCursor::KeepAnchor);
    setTextCursor(sel);
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

void MarkdownEditor::insertLink() {
    QTextCursor cur = textCursor();
    if (cur.hasSelection()) {
        // Wrap the selection as a Markdown link and drop the caret between the
        // parens, ready for the URL: "text" -> "[text](‸)".
        cur.insertText(QStringLiteral("[") + cur.selectedText() +
                       QStringLiteral("]()"));
        cur.movePosition(QTextCursor::PreviousCharacter);
    } else {
        // No selection: empty link skeleton, caret inside the [] for the text.
        cur.insertText(QStringLiteral("[]()"));
        cur.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, 3);
    }
    setTextCursor(cur);
}

void MarkdownEditor::setHeadingLevel(int level) {
    const QTextBlock block = textCursor().block();
    const int current = headingLevel(block.text()); // 0 when not a heading
    const int strip = current > 0 ? current + 1 : 0; // existing "###" + its space
    // Pressing the level a line already has clears the heading (toggle off).
    const int target = current == level ? 0 : level;
    QTextCursor c(block);
    c.movePosition(QTextCursor::StartOfBlock);
    c.setPosition(block.position() + strip, QTextCursor::KeepAnchor);
    c.insertText(target > 0 ? QString(target, QLatin1Char('#')) + QLatin1Char(' ')
                            : QString());
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
    const QRectF &clip,
    const std::function<void(const CodeBlock &)> &fn,
    bool includeCode) const {
    const qreal docMargin = document()->documentMargin();
    const qreal left = docMargin * 0.5;
    const qreal right = viewport()->width() - docMargin * 0.5;
    static const QRegularExpression fenceRe(
        QStringLiteral("^\\s*(?:```|~~~)\\s*(\\S*)"));

    // The block counts as "being edited" — show the raw ``` source, not the
    // rendered header bar — when the caret or a selection touches it. This is the
    // same test the highlighter uses to reveal the fences
    // (MarkdownHighlighter::caretInCodeRegion), so selecting the whole block (or
    // across it) shows raw markup on both sides instead of a half-rendered mix.
    const QTextCursor tc = textCursor();
    const int selFirst = document()->findBlock(tc.selectionStart()).blockNumber();
    const int selLast = document()->findBlock(tc.selectionEnd()).blockNumber();
    QTextBlock start = firstVisibleTextBlock();
    if (!start.isValid())
        return;
    // If the viewport starts inside a fenced region, walk back to its opening
    // fence so the block geometry is complete. This bounds the common case to
    // visible content while still drawing a block whose header is above view.
    while (start.previous().isValid() && start.userState() == 1 &&
           start.previous().userState() == 1)
        start = start.previous();

    bool inCode = false;
    qreal headerTop = 0, headerBottom = 0;
    int openNum = 0;
    QString lang;
    QStringList code;
    auto emitRegion = [&](qreal bodyBottom, int closeNum) {
        CodeBlock cb;
        cb.header = QRectF(left, headerTop, right - left, headerBottom - headerTop);
        cb.body = QRectF(left, headerBottom, right - left, bodyBottom - headerBottom);
        if (!cb.header.united(cb.body).intersects(clip))
            return;
        const qreal s = 16;
        cb.copyBtn = QRectF(cb.header.right() - s - 8,
                            cb.header.center().y() - s / 2, s, s);
        cb.language = lang.isEmpty() ? QStringLiteral("Text") : lang.left(32);
        if (includeCode)
            cb.code = code.join(QLatin1Char('\n'));
        // The caret or selection touching the block (see selFirst/selLast above)
        // means it's being edited; callers then show raw markup instead of the
        // header bar (which would overlap the now-visible ``` fence).
        cb.active = !m_readMode && selLast >= openNum && selFirst <= closeNum;
        fn(cb);
    };

    for (QTextBlock b = start; b.isValid(); b = b.next()) {
        const bool isCode = b.userState() == 1; // MarkdownHighlighter::StateCode
        const QRectF geo = blockViewportRect(b);
        if (geo.top() > clip.bottom()) {
            if (inCode)
                emitRegion(geo.top(), b.blockNumber());
            break;
        }
        if (isCode && !inCode) { // opening fence = the header row
            inCode = true;
            headerTop = geo.top();
            headerBottom = geo.bottom();
            openNum = b.blockNumber();
            const auto m = fenceRe.match(b.text());
            lang = m.hasMatch() ? m.captured(1) : QString();
            code.clear();
        } else if (isCode && inCode) { // inner code line
            if (includeCode)
                code << b.text();
        } else if (!isCode && inCode) { // closing fence
            emitRegion(geo.bottom(), b.blockNumber());
            inCode = false;
        }
    }
    if (inCode)
        emitRegion(blockViewportRect(document()->lastBlock()).bottom(),
                   document()->lastBlock().blockNumber());
}

bool MarkdownEditor::copyCodeBlockAt(const QPoint &pos) {
    bool copied = false;
    const QRectF clip(QPointF(pos), QSizeF(1, 1));
    forEachCodeBlock(clip, [&](const CodeBlock &cb) {
        if (!copied && !cb.active && cb.copyBtn.contains(pos)) {
            QApplication::clipboard()->setText(cb.code);
            copied = true;
        }
    }, true);
    if (copied)
        emit noticeRequested(tr("Copied code to clipboard"));
    return copied;
}

bool MarkdownEditor::isOverCopyButton(const QPoint &pos) const {
    bool over = false;
    const QRectF clip(QPointF(pos), QSizeF(1, 1));
    forEachCodeBlock(clip, [&](const CodeBlock &cb) {
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
    const QTextBlock sourceHeading = sourceBlockForDisplay(heading);
    if (!sourceHeading.isValid())
        return {};
    // The last block a fold of `heading` should hide: the section runs from
    // heading.next() down to the next same-or-higher heading (or EOF), minus any
    // trailing blank lines — so the blank separation before that next heading
    // stays visible. Invalid when the section has no foldable content.
    const int level = headingLevel(
        commentAnalysisForBlock(sourceHeading).masked(sourceHeading.text()));
    QTextBlock lastContent;
    for (QTextBlock b = sourceHeading.next(); b.isValid(); b = b.next()) {
        // A "# ..." line inside a code block is literal text, not a heading,
        // so it must not end the section early.
        const bool code = insideCodeBlock(b);
        const QString structure =
            code ? b.text()
                 : commentAnalysisForBlock(b).masked(b.text());
        const int l = code ? 0 : headingLevel(structure);
        if (l > 0 && l <= level)
            break;
        if (!structure.trimmed().isEmpty())
            lastContent = b;
    }
    return lastContent;
}

bool MarkdownEditor::headingFoldable(const QTextBlock &heading) const {
    const QTextBlock sourceHeading = sourceBlockForDisplay(heading);
    if (!sourceHeading.isValid() || insideCodeBlock(sourceHeading))
        return false; // a "# ..." line inside a code block isn't a heading
    if (headingLevel(commentAnalysisForBlock(sourceHeading)
                         .masked(sourceHeading.text())) == 0)
        return false;
    return foldSectionEnd(sourceHeading).isValid();
}

QTextBlock MarkdownEditor::listSubtreeEnd(const QTextBlock &item) const {
    const QTextBlock sourceItem = sourceBlockForDisplay(item);
    if (!sourceItem.isValid() || insideCodeBlock(sourceItem))
        return {};
    const QString sourceStructure =
        commentAnalysisForBlock(sourceItem).masked(sourceItem.text());
    if (quotePrefix(sourceStructure).depth > 0)
        return {};
    const ListPrefix parent = listPrefix(sourceStructure);
    if (!parent.valid())
        return {};

    QTextBlock block = sourceItem.next();
    if (!block.isValid() || insideCodeBlock(block))
        return {};
    QString blockStructure =
        commentAnalysisForBlock(block).masked(block.text());
    if (quotePrefix(blockStructure).depth > 0)
        return {};
    const ListPrefix firstChild = listPrefix(blockStructure);
    if (!firstChild.valid() || firstChild.depth <= parent.depth)
        return {};

    QTextBlock end = block;
    for (block = block.next(); block.isValid(); block = block.next()) {
        blockStructure =
            commentAnalysisForBlock(block).masked(block.text());
        if (insideCodeBlock(block) || quotePrefix(blockStructure).depth > 0)
            break;
        const ListPrefix descendant = listPrefix(blockStructure);
        if (!descendant.valid() || descendant.depth <= parent.depth)
            break;
        end = block;
    }
    return end;
}

bool MarkdownEditor::listItemFoldable(const QTextBlock &item) const {
    // The renderer caches direct ownership on consecutive list rows. This is
    // the paint/hit-test fast path; the source scan below remains the fallback
    // while a just-edited block is waiting for its queued visual-format pass.
    if (item.isValid() && item.blockFormat().hasProperty(
                              MarkdownStyle::ListDepthProperty)) {
        const QTextBlock child = item.next();
        return child.isValid() &&
               child.blockFormat().hasProperty(
                   MarkdownStyle::ListParentBlockProperty) &&
               child.blockFormat()
                       .property(MarkdownStyle::ListParentBlockProperty)
                       .toInt() == item.blockNumber();
    }
    return listSubtreeEnd(item).isValid();
}

bool MarkdownEditor::foldAnchorFoldable(const QTextBlock &block) const {
    return listItemFoldable(block) || headingFoldable(block);
}

QTextBlock MarkdownEditor::sourceBlockForDisplay(
    const QTextBlock &block) const {
    if (!block.isValid() || !m_sourceDocument)
        return {};
    if (block.document() == m_sourceDocument)
        return block;
    const int sourceBlock = MarkdownReadRenderer::sourceBlockNumber(block);
    return sourceBlock >= 0
               ? m_sourceDocument->findBlockByNumber(sourceBlock)
               : QTextBlock();
}

int MarkdownEditor::foldIndexOf(const QTextBlock &heading) const {
    const QTextBlock sourceAnchor = sourceBlockForDisplay(heading);
    if (!sourceAnchor.isValid())
        return -1;
    for (int i = 0; i < m_folds.size(); ++i)
        if (m_folds[i].anchor == sourceAnchor)
            return i;
    return -1;
}

bool MarkdownEditor::isFolded(const QTextBlock &heading) const {
    return foldIndexOf(heading) >= 0;
}

void MarkdownEditor::toggleFoldAt(const QTextBlock &heading) {
    if (!foldAnchorFoldable(heading))
        return;
    const QTextBlock sourceAnchor = sourceBlockForDisplay(heading);
    const int idx = foldIndexOf(heading);
    if (idx >= 0) {
        m_folds.removeAt(idx);
    } else {
        // Capture the section extent now and hold it: later edits to the visible
        // trailing blank lines won't grow the fold (see Fold). Move the caret out
        // of the part about to be hidden; trailing blanks stay visible, so a
        // caret resting on one of those is fine to leave in place.
        const bool listFold = listItemFoldable(sourceAnchor);
        const Fold::Kind kind =
            listFold ? Fold::Kind::List : Fold::Kind::Heading;
        const QTextBlock end = listFold ? listSubtreeEnd(sourceAnchor)
                                        : foldSectionEnd(sourceAnchor);
        const int caret = sourceTextCursor().blockNumber();
        for (QTextBlock b = sourceAnchor.next(); end.isValid() && b.isValid();
             b = b.next()) {
            if (b.blockNumber() == caret) {
                QTextCursor c(sourceAnchor);
                c.movePosition(QTextCursor::EndOfBlock);
                setSourceTextCursor(c);
                break;
            }
            if (b == end)
                break;
        }
        m_folds.append({sourceAnchor, end, kind});
    }
    reapplyFolds();
}

// Drop every active fold. Must be called before the document's content is
// wholesale replaced (loading another note, reloading from disk, clearing on a
// vault switch): the folds hold QTextBlock handles into the current content, and
// once that content is swapped out those handles dangle. They'd still report
// isValid() == true (the QTextDocument object is reused), so reapplyFolds() —
// fired from the contentsChanged that the replacement emits — would call
// QTextBlock::text() on freed memory and crash. Clearing the list first means
// reapplyFolds() early-returns on the empty set instead.
void MarkdownEditor::clearFolds() { m_folds.clear(); }

void MarkdownEditor::reapplyFolds() {
    if (m_applyingFolds)
        return;
    m_applyingFolds = true;

    // Forget folds whose source anchor was edited away, deleted, or no longer
    // owns any content of the same structural kind.
    m_folds.erase(std::remove_if(m_folds.begin(), m_folds.end(),
                                 [this](const Fold &f) {
                                     if (!f.anchor.isValid())
                                         return true;
                                     if (f.kind == Fold::Kind::Heading)
                                         return headingLevel(
                                                    commentAnalysisForBlock(f.anchor)
                                                        .masked(f.anchor.text())) == 0 ||
                                                !foldSectionEnd(f.anchor).isValid();
                                     return !listSubtreeEnd(f.anchor).isValid();
                                 }),
                  m_folds.end());

    for (QTextBlock b = document()->firstBlock(); b.isValid(); b = b.next())
        if (!b.isVisible())
            b.setVisible(true);

    for (const Fold &f : m_folds) {
        // Use the extent captured when the fold was made. If that block was
        // deleted since, fall back to recomputing so the fold still holds.
        QTextBlock end =
            f.end.isValid()
                ? f.end
                : (f.kind == Fold::Kind::List ? listSubtreeEnd(f.anchor)
                                              : foldSectionEnd(f.anchor));
        if (!end.isValid())
            continue;
        if (!m_readMode) {
            for (QTextBlock b = f.anchor.next(); b.isValid(); b = b.next()) {
                b.setVisible(false);
                if (b == end)
                    break;
            }
        } else {
            const int firstSource = f.anchor.blockNumber() + 1;
            const int lastSource = end.blockNumber();
            for (QTextBlock b = document()->firstBlock(); b.isValid();
                 b = b.next()) {
                const int sourceBlock =
                    MarkdownReadRenderer::sourceBlockNumber(b);
                if (sourceBlock >= firstSource && sourceBlock <= lastSource)
                    b.setVisible(false);
            }
        }
    }

    document()->markContentsDirty(0, document()->characterCount());
    m_applyingFolds = false;
    viewport()->update();
}

void MarkdownEditor::prettifyTableAt(int blockNumber) {
    QTextBlock first = document()->findBlockByNumber(blockNumber);
    const auto structuralTableRow = [this](const QTextBlock &block) {
        return block.isValid() && !insideCodeBlock(block) &&
               isTableRow(commentMaskedBlockText(block));
    };
    if (!structuralTableRow(first))
        return;
    while (structuralTableRow(first.previous()))
        first = first.previous();
    QTextBlock last = first;
    while (structuralTableRow(last.next()))
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
                width[c] = qMax(
                    width[c],
                    MarkdownHighlighter::inlinePreviewColumnCount(rows[r][c]));

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

    // The editor's configured maximum column width is reflected by the live
    // viewport. Padding a narrow source table can otherwise make every row wrap
    // after prettification, destroying the visual grid it was meant to create.
    // Decide for the table as a whole before touching the document: either all
    // rows fit and are aligned, or the original Markdown is preserved exactly.
    const qreal availableWidth = qMax(
        qreal(0), viewport()->width() - document()->documentMargin() * 2);
    const QFontMetricsF metrics(font());
    for (const QString &line : std::as_const(out))
        if (metrics.horizontalAdvance(line) > availableWidth)
            return;

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

// Enter anywhere on a freshly-typed table header (a pipe row that is the first
// row and has no separator yet) builds the `| --- |` separator beneath it — plus
// an empty data row when none follows — and starts the caret in the first data
// cell. Returns false (Enter behaves normally) for any other line.
bool MarkdownEditor::handleTableHeaderEnter() {
    QTextCursor cur = textCursor();
    if (document()->findBlock(cur.selectionStart()) !=
        document()->findBlock(cur.selectionEnd()))
        return false;
    const QTextBlock block = cur.block();
    if (!isTableRow(block.text()) || isSeparatorRow(block.text()))
        return false;
    // Must be the first row of the table — the header.
    if (block.previous().isValid() && isTableRow(block.previous().text()))
        return false;
    // ...and the table must not already carry a separator anywhere below it.
    for (QTextBlock b = block.next(); b.isValid() && isTableRow(b.text());
         b = b.next())
        if (isSeparatorRow(b.text()))
            return false;

    const int cols = qMax(1, int(splitRow(block.text()).size()));
    const QTextBlock below = block.next();
    const bool hasDataBelow = below.isValid() && isTableRow(below.text());

    QString sep = QStringLiteral("|");
    QString empty = QStringLiteral("|");
    for (int i = 0; i < cols; ++i) {
        sep += QStringLiteral(" --- |");
        empty += QStringLiteral("  |");
    }
    QString insert = QStringLiteral("\n") + sep;
    if (!hasDataBelow)
        insert += QStringLiteral("\n") + empty;

    const int headerNo = block.blockNumber();
    QTextCursor edit(block);
    edit.movePosition(QTextCursor::EndOfBlock);
    m_prettifying = true; // suppress the leave-table reformat mid-edit
    edit.insertText(insert);
    m_prettifying = false;
    prettifyTableAt(headerNo); // align header + separator (+ new row)

    // A completed header starts data entry at the beginning of the first row,
    // regardless of which header cell held the caret.
    const QTextBlock data = document()->findBlockByNumber(headerNo + 2);
    if (data.isValid())
        moveToTableCell(data, 0);
    return true;
}

// Enter in an established table moves vertically. Header + separator are one
// setup region and always start at the first cell of the first data row; body
// rows keep their current column. At the bottom, grow one compact data row.
bool MarkdownEditor::handleTableCellEnter() {
    QTextCursor cur = textCursor();
    if (document()->findBlock(cur.selectionStart()) !=
        document()->findBlock(cur.selectionEnd()))
        return false;
    const QTextBlock block = cur.block();
    if (!isTableRow(block.text()))
        return false;

    QTextBlock first = block;
    QTextBlock last = block;
    while (first.previous().isValid() && isTableRow(first.previous().text()))
        first = first.previous();
    while (last.next().isValid() && isTableRow(last.next().text()))
        last = last.next();

    const int firstNo = first.blockNumber();
    const bool inHeaderRegion = block == first || isSeparatorRow(block.text());
    const int targetCell = inHeaderRegion ? 0 : tableCellIndex(cur);
    QTextBlock destination = block.next();
    while (destination.isValid() && destination != last.next() &&
           isSeparatorRow(destination.text()))
        destination = destination.next();

    if (!destination.isValid() || !isTableRow(destination.text())) {
        int columns = 1;
        for (QTextBlock row = first; row.isValid(); row = row.next()) {
            columns = qMax(columns, int(splitRow(row.text()).size()));
            if (row == last)
                break;
        }
        QString empty = QStringLiteral("|");
        for (int column = 0; column < columns; ++column)
            empty += QStringLiteral("  |");

        const int destinationNo = last.blockNumber() + 1;
        QTextCursor edit(last);
        edit.movePosition(QTextCursor::EndOfBlock);
        m_prettifying = true;
        edit.insertText(QLatin1Char('\n') + empty);
        m_prettifying = false;
        prettifyTableAt(firstNo);
        destination = document()->findBlockByNumber(destinationNo);
    } else if (inHeaderRegion) {
        // Enter confirms the header structure. Align the whole table before
        // data entry when the width guard in prettifyTableAt allows it, then
        // reacquire the destination because prettification replaces the rows.
        const int destinationNo = destination.blockNumber();
        prettifyTableAt(firstNo);
        destination = document()->findBlockByNumber(destinationNo);
    }

    if (destination.isValid())
        moveToTableCell(destination, targetCell);
    return true;
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

    // The caret's cell = number of structural pipes before it, minus the
    // leading one. A pipe inside [[target|alias]] belongs to that same cell.
    const QString text = block.text();
    const int caret = cursor.positionInBlock();
    int pipes = 0;
    for (int pipe : MarkdownHighlighter::tablePipePositions(text))
        if (pipe < caret)
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
        prettifyTableAt(firstNo); // keep the grid aligned (no-op if it already is)
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
        // Separator: land in the first cell of the data row below — creating
        // one only when the table is still just header + separator.
        targetRow = rowIdx + 1;
        targetCell = 0;
        if (rowIdx + 1 >= rowCount) { // no data row yet: build a fresh one
            normalize(nCols);
            rows.insert(rowIdx + 1, emptyRow());
            structural = true;
        }
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
    }

    // Re-align on every Tab so the grid tightens up as cells are filled. It's
    // cheap: prettifyTableAt rebuilds the rows in memory and only touches the
    // document when the alignment actually changed (else it returns at once).
    prettifyTableAt(firstNo);
    const QTextBlock dest = document()->findBlockByNumber(firstNo + targetRow);
    if (dest.isValid())
        moveToTableCell(dest, targetCell);
    return true;
}

void MarkdownEditor::moveToTableCell(const QTextBlock &block, int cellIdx) {
    const QString t = block.text();
    const QList<int> pipes = MarkdownHighlighter::tablePipePositions(t);
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
    EMERALD_PROFILE_SCOPE("MarkdownEditor::resizeEvent");
    stopSmoothScroll();
    // Preserve the document block and its fractional pixel offset at the top
    // while QTextEdit reflows to the new width.
    const QTextBlock anchor = firstVisibleTextBlock();
    const qreal anchorOffset = blockViewportRect(anchor).top();

    QTextEdit::resizeEvent(event);
    scheduleScrollPastEndRangeUpdate();
    if (!m_readMode) {
        applyImagePreviewFormats(); // source media rows depend on viewport width
    } else if (!m_readResizeQueued) {
        m_readResizeQueued = true;
        QTimer::singleShot(0, this, [this] {
            m_readResizeQueued = false;
            if (m_readMode)
                rebuildReadDocument(currentScrollRatio());
        });
    }

    if (!anchor.isValid() || anchor.blockNumber() <= 0 ||
        !document()->documentLayout())
        return;
    const qreal documentTop =
        document()->documentLayout()->blockBoundingRect(anchor).top();
    verticalScrollBar()->setValue(qRound(documentTop - anchorOffset));
}

void MarkdownEditor::drawFoldControls(QPainter &painter,
                                      const QRect &clip) const {
    painter.setRenderHint(QPainter::Antialiasing);
    for (QTextBlock block = firstVisibleTextBlock(); block.isValid();
         block = block.next()) {
        const QRectF geo = blockViewportRect(block);
        if (geo.top() > clip.bottom())
            break;
        if (!block.isVisible() || geo.bottom() < clip.top() ||
            !foldAnchorFoldable(block))
            continue;

        const QRectF control = foldControlRect(block);
        const QPointF center = control.center();
        const bool folded = isFolded(block);
        painter.setPen(Qt::NoPen);
        painter.setBrush(AppTheme::color(QColor(0x4f, 0x75, 0x65)));
        QPointF triangle[3];
        if (folded) {
            triangle[0] = {center.x() - 3, center.y() - 4};
            triangle[1] = {center.x() + 3, center.y()};
            triangle[2] = {center.x() - 3, center.y() + 4};
        } else {
            triangle[0] = {center.x() - 4, center.y() - 3};
            triangle[1] = {center.x() + 4, center.y() - 3};
            triangle[2] = {center.x(), center.y() + 4};
        }
        painter.drawPolygon(triangle, 3);

        // A collapsed list item gets the same quiet trailing cue as a heading.
        if (folded) {
            QTextCursor endCursor(block);
            endCursor.movePosition(QTextCursor::EndOfBlock);
            const auto endRects = textRangeViewportRects(
                block, qMax(0, block.length() - 2), 1);
            const QRectF end = endRects.isEmpty() ? cursorRect(endCursor)
                                                  : endRects.last();
            const qreal dotRadius = 1.4;
            qreal x = end.right() + 10.0;
            for (int dot = 0; dot < 3; ++dot, x += 5.5)
                painter.drawEllipse(QPointF(x, end.center().y()), dotRadius,
                                    dotRadius);
        }
    }
}

void MarkdownEditor::drawQuotePanels(QPainter &painter,
                                     const QRect &clip,
                                     bool drawRails) const {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(font());
    QColor accent = palette().color(QPalette::Highlight);
    if (!accent.isValid())
        accent = AppTheme::color(QColor(0x2b, 0xbf, 0x74));
    const qreal lineHeight = QFontMetricsF(font()).lineSpacing();
    const qreal quoteIndent = lineHeight * 1.18;
    const qreal documentMargin = document()->documentMargin();

    const auto effectiveQuoteDepth = [this](const QTextBlock &block) {
        if (!block.isValid())
            return 0;
        if (m_readMode) {
            // Read Mode already carries the rendered quote surface and its
            // nesting margin. Derive depth from that presentation metadata so
            // painting remains correct even when a new source note was loaded
            // while its detached syntax highlighter was suspended.
            const QTextBlockFormat format = block.blockFormat();
            if (format.background().style() == Qt::NoBrush)
                return 0;
            return qMax(1, qRound(format.leftMargin() / 16.0) + 1);
        }
        const QTextBlock source =
            block;
        return source.isValid() && !insideCodeBlock(source)
                   ? quotePrefix(commentMaskedBlockText(source)).depth
                   : 0;
    };
    for (QTextBlock block = firstVisibleTextBlock(); block.isValid();
         block = block.next()) {
        const int depth = effectiveQuoteDepth(block);
        if (depth == 0) {
            if (block.isVisible() &&
                blockViewportRect(block).top() > clip.bottom())
                break;
            continue;
        }

        if (!block.isVisible())
            continue;
        const QRectF geo = blockViewportRect(block);
        if (geo.top() > clip.bottom())
            break;

        const QTextBlockFormat cachedFormat = block.blockFormat();
        const int calloutDepth =
            cachedFormat.property(MarkdownStyle::CalloutDepthProperty).toInt();
        const QString calloutType =
            cachedFormat.property(MarkdownStyle::CalloutTypeProperty).toString();
        const bool isCalloutTitle =
            cachedFormat.property(MarkdownStyle::CalloutTitleProperty).toBool();

        const QColor lineAccent = calloutDepth > 0
                                      ? MarkdownCallout::accent(calloutType)
                                      : accent;
        QColor wash;
        if (m_readMode &&
            cachedFormat.background().style() != Qt::NoBrush) {
            wash = cachedFormat.background().color();
        } else if (calloutDepth > 0) {
            wash = MarkdownCallout::surface(calloutType, isCalloutTitle);
        } else {
            wash = ordinaryQuoteSurface(palette());
        }

        // Adjacent QTextBlock rectangles can land on opposing sides of a
        // fractional device pixel. Paint through the next row's top edge plus
        // one logical pixel so raster rounding can never expose the viewport.
        qreal bottom = geo.bottom();
        const QTextBlock next = block.next();
        if (effectiveQuoteDepth(next) > 0 && next.isVisible())
            bottom = qMax(bottom, blockViewportRect(next).top() + 1.0);
        if (bottom < clip.top())
            continue;

        const qreal surfaceIndent =
            calloutDepth > 0 ? (calloutDepth - 1) * quoteIndent : 0.0;
        const QRectF washRect(
            documentMargin + surfaceIndent, geo.top(),
            qMax(qreal(0), viewport()->width() - documentMargin * 2 -
                                   surfaceIndent),
            qMax(qreal(0), bottom - geo.top()));
        painter.fillRect(washRect, wash);

        if (drawRails) {
            for (int railDepth = 0; railDepth < depth; ++railDepth) {
                QColor rail = lineAccent;
                rail.setAlpha(qMax(70, 170 - railDepth * 35));
                QPen pen(rail, railDepth == 0 ? 2.2 : 1.6);
                pen.setCapStyle(Qt::FlatCap);
                painter.setPen(pen);
                // Align the stroke's outer edge with the panel boundary.
                // Centering it before documentMargin made the top-level rail
                // protrude into the gutter before the quote surface began.
                const qreal x =
                    documentMargin + railDepth * quoteIndent +
                    pen.widthF() / 2.0;
                painter.drawLine(QPointF(x, geo.top()),
                                 QPointF(x, bottom));
            }
        }

        // Read Mode emits its emoji as actual presentation text. Edit Mode
        // keeps source untouched and paints the icon over its reserved marker.
        if (!m_readMode && isCalloutTitle) {
            const MarkdownCallout::TitleLine title =
                MarkdownCallout::titleLine(
                    commentMaskedBlockText(block),
                    effectiveQuoteDepth(block.previous()));
            const bool sourceRevealed =
                block.blockNumber() >= m_visualSelectionFirst &&
                block.blockNumber() <= m_visualSelectionLast;
            if (title.valid() && !sourceRevealed) {
                const QList<QRectF> markerRects = textRangeViewportRects(
                    block, title.markerStart, 1);
                if (!markerRects.isEmpty()) {
                    QRectF iconRect = markerRects.first();
                    iconRect.setTop(geo.top());
                    iconRect.setHeight(geo.height());
                    painter.setPen(lineAccent);
                    painter.drawText(
                        iconRect, Qt::AlignLeft | Qt::AlignVCenter,
                        MarkdownCallout::emoji(title.type));
                }
            }
        }
    }
}

void MarkdownEditor::paintEvent(QPaintEvent *event) {
    EMERALD_PROFILE_SCOPE("MarkdownEditor::paintEvent");
    if (m_readMode) {
        // The Read Mode document already contains presentation formats. None of
        // the source editor's regex-driven overlays may inspect or repaint it.
        // Fill only the spaces between its block-owned quote surfaces. Drawing
        // the Edit Mode rail underneath those opaque backgrounds left only the
        // stroke caps visible as stray upper- and lower-left corner pixels.
        {
            QPainter quotePainter(viewport());
            drawQuotePanels(quotePainter, event->rect(), false);
        }
        QTextEdit::paintEvent(event);
        QPainter overlay(viewport());
        drawFoldControls(overlay, event->rect());
        drawQuickJumpOverlay(overlay);
        return;
    }
    // Code-block backgrounds go behind the text: a rounded body with a thin
    // header bar (rounded top corners) in a lighter complementary colour.
    {
        QPainter bg(viewport());
        bg.setRenderHint(QPainter::Antialiasing);
        forEachCodeBlock(event->rect(), [&](const CodeBlock &cb) {
            const QRectF full(cb.header.left(), cb.header.top(), cb.header.width(),
                              cb.body.bottom() - cb.header.top());
            if (!full.intersects(event->rect()))
                return;
            if (cb.active) // editing or selected: show the raw ``` source, no box
                return;
            bg.setPen(QPen(AppTheme::color(QColor(0x2a, 0x49, 0x39)),
                           1.0));
            bg.setBrush(AppTheme::color(QColor(0x12, 0x1d, 0x18)));
            bg.drawRoundedRect(full, 6, 6);
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
            bg.fillPath(path, AppTheme::color(QColor(0x1f, 0x47, 0x33)));
            bg.setPen(QPen(AppTheme::color(QColor(0x2d, 0x5c, 0x43)),
                           1.0));
            bg.drawLine(QPointF(h.left() + 1, h.bottom()),
                        QPointF(h.right() - 1, h.bottom()));
        });
    }

    // Quote surfaces and guides sit behind the document glyphs. Callout context
    // is cached by applyVisualBlockFormats on every row in the quote group, so
    // painting remains proportional to visible content even in a huge callout.
    {
        QPainter quotePainter(viewport());
        drawQuotePanels(quotePainter, event->rect());
    }

    // Inline code keeps real monospace text, with a rounded span painted behind
    // it instead of per-glyph square backgrounds. Wrapped spans are split into
    // one rounded segment per visual line.
    {
        static const QRegularExpression inlineCodeRe(
            QStringLiteral("`([^`]+)`"));
        QPainter codePainter(viewport());
        codePainter.setRenderHint(QPainter::Antialiasing);
        codePainter.setPen(QPen(AppTheme::color(QColor(0x29, 0x46, 0x37)),
                                1.0));
        codePainter.setBrush(AppTheme::color(QColor(0x16, 0x24, 0x1c)));
        for (QTextBlock block = firstVisibleTextBlock(); block.isValid();
             block = block.next()) {
            if (!block.isVisible() || block.userState() == 1)
                continue;
            const QRectF geo = blockViewportRect(block);
            if (geo.top() > event->rect().bottom())
                break;
            if (geo.bottom() < event->rect().top())
                continue;
            auto matches = inlineCodeRe.globalMatch(block.text());
            while (matches.hasNext()) {
                const auto match = matches.next();
                const auto rects = textRangeViewportRects(
                    block, int(match.capturedStart(1)),
                    int(match.capturedLength(1)));
                for (QRectF rect : rects) {
                    rect.adjust(-3.0, 1.0, 3.0, -1.0);
                    if (rect.width() > 0 && rect.height() > 0)
                        codePainter.drawRoundedRect(rect, 4, 4);
                }
            }
        }
    }

    QTextEdit::paintEvent(event);

    // Standalone Markdown images are stored as plain text but rendered as a
    // local preview whenever the caret/selection is not on that line.
    {
        const QTextCursor selCur = textCursor();
        const int selFirst = m_readMode
                                 ? 1
                                 : document()
                                       ->findBlock(selCur.selectionStart())
                                       .blockNumber();
        const int selLast = m_readMode
                                ? 0
                                : document()
                                      ->findBlock(selCur.selectionEnd())
                                      .blockNumber();

        QPainter imgPainter(viewport());
        imgPainter.setRenderHint(QPainter::Antialiasing);
        const qreal dpr = devicePixelRatioF();
        for (QTextBlock block = firstVisibleTextBlock(); block.isValid();
             block = block.next()) {
            if (!block.isVisible())
                continue;
            if (insideCodeBlock(block))
                continue;
            if (block.blockNumber() >= selFirst &&
                block.blockNumber() <= selLast)
                continue;

            const MarkdownImage::Image image = imageForBlock(block);
            if (!image.valid)
                continue;
            const QString &target = image.target;
            const QRectF area = imagePreviewArea(block);
            // The concealed Markdown glyph has a tiny block bounding rect;
            // visibility must be tested against the full fixed-height preview
            // area or the image vanishes as soon as that glyph leaves screen.
            if (area.top() > event->rect().bottom())
                break;
            if (area.bottom() < event->rect().top())
                continue;
            if (area.width() < 24 || area.height() < 24)
                continue;

            const QString path = resolvedImagePath(block);
            const QSizeF previewSize = imagePreviewSize(block);
            const QPixmap pm =
                imagePreviewPixmap(path, previewSize.toSize(), dpr,
                                   image.dimensions.width > 0 &&
                                       image.dimensions.height > 0);
            if (pm.isNull()) {
                const qreal placeholderWidth = qMin(qreal(360), area.width());
                const QRectF placeholder(
                    area.center() - QPointF(placeholderWidth / 2.0, 34),
                    QSizeF(placeholderWidth, 68));
                imgPainter.setPen(QPen(
                    AppTheme::color(QColor(0x20, 0x38, 0x2b)), 1));
                imgPainter.setBrush(
                    AppTheme::color(QColor(0x10, 0x11, 0x13)));
                imgPainter.drawRoundedRect(placeholder, 6, 6);
                imgPainter.setPen(
                    AppTheme::color(QColor(0x6f, 0x8e, 0x7e)));
                const QString label = target.size() > 42
                                          ? target.left(39) + QStringLiteral("…")
                                          : target;
                imgPainter.drawText(
                    placeholder.adjusted(12, 8, -12, -8),
                    Qt::AlignCenter | Qt::TextWordWrap,
                    tr("Image not found\n%1").arg(label));
                continue;
            }

            const QSizeF logicalSize = QSizeF(pm.size()) / pm.devicePixelRatio();
            const QPointF topLeft(
                area.left() + (area.width() - logicalSize.width()) / 2.0,
                area.top() + (area.height() - logicalSize.height()) / 2.0);
            const QRectF imageRect(topLeft, logicalSize);
            imgPainter.setPen(QPen(
                AppTheme::color(QColor(0x2b, 0x4a, 0x39)), 1));
            imgPainter.setBrush(
                AppTheme::color(QColor(0x10, 0x11, 0x13)));
            imgPainter.drawRoundedRect(imageRect, 6, 6);
            imgPainter.setClipRect(imageRect.adjusted(1, 1, -1, -1));
            imgPainter.drawPixmap(topLeft, pm);
            imgPainter.setClipping(false);
        }
    }

    // Draw a bullet glyph over each list dash that the highlighter hid (every
    // bullet line except the active one). The glyph varies by nesting level.
    static const QRegularExpression re(
        QStringLiteral("^(\\s*)[-*+]\\s+(?!\\[[ xX]\\]\\s)"));
    // Raw markup shows on every line the caret or a selection covers, so the
    // editor's own glyphs (bullets, checkboxes, rules — and the math below)
    // must hide there too, matching the highlighter's reveal span.
    const QTextCursor selCur = textCursor();
    const int selFirst = m_readMode
                             ? 1
                             : document()
                                   ->findBlock(selCur.selectionStart())
                                   .blockNumber();
    const int selLast = m_readMode
                            ? 0
                            : document()
                                  ->findBlock(selCur.selectionEnd())
                                  .blockNumber();
    const QFontMetricsF fm(font());
    const qreal diameter = fm.ascent() * 0.30;

    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    const QColor color(0x6d, 0x8e, 0x7c);

    for (QTextBlock block = firstVisibleTextBlock(); block.isValid();
         block = block.next()) {
        // Folded-away lines collapse onto their heading; skip them or their
        // bullet/checkbox/rule glyphs would pile up just under the title.
        if (!block.isVisible())
            continue;
        // Inside a code block the text is verbatim — no bullets/rules/checkboxes.
        if (insideCodeBlock(block))
            continue;
        const QRectF geo = blockViewportRect(block);
        if (geo.top() > event->rect().bottom())
            break;
        if (geo.bottom() < event->rect().top() ||
            (block.blockNumber() >= selFirst && block.blockNumber() <= selLast))
            continue;
        const QString structureText = commentMaskedBlockText(block);

        // Horizontal rule: a full-width line across the (hidden) dashes.
        static const QRegularExpression ruleRe(
            QStringLiteral("^\\s*([-*_])\\s*(?:\\1\\s*){2,}$"));
        if (ruleRe.match(structureText).hasMatch()) {
            const qreal margin = document()->documentMargin();
            const qreal y = geo.center().y();
            QPen pen(AppTheme::color(QColor(0x2f, 0x4a, 0x3b)));
            pen.setWidthF(1.4);
            p.setPen(pen);
            p.drawLine(QPointF(margin, y),
                       QPointF(viewport()->width() - margin, y));
            continue;
        }

        // Task checkbox, drawn over the hidden "- [ ] " markup.
        if (const auto t = taskRe().match(structureText); t.hasMatch()) {
            const QRectF box = taskCheckboxRect(block);
            const qreal s = box.width();
            const bool checked = t.captured(2).compare(QStringLiteral("x"),
                                                       Qt::CaseInsensitive) == 0;
            const QColor accent =
                AppTheme::color(QColor(0x2b, 0xbf, 0x74));
            if (checked) {
                p.setPen(Qt::NoPen);
                p.setBrush(accent);
                p.drawRoundedRect(box, 3, 3);
                QPen tick(AppTheme::current() == AppTheme::Id::Light
                              ? QColor(Qt::white)
                              : QColor(0x10, 0x18, 0x14));
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
        const auto m = re.match(structureText);
        if (!m.hasMatch())
            continue;

        const int markerPos = m.capturedLength(1); // the dash column
        QTextCursor cur(block);
        cur.setPosition(block.position() + markerPos);
        const QRectF cell = cursorRect(cur);
        const qreal cw = fm.horizontalAdvance(block.text().at(markerPos));
        const QPointF c(cell.left() + cw / 2.0, cell.center().y());
        const qreal r = diameter / 2.0;

        switch (listPrefix(structureText).depth % 3) {
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

    // Math: paint each formula over the space the highlighter reserved for it.
    // The cursor's own line shows the raw source instead, so skip it (and code
    // blocks, where $ is literal). A whole-line $$…$$ renders as a centred
    // display block; otherwise each inline $…$ is drawn on the text baseline.
    {
        const QColor mathColor(0x6f, 0xcf, 0xc0);
        // The editor background, painted behind a display formula before drawing
        // it: the highlighter hides the source with a transparent format, but
        // colour emoji ignore the foreground colour and bleed through, so mask
        // the reserved area first.
        const QColor mathBg(0x17, 0x18, 0x1b);
        const QFont inlineFont = MathRender::mathFont(font(), false);
        const QFont displayFont = MathRender::mathFont(font(), true);
        // A formula whose line(s) the selection touches shows raw source (the
        // highlighter reveals it), so skip painting it — selFirst/selLast above
        // (the caret-or-selection span) keep the rendered formula from sitting
        // under the selection highlight.

        // Multi-line $$ blocks first: the highlighter marks the opening and
        // middle lines StateMath (2); the closing line is the next normal one.
        // The body parts of each line joined form one formula, painted centred
        // over the whole region. Skip a region the selection touches — its lines
        // show raw source for editing.
        bool inMath = false;
        QRectF regionGeo;
        QStringList body;
        int openNum = 0;
        QTextBlock start = firstVisibleTextBlock();
        if (start.isValid()) {
            if (start.previous().isValid() && start.previous().userState() == 2) {
                start = start.previous();
                while (start.previous().isValid() &&
                       start.previous().userState() == 2)
                    start = start.previous();
            }
        }
        for (QTextBlock b = start; b.isValid(); b = b.next()) {
            const bool mathy = b.userState() == 2; // StateMath: open / middle
            const QRectF geo = blockViewportRect(b);
            auto flush = [&](int closeNum) {
                const bool busy = selLast >= openNum && selFirst <= closeNum;
                if (!busy && !body.isEmpty() &&
                    regionGeo.intersects(event->rect())) {
                    p.fillRect(regionGeo, mathBg);
                    MathRender::paint(p, regionGeo, body.join(QLatin1Char(' ')),
                                      displayFont, mathColor,
                                      MathRender::Align::Display);
                }
            };
            if (mathy && !inMath) { // opening line
                inMath = true;
                regionGeo = geo;
                body.clear();
                body << MathRender::bodyAfterOpen(b.text());
                openNum = b.blockNumber();
            } else if (mathy && inMath) { // middle line
                regionGeo = regionGeo.united(geo);
                body << b.text();
            } else if (!mathy && inMath) { // closing line
                regionGeo = regionGeo.united(geo);
                body << MathRender::bodyBeforeClose(b.text());
                flush(b.blockNumber());
                inMath = false;
            }
            if (geo.top() > event->rect().bottom() && !inMath)
                break;
        }
        if (inMath) { // a block left open at end of document
            const bool busy = selLast >= openNum;
            if (!busy && !body.isEmpty() && regionGeo.intersects(event->rect())) {
                p.fillRect(regionGeo, mathBg);
                MathRender::paint(p, regionGeo, body.join(QLatin1Char(' ')),
                                  displayFont, mathColor,
                                  MathRender::Align::Display);
            }
        }

        for (QTextBlock block = firstVisibleTextBlock(); block.isValid();
             block = block.next()) {
            if (!block.isVisible())
                continue;
            const QRectF geo = blockViewportRect(block);
            if (geo.top() > event->rect().bottom())
                break;
            const int bn = block.blockNumber();
            if (geo.bottom() < event->rect().top() ||
                (bn >= selFirst && bn <= selLast) || insideCodeBlock(block) ||
                block.userState() == 2) // part of a multi-line $$ region (above)
                continue;
            const QString btext = commentMaskedBlockText(block);

            const auto disp = MathRender::displayPattern().match(btext);
            if (disp.hasMatch()) {
                p.fillRect(geo, mathBg);
                MathRender::paint(p, geo, disp.captured(1), displayFont,
                                  mathColor, MathRender::Align::Display);
                continue;
            }

            // Inline code wins over math (matching the highlighter's consume
            // order), so a `$x^2$` stays literal: mask the backtick spans and
            // skip any formula that opens inside one.
            static const QRegularExpression inlineCodeRe(
                QStringLiteral("`[^`]+`"));
            QList<bool> codeMask(btext.size(), false);
            auto cit = inlineCodeRe.globalMatch(btext);
            while (cit.hasNext()) {
                const auto cm = cit.next();
                for (int i = cm.capturedStart();
                     i < cm.capturedEnd() && i < codeMask.size(); ++i)
                    codeMask[i] = true;
            }

            // Strikethrough mask for this line: a completed task's label and
            // every ~~…~~ span (ignoring ~~ inside inline `code`). A struck
            // formula gets a line drawn through it, matching how the highlighter
            // strikes inline `code` inside the same span.
            QList<bool> struck(btext.size(), false);
            {
                const auto tm = taskRe().match(btext);
                if (tm.hasMatch() &&
                    tm.captured(2).compare(QStringLiteral("x"),
                                           Qt::CaseInsensitive) == 0)
                    for (int i = tm.capturedEnd(0); i < struck.size(); ++i)
                        struck[i] = true;
                int open = -1;
                for (int i = 0; i + 1 < btext.size();) {
                    if (!codeMask[i] && btext[i] == QLatin1Char('~') &&
                        btext[i + 1] == QLatin1Char('~')) {
                        if (open < 0)
                            open = i + 2;
                        else {
                            for (int k = open; k < i && k < struck.size(); ++k)
                                struck[k] = true;
                            open = -1;
                        }
                        i += 2;
                    } else {
                        ++i;
                    }
                }
            }

            for (const auto &sp : MathRender::spans(btext)) {
                if (sp.start < codeMask.size() && codeMask[sp.start])
                    continue; // inside an inline `code` span — not a formula
                const auto formulaRects =
                    textRangeViewportRects(block, sp.start, sp.length);
                // A formula cannot be split semantically across visual lines.
                if (formulaRects.size() != 1)
                    continue;
                const QRectF rect = formulaRects.first();
                const QTextLine tline =
                    block.layout()->lineForTextPosition(sp.start);
                const qreal baselineOffset =
                    tline.isValid()
                        ? geo.top() + tline.y() + tline.ascent() - rect.top()
                        : -1.0;
                MathRender::paint(p, rect, sp.body, inlineFont, mathColor,
                                  MathRender::Align::Inline, baselineOffset);
                if (sp.start < struck.size() && struck[sp.start]) {
                    // Put the strike where the surrounding ~~text~~ has it: at the
                    // line's text baseline minus the font's strikeout offset — not
                    // the middle of the (math-inflated) line box, which sits too
                    // low. Same dim colour as the highlighter's strike so the line
                    // reads as continuous across "text $x^2$".
                    const QFontMetricsF fmText(font());
                    const qreal y =
                        tline.isValid()
                            ? geo.top() + tline.y() + tline.ascent() -
                                  fmText.strikeOutPos()
                            : rect.center().y();
                    QPen strikePen(
                        AppTheme::color(QColor(0x5e, 0x7d, 0x6d)));
                    strikePen.setWidthF(1.3);
                    p.setPen(strikePen);
                    p.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
                }
            }
        }
    }

    // Code-block header content: language label on the left, copy button right.
    forEachCodeBlock(event->rect(), [&](const CodeBlock &cb) {
        if (cb.active || !cb.header.intersects(event->rect()))
            return; // while editing, the raw fence shows instead
        QFont lf = font();
        lf.setPointSizeF(font().pointSizeF() * 0.85);
        p.setFont(lf);
        const QFontMetricsF labelMetrics(lf);
        const qreal labelWidth = qMin(cb.header.width() - 52.0,
                                      labelMetrics.horizontalAdvance(cb.language) +
                                          14.0);
        const QRectF labelRect(cb.header.left() + 8,
                               cb.header.center().y() -
                                   labelMetrics.height() * 0.58,
                               qMax(qreal(24), labelWidth),
                               labelMetrics.height() * 1.16);
        p.setPen(QPen(AppTheme::color(QColor(0x39, 0x79, 0x57)), 1.0));
        p.setBrush(AppTheme::color(QColor(0x19, 0x37, 0x28)));
        p.drawRoundedRect(labelRect, labelRect.height() / 2.0,
                          labelRect.height() / 2.0);
        p.setPen(AppTheme::color(QColor(0x7e, 0xe0, 0xb0)));
        p.drawText(labelRect, Qt::AlignCenter, cb.language);
        p.setFont(font());

        // Two offset rounded rects = a "copy" (stacked pages) glyph.
        const QRectF btn = cb.copyBtn;
        p.setPen(Qt::NoPen);
        p.setBrush(AppTheme::color(QColor(0x19, 0x37, 0x28)));
        p.drawRoundedRect(btn.adjusted(-3, -2, 3, 2), 4, 4);
        QPen pen(AppTheme::color(QColor(0x92, 0xb3, 0xa2)));
        pen.setWidthF(1.3);
        p.setPen(pen);
        p.setBrush(AppTheme::color(QColor(0x1f, 0x47, 0x33)));
        p.drawRoundedRect(QRectF(btn.left() + 5, btn.top() + 2, 8, 10), 2, 2);
        p.drawRoundedRect(QRectF(btn.left() + 2, btn.top() + 4, 8, 10), 2, 2);
    });

    drawFoldControls(p, event->rect());

    drawQuickJumpOverlay(p);
}
