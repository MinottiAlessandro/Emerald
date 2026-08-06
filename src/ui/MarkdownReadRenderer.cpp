#include "MarkdownReadRenderer.h"

#include "MathRender.h"
#include "MarkdownReadObjectRenderer.h"
#include "core/ContentSecurity.h"
#include "core/MascotSeed.h"

#include <QImageReader>
#include <QRegularExpression>
#include <QTextBlockFormat>
#include <QTextBlockUserData>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

namespace {
class ReadBlockData final : public QTextBlockUserData {
public:
    int sourceBlock = -1;
    int sourceStart = 0;
    int sourceLength = 0;
};

double headingScale(int level) {
    switch (level) {
    case 1:  return 2.0;
    case 2:  return 1.6;
    case 3:  return 1.35;
    case 4:  return 1.15;
    case 5:  return 1.0;
    default: return 0.92;
    }
}

QFont monospaceFont(const QFont &base) {
    QFont font(base);
    font.setFamilies({QStringLiteral("Menlo"), QStringLiteral("Consolas"),
                      QStringLiteral("DejaVu Sans Mono"),
                      QStringLiteral("Liberation Mono"),
                      QStringLiteral("monospace")});
    font.setStyleHint(QFont::Monospace);
    return font;
}

template <typename Change>
QTextCharFormat merged(const QTextCharFormat &base, Change change) {
    QTextCharFormat format = base;
    change(format);
    return format;
}

int closingMarker(const QString &text, const QString &marker, int from) {
    const int end = text.indexOf(marker, from);
    return end > from ? end : -1;
}

// A deliberately small inline parser. It handles Emerald's existing live-
// preview syntax without passing note content through HTML or loading external
// resources. Recursive formatting lets emphasis nest inside links/strong text.
void insertInline(QTextCursor &cursor, const QString &text,
                  const QTextCharFormat &base,
                  const MarkdownReadRenderer::Options &options) {
    const QColor accent(0x58, 0xd6, 0x91);
    const QColor muted(0x79, 0x9a, 0x88);
    int pos = 0;
    while (pos < text.size()) {
        if (text.at(pos) == QLatin1Char('\\') && pos + 1 < text.size()) {
            cursor.insertText(text.mid(pos + 1, 1), base);
            pos += 2;
            continue;
        }

        if (text.mid(pos, 2) == QStringLiteral("[[")) {
            const int end = text.indexOf(QStringLiteral("]]"), pos + 2);
            if (end >= 0) {
                const QString inside = text.mid(pos + 2, end - pos - 2);
                const int separator = inside.indexOf(QLatin1Char('|'));
                const QString label =
                    (separator >= 0 ? inside.mid(separator + 1) : inside).trimmed();
                const QTextCharFormat link = merged(base, [&](QTextCharFormat &f) {
                    f.setForeground(accent);
                    f.setFontUnderline(true);
                });
                insertInline(cursor, label, link, options);
                pos = end + 2;
                continue;
            }
        }

        if (text.at(pos) == QLatin1Char('[') &&
            (pos == 0 || text.at(pos - 1) != QLatin1Char('!'))) {
            const int labelEnd = text.indexOf(QStringLiteral("]("), pos + 1);
            if (labelEnd >= 0) {
                const int targetEnd = text.indexOf(QLatin1Char(')'), labelEnd + 2);
                if (targetEnd >= 0) {
                    const QTextCharFormat link =
                        merged(base, [&](QTextCharFormat &f) {
                            f.setForeground(accent);
                            f.setFontUnderline(true);
                        });
                    insertInline(cursor,
                                 text.mid(pos + 1, labelEnd - pos - 1), link,
                                 options);
                    pos = targetEnd + 1;
                    continue;
                }
            }
        }

        if (text.mid(pos, 2) == QStringLiteral("![")) {
            const int labelEnd = text.indexOf(QStringLiteral("]("), pos + 2);
            if (labelEnd >= 0) {
                const int targetEnd = text.indexOf(QLatin1Char(')'), labelEnd + 2);
                if (targetEnd >= 0) {
                    QString label = text.mid(pos + 2, labelEnd - pos - 2).trimmed();
                    if (label.isEmpty())
                        label = QStringLiteral("Image");
                    QTextCharFormat image = base;
                    image.setForeground(muted);
                    image.setFontItalic(true);
                    cursor.insertText(QStringLiteral("[%1]").arg(label), image);
                    pos = targetEnd + 1;
                    continue;
                }
            }
        }

        QString pairedMarker;
        enum PairedStyle { None, Strong, Strike, Highlight } pairedStyle = None;
        if (text.mid(pos, 2) == QStringLiteral("**") ||
            text.mid(pos, 2) == QStringLiteral("__")) {
            pairedMarker = text.mid(pos, 2);
            pairedStyle = Strong;
        } else if (text.mid(pos, 2) == QStringLiteral("~~")) {
            pairedMarker = QStringLiteral("~~");
            pairedStyle = Strike;
        } else if (text.mid(pos, 2) == QStringLiteral("==")) {
            pairedMarker = QStringLiteral("==");
            pairedStyle = Highlight;
        }
        if (pairedStyle != None) {
            const int end = closingMarker(text, pairedMarker, pos + 2);
            if (end >= 0) {
                QTextCharFormat format = base;
                if (pairedStyle == Strong)
                    format.setFontWeight(QFont::Bold);
                else if (pairedStyle == Strike)
                    format.setFontStrikeOut(true);
                else {
                    format.setBackground(QColor(0x55, 0x66, 0x22));
                    format.setForeground(QColor(0xe7, 0xf2, 0xc5));
                }
                insertInline(cursor, text.mid(pos + 2, end - pos - 2), format,
                             options);
                pos = end + 2;
                continue;
            }
        }

        if (text.at(pos) == QLatin1Char('`')) {
            const int end = closingMarker(text, QStringLiteral("`"), pos + 1);
            if (end >= 0) {
                QTextCharFormat code = base;
                code.setFont(monospaceFont(base.font()));
                code.setBackground(QColor(0x22, 0x2f, 0x28));
                code.setForeground(QColor(0xd5, 0xe9, 0xde));
                cursor.insertText(text.mid(pos + 1, end - pos - 1), code);
                pos = end + 1;
                continue;
            }
        }

        if (text.at(pos) == QLatin1Char('$')) {
            const auto math = MathRender::pattern().match(text, pos);
            if (math.hasMatch() && math.capturedStart(0) == pos) {
                const QTextCharFormat formula =
                    MarkdownReadObjectRenderer::inlineMathFormat(
                        options.baseFont, math.captured(1));
                cursor.insertText(
                    QString(1, QChar(QChar::ObjectReplacementCharacter)),
                    formula);
                pos = math.capturedEnd(0);
                continue;
            }
        }

        if (text.at(pos) == QLatin1Char('*') ||
            text.at(pos) == QLatin1Char('_')) {
            const QString marker(text.at(pos));
            const int end = closingMarker(text, marker, pos + 1);
            if (end >= 0) {
                const QTextCharFormat emphasis =
                    merged(base, [](QTextCharFormat &f) { f.setFontItalic(true); });
                insertInline(cursor, text.mid(pos + 1, end - pos - 1), emphasis,
                             options);
                pos = end + 1;
                continue;
            }
        }

        int next = pos + 1;
        while (next < text.size() &&
               !QStringLiteral("\\[!*_`~=$").contains(text.at(next)))
            ++next;
        cursor.insertText(text.mid(pos, next - pos), base);
        pos = next;
    }
}

QTextBlockFormat baseBlockFormat(int lineSpacing) {
    QTextBlockFormat format;
    format.setLineHeight(qBound(100, lineSpacing, 300),
                         QTextBlockFormat::ProportionalHeight);
    format.setBottomMargin(5.0);
    return format;
}

void attachSourceData(QTextBlock block, int sourceBlock, int sourceStart,
                      int sourceLength) {
    auto *data = new ReadBlockData;
    data->sourceBlock = sourceBlock;
    data->sourceStart = sourceStart;
    data->sourceLength = sourceLength;
    block.setUserData(data);
}
} // namespace

void MarkdownReadRenderer::render(QTextDocument *target, const QString &source,
                                  const Options &options) {
    if (!target)
        return;

    target->setUndoRedoEnabled(false);
    target->clear();
    target->setDefaultFont(options.baseFont);
    target->setDocumentMargin(16.0);

    QTextCursor cursor(target);
    const QStringList lines = source.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QVector<int> lineStarts;
    lineStarts.reserve(lines.size());
    int nextLineStart = 0;
    for (const QString &line : lines) {
        lineStarts.append(nextLineStart);
        nextLineStart += line.size() + 1;
    }
    const qreal baseSize = options.baseFont.pointSizeF() > 0
                               ? options.baseFont.pointSizeF()
                               : 12.0;
    const QTextCharFormat body = [&] {
        QTextCharFormat f;
        f.setFont(options.baseFont);
        f.setForeground(QColor(0xd7, 0xee, 0xe2));
        return f;
    }();
    const QFont mono = monospaceFont(options.baseFont);
    const QRegularExpression headingRe(QStringLiteral("^(#{1,6})\\s+(.*)$"));
    const QRegularExpression fenceRe(
        QStringLiteral("^\\s*(```|~~~)\\s*([^`~]*)$"));
    const QRegularExpression listRe(QStringLiteral(
        "^(\\s*)([-*+]|\\d+[.)])\\s+(?:\\[([ xX])\\]\\s+)?(.*)$"));
    const QRegularExpression ruleRe(
        QStringLiteral("^\\s*([-*_])\\s*(?:\\1\\s*){2,}$"));
    const QRegularExpression tableRe(QStringLiteral("^\\s*\\|.*\\|\\s*$"));
    const QRegularExpression imageRe(QStringLiteral(
        "^\\s*!\\[([^]\\n]*)\\]\\((?:<([^>]+)>|([^\\)\\n]+))\\)\\s*$"));

    bool firstOutput = true;
    for (int sourceBlock = 0; sourceBlock < lines.size(); ++sourceBlock) {
        const QString line = lines.at(sourceBlock);
        const int lineStart = lineStarts.at(sourceBlock);
        int sourceEndBlock = sourceBlock;

        if (sourceBlock == 0 && MascotSeed::fromLine(line) != 0)
            continue;

        QTextBlockFormat block = baseBlockFormat(options.lineSpacing);
        QTextCharFormat text = body;
        QTextCharFormat object;
        QString content = line;
        bool parseInline = true;
        bool taskItem = false;
        bool checkedTask = false;

        if (const auto fence = fenceRe.match(line); fence.hasMatch()) {
            const QString marker = fence.captured(1);
            QStringList codeLines;
            int closingBlock = lines.size();
            for (int blockNumber = sourceBlock + 1;
                 blockNumber < lines.size(); ++blockNumber) {
                if (lines.at(blockNumber).trimmed().startsWith(marker)) {
                    closingBlock = blockNumber;
                    break;
                }
                codeLines.append(lines.at(blockNumber));
            }
            sourceEndBlock = closingBlock < lines.size()
                                 ? closingBlock
                                 : lines.size() - 1;
            object = MarkdownReadObjectRenderer::codeBlockFormat(
                options.baseFont, fence.captured(2),
                codeLines.join(QLatin1Char('\n')), options.fallbackWidth);
            block.setTopMargin(baseSize * 0.35);
            block.setBottomMargin(baseSize * 0.55);
            block.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
            parseInline = false;
        } else if (const auto displayMath =
                       MathRender::displayPattern().match(line);
                   displayMath.hasMatch()) {
            object = MarkdownReadObjectRenderer::displayMathFormat(
                options.baseFont, displayMath.captured(1), options.fallbackWidth);
            block.setTopMargin(baseSize * 0.3);
            block.setBottomMargin(baseSize * 0.3);
            block.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
            parseInline = false;
        } else if (MathRender::opensBlock(line)) {
            QStringList formulaParts{MathRender::bodyAfterOpen(line)};
            for (int blockNumber = sourceBlock + 1;
                 blockNumber < lines.size(); ++blockNumber) {
                sourceEndBlock = blockNumber;
                const QString formulaLine = lines.at(blockNumber);
                if (formulaLine.contains(QStringLiteral("$$"))) {
                    formulaParts.append(
                        MathRender::bodyBeforeClose(formulaLine));
                    break;
                }
                formulaParts.append(formulaLine);
            }
            object = MarkdownReadObjectRenderer::displayMathFormat(
                options.baseFont,
                formulaParts.join(QLatin1Char(' ')).simplified(),
                options.fallbackWidth);
            block.setTopMargin(baseSize * 0.3);
            block.setBottomMargin(baseSize * 0.3);
            block.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
            parseInline = false;
        } else if (const auto image = imageRe.match(line); image.hasMatch()) {
            const QString rawTarget = !image.captured(2).isEmpty()
                                          ? image.captured(2)
                                          : image.captured(3);
            const QString decodedTarget =
                QUrl::fromPercentEncoding(rawTarget.toUtf8());
            const QString path = ContentSecurity::resolveLocalImage(
                decodedTarget, options.imageBasePath, options.vaultRootPath);
            QSize sourceSize;
            if (!path.isEmpty()) {
                QImageReader reader(path);
                reader.setAutoTransform(true);
                sourceSize = reader.size();
            }
            object = MarkdownReadObjectRenderer::imageFormat(
                options.baseFont, path, decodedTarget, image.captured(1),
                sourceSize, options.fallbackWidth, options.maxImageHeight);
            block.setTopMargin(baseSize * 0.25);
            block.setBottomMargin(baseSize * 0.35);
            block.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
            parseInline = false;
        } else if (const auto heading = headingRe.match(line);
                   heading.hasMatch()) {
            const int level = heading.capturedLength(1);
            content = heading.captured(2);
            text.setFontPointSize(baseSize * headingScale(level));
            text.setFontWeight(level <= 3 ? QFont::Bold : QFont::DemiBold);
            text.setForeground(QColor(0xe3, 0xf5, 0xec));
            block.setTopMargin(baseSize * (level == 1 ? 0.9 : 0.55));
            block.setBottomMargin(baseSize * (level <= 2 ? 0.42 : 0.28));
        } else {
            int quoteDepth = 0;
            int quoteEnd = 0;
            while (quoteEnd < content.size()) {
                while (quoteEnd < content.size() &&
                       content.at(quoteEnd).isSpace())
                    ++quoteEnd;
                if (quoteEnd >= content.size() ||
                    content.at(quoteEnd) != QLatin1Char('>'))
                    break;
                ++quoteDepth;
                ++quoteEnd;
            }
            if (quoteDepth > 0) {
                while (quoteEnd < content.size() &&
                       content.at(quoteEnd).isSpace())
                    ++quoteEnd;
                content = content.mid(quoteEnd);
                block.setLeftMargin(14.0 + quoteDepth * 16.0);
                block.setRightMargin(8.0);
                block.setBackground(QColor(0x19, 0x26, 0x1f));
                text.setForeground(QColor(0xb9, 0xd6, 0xc7));
                text.setFontItalic(true);
            } else if (const auto list = listRe.match(content);
                       list.hasMatch()) {
                int columns = 0;
                for (const QChar ch : list.captured(1))
                    columns += ch == QLatin1Char('\t') ? 2 : 1;
                const int depth = (columns + 1) / 2;
                QString marker = list.captured(2);
                if (list.capturedStart(3) >= 0) {
                    taskItem = true;
                    checkedTask =
                        !list.captured(3).trimmed().isEmpty();
                    content = list.captured(4);
                    if (checkedTask) {
                        text.setFontStrikeOut(true);
                        text.setForeground(QColor(0x78, 0x93, 0x84));
                    }
                } else if (!marker.at(0).isDigit()) {
                    static const QChar bullets[] = {QChar(0x2022), QChar(0x25E6),
                                                    QChar(0x25AA)};
                    marker = QString(bullets[depth % 3]);
                }
                if (!taskItem)
                    content = marker + QLatin1Char(' ') + list.captured(4);
                block.setLeftMargin(18.0 + depth * 22.0);
                block.setTextIndent(-14.0);
            } else if (ruleRe.match(content).hasMatch()) {
                object = MarkdownReadObjectRenderer::ruleFormat(
                    options.baseFont, options.fallbackWidth);
                block.setTopMargin(baseSize * 0.15);
                block.setBottomMargin(baseSize * 0.15);
                block.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
                parseInline = false;
            } else if (tableRe.match(content).hasMatch()) {
                text.setFont(mono);
                text.setForeground(QColor(0xb8, 0xd4, 0xc5));
                block.setBackground(QColor(0x12, 0x1d, 0x18));
                block.setLeftMargin(8.0);
                block.setRightMargin(8.0);
                parseInline = false;
            }
        }

        if (!firstOutput)
            cursor.insertBlock(block);
        else
            cursor.setBlockFormat(block);
        firstOutput = false;

        if (object.objectType() == MarkdownReadObjectRenderer::ObjectType) {
            cursor.insertText(
                QString(1, QChar(QChar::ObjectReplacementCharacter)), object);
        } else if (parseInline) {
            if (taskItem) {
                cursor.insertText(
                    QString(1, QChar(QChar::ObjectReplacementCharacter)),
                    MarkdownReadObjectRenderer::checkboxFormat(
                        options.baseFont, checkedTask));
                cursor.insertText(QStringLiteral(" "), text);
            }
            insertInline(cursor, content, text, options);
        } else {
            cursor.insertText(content, text);
        }
        const int sourceEnd = lineStarts.at(sourceEndBlock) +
                              lines.at(sourceEndBlock).size();
        attachSourceData(cursor.block(), sourceBlock, lineStart,
                         sourceEnd - lineStart);
        sourceBlock = sourceEndBlock;
    }

    // A note containing only a mascot header still needs a valid visible block.
    if (firstOutput) {
        cursor.setBlockFormat(baseBlockFormat(options.lineSpacing));
        attachSourceData(cursor.block(), 0, 0, 0);
    }
    target->setModified(false);
}

int MarkdownReadRenderer::sourceBlockNumber(const QTextBlock &block) {
    if (!block.isValid())
        return -1;
    if (const auto *data = dynamic_cast<const ReadBlockData *>(block.userData()))
        return data->sourceBlock;
    return -1;
}

QTextBlock MarkdownReadRenderer::blockForSourceBlock(QTextDocument *document,
                                                     int sourceBlockNumber) {
    if (!document)
        return {};
    QTextBlock nearest;
    for (QTextBlock block = document->firstBlock(); block.isValid();
         block = block.next()) {
        const int mapped = MarkdownReadRenderer::sourceBlockNumber(block);
        if (mapped == sourceBlockNumber)
            return block;
        if (mapped > sourceBlockNumber)
            return nearest.isValid() ? nearest : block;
        nearest = block;
    }
    return nearest;
}
