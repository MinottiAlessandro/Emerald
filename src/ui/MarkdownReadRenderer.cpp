#include "MarkdownReadRenderer.h"

#include "MathRender.h"
#include "MarkdownReadObjectRenderer.h"
#include "core/ContentSecurity.h"
#include "core/MascotSeed.h"
#include "core/WikiLink.h"

#include <QFontMetricsF>
#include <QImageReader>
#include <QRegularExpression>
#include <QTextBlockFormat>
#include <QTextBlockUserData>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLength>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableCellFormat>
#include <QTextTableFormat>
#include <QUrl>
#include <limits>

namespace {
struct ReadSourceRange {
    int readStart = 0;
    int readLength = 0;
    int sourceStart = 0;
    int sourceLength = 0;
};

using ReadSourceRanges = QList<ReadSourceRange>;

class ReadBlockData final : public QTextBlockUserData {
public:
    int sourceBlock = -1;
    int sourceStart = 0;
    int sourceLength = 0;
    ReadSourceRanges ranges;
};

void appendSourceRange(ReadSourceRanges *ranges, int readStart, int readLength,
                       int sourceStart, int sourceLength) {
    if (!ranges || readLength <= 0 || sourceLength < 0)
        return;
    if (!ranges->isEmpty()) {
        ReadSourceRange &last = ranges->last();
        const bool directLast = last.readLength == last.sourceLength;
        const bool directCurrent = readLength == sourceLength;
        if (directLast && directCurrent &&
            last.readStart + last.readLength == readStart &&
            last.sourceStart + last.sourceLength == sourceStart) {
            last.readLength += readLength;
            last.sourceLength += sourceLength;
            return;
        }
    }
    ranges->append({readStart, readLength, sourceStart, sourceLength});
}

void insertMappedText(QTextCursor &cursor, const QString &display,
                      const QTextCharFormat &format, int sourceStart,
                      int sourceLength, ReadSourceRanges *ranges) {
    if (display.isEmpty())
        return;
    const int readStart = cursor.position();
    cursor.insertText(display, format);
    appendSourceRange(ranges, readStart, display.size(), sourceStart,
                      sourceLength);
}

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
                  const MarkdownReadRenderer::Options &options,
                  int sourceOffset, ReadSourceRanges *ranges) {
    const QColor accent(0x58, 0xd6, 0x91);
    const QColor muted(0x79, 0x9a, 0x88);
    int pos = 0;
    while (pos < text.size()) {
        if (text.at(pos) == QLatin1Char('\\') && pos + 1 < text.size()) {
            insertMappedText(cursor, text.mid(pos + 1, 1), base,
                             sourceOffset + pos, 2, ranges);
            pos += 2;
            continue;
        }

        if (text.mid(pos, 2) == QStringLiteral("[[")) {
            const int end = text.indexOf(QStringLiteral("]]"), pos + 2);
            if (end >= 0) {
                const QString inside = text.mid(pos + 2, end - pos - 2);
                const int separator = inside.indexOf(QLatin1Char('|'));
                const int labelStart = separator >= 0 ? separator + 1 : 0;
                const QString rawLabel = inside.mid(labelStart);
                int leadingSpace = 0;
                while (leadingSpace < rawLabel.size() &&
                       rawLabel.at(leadingSpace).isSpace())
                    ++leadingSpace;
                int trailingSpace = rawLabel.size();
                while (trailingSpace > leadingSpace &&
                       rawLabel.at(trailingSpace - 1).isSpace())
                    --trailingSpace;
                const QString label = rawLabel.mid(
                    leadingSpace, trailingSpace - leadingSpace);
                const QString target = WikiLink::cleanTarget(inside);
                const QTextCharFormat link = merged(base, [&](QTextCharFormat &f) {
                    f.setForeground(accent);
                    f.setFontUnderline(true);
                    if (!target.isEmpty()) {
                        f.setAnchor(true);
                        f.setAnchorHref(
                            MarkdownReadRenderer::wikiLinkHref(target));
                        f.setToolTip(target);
                    }
                });
                insertInline(cursor, label, link, options,
                             sourceOffset + pos + 2 + labelStart +
                                 leadingSpace,
                             ranges);
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
                    const QString target =
                        text.mid(labelEnd + 2, targetEnd - labelEnd - 2);
                    const QTextCharFormat link =
                        merged(base, [&](QTextCharFormat &f) {
                            f.setForeground(accent);
                            f.setFontUnderline(true);
                            if (!target.isEmpty()) {
                                f.setAnchor(true);
                                f.setAnchorHref(target);
                                f.setToolTip(target);
                            }
                        });
                    insertInline(cursor,
                                 text.mid(pos + 1, labelEnd - pos - 1), link,
                                 options, sourceOffset + pos + 1, ranges);
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
                    insertMappedText(cursor, QStringLiteral("[%1]").arg(label),
                                     image, sourceOffset + pos,
                                     targetEnd + 1 - pos, ranges);
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
                             options, sourceOffset + pos + 2, ranges);
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
                insertMappedText(cursor,
                                 text.mid(pos + 1, end - pos - 1), code,
                                 sourceOffset + pos + 1, end - pos - 1,
                                 ranges);
                pos = end + 1;
                continue;
            }
        }

        if (text.at(pos) == QLatin1Char('$')) {
            const auto math = MathRender::pattern().match(text, pos);
            if (math.hasMatch() && math.capturedStart(0) == pos) {
                QTextCharFormat formula =
                    MarkdownReadObjectRenderer::inlineMathFormat(
                        options.baseFont, math.captured(1));
                if (base.isAnchor()) {
                    formula.setAnchor(true);
                    formula.setAnchorHref(base.anchorHref());
                }
                insertMappedText(
                    cursor,
                    QString(1, QChar(QChar::ObjectReplacementCharacter)),
                    formula, sourceOffset + pos,
                    math.capturedEnd(0) - math.capturedStart(0), ranges);
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
                             options, sourceOffset + pos + 1, ranges);
                pos = end + 1;
                continue;
            }
        }

        int next = pos + 1;
        while (next < text.size() &&
               !QStringLiteral("\\[!*_`~=$").contains(text.at(next)))
            ++next;
        insertMappedText(cursor, text.mid(pos, next - pos), base,
                         sourceOffset + pos, next - pos, ranges);
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
                      int sourceLength,
                      const ReadSourceRanges &absoluteRanges = {}) {
    auto *data = new ReadBlockData;
    data->sourceBlock = sourceBlock;
    data->sourceStart = sourceStart;
    data->sourceLength = sourceLength;
    data->ranges = absoluteRanges;
    for (ReadSourceRange &range : data->ranges)
        range.readStart -= block.position();
    if (data->ranges.size() == 1) {
        const ReadSourceRange &range = data->ranges.first();
        if (range.readStart == 0 && range.readLength == block.text().size() &&
            range.sourceStart == sourceStart &&
            range.sourceLength == sourceLength) {
            // An ordinary untransformed paragraph maps 1:1 through the coarse
            // block fields; avoid retaining a redundant heap allocation.
            data->ranges.clear();
        }
    }
    block.setUserData(data);
}

bool isPipeTableRow(const QString &text) {
    const QString trimmed = text.trimmed();
    return trimmed.size() >= 2 && trimmed.startsWith(QLatin1Char('|')) &&
           trimmed.endsWith(QLatin1Char('|'));
}

struct ReadTableCell {
    QString text;
    int sourceStart = 0;
};
using ReadTableRow = QList<ReadTableCell>;

ReadTableCell trimmedTableCell(const QString &text, int sourceStart) {
    int first = 0;
    while (first < text.size() && text.at(first).isSpace())
        ++first;
    int last = text.size();
    while (last > first && text.at(last - 1).isSpace())
        --last;
    return {text.mid(first, last - first), sourceStart + first};
}

ReadTableRow splitMarkdownTableRow(const QString &row) {
    int begin = 0;
    while (begin < row.size() && row.at(begin).isSpace())
        ++begin;
    if (begin < row.size() && row.at(begin) == QLatin1Char('|'))
        ++begin;

    int end = row.size();
    while (end > begin && row.at(end - 1).isSpace())
        --end;
    if (end > begin && row.at(end - 1) == QLatin1Char('|'))
        --end;

    ReadTableRow cells;
    QString cell;
    int cellSourceStart = begin;
    bool inCode = false;
    bool inWikiLink = false;
    for (int pos = begin; pos < end; ++pos) {
        const QChar ch = row.at(pos);
        if (ch == QLatin1Char('\\') && pos + 1 < end &&
            row.at(pos + 1) == QLatin1Char('|')) {
            cell += ch;
            cell += row.at(pos + 1);
            ++pos;
            continue;
        }
        if (ch == QLatin1Char('`')) {
            inCode = !inCode;
            cell += ch;
            continue;
        }
        if (!inCode && !inWikiLink &&
            row.mid(pos, 2) == QStringLiteral("[[") &&
            row.indexOf(QStringLiteral("]]"), pos + 2) >= 0) {
            inWikiLink = true;
        } else if (!inCode && inWikiLink &&
            row.mid(pos, 2) == QStringLiteral("]]")) {
            inWikiLink = false;
        }
        if (ch == QLatin1Char('|') && !inCode && !inWikiLink) {
            cells.append(trimmedTableCell(cell, cellSourceStart));
            cell.clear();
            cellSourceStart = pos + 1;
            continue;
        }
        cell += ch;
    }
    cells.append(trimmedTableCell(cell, cellSourceStart));
    return cells;
}

bool tableSeparator(const ReadTableRow &cells,
                    QList<Qt::Alignment> *alignments) {
    if (cells.isEmpty())
        return false;
    QList<Qt::Alignment> parsed;
    static const QRegularExpression separatorRe(
        QStringLiteral("^:?-{3,}:?$"));
    static const QRegularExpression whitespaceRe(QStringLiteral("\\s"));
    for (const ReadTableCell &sourceCell : cells) {
        QString cell = sourceCell.text;
        cell.remove(whitespaceRe);
        if (!separatorRe.match(cell).hasMatch())
            return false;
        const bool left = cell.startsWith(QLatin1Char(':'));
        const bool right = cell.endsWith(QLatin1Char(':'));
        parsed.append(left && right ? Qt::AlignCenter
                                   : right ? Qt::AlignRight : Qt::AlignLeft);
    }
    if (alignments)
        *alignments = parsed;
    return true;
}

void insertReadTable(QTextCursor &cursor, const QList<ReadTableRow> &rows,
                     const QList<int> &sourceRows,
                     const QList<Qt::Alignment> &alignments,
                     const QStringList &sourceLines,
                     const QVector<int> &lineStarts,
                     const MarkdownReadRenderer::Options &options) {
    if (rows.isEmpty())
        return;
    int columns = alignments.size();
    for (const ReadTableRow &row : rows)
        columns = qMax(columns, row.size());
    columns = qMax(1, columns);

    QTextTableFormat tableFormat;
    tableFormat.setAlignment(Qt::AlignLeft);
    tableFormat.setWidth(QTextLength(QTextLength::PercentageLength, 100.0));
    tableFormat.setCellPadding(8.0);
    tableFormat.setCellSpacing(0.0);
    tableFormat.setBorder(1.0);
    tableFormat.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tableFormat.setBorderBrush(QColor(0x31, 0x51, 0x40));
    tableFormat.setBorderCollapse(true);
    tableFormat.setTopMargin(7.0);
    tableFormat.setBottomMargin(9.0);
    tableFormat.setHeaderRowCount(1);
    QList<qreal> naturalWidths(columns, 48.0);
    const QFontMetricsF metrics(options.baseFont);
    for (const ReadTableRow &row : rows) {
        for (int column = 0; column < row.size(); ++column) {
            naturalWidths[column] =
                qMax(naturalWidths.at(column),
                     qBound(qreal(48.0),
                            metrics.horizontalAdvance(row.at(column).text) +
                                18.0,
                            qreal(240.0)));
        }
    }
    qreal totalNaturalWidth = 0.0;
    for (const qreal width : naturalWidths)
        totalNaturalWidth += width;
    QList<QTextLength> widths;
    for (int column = 0; column < columns; ++column) {
        widths.append(QTextLength(QTextLength::PercentageLength,
                                  naturalWidths.at(column) /
                                      totalNaturalWidth * 100.0));
    }
    tableFormat.setColumnWidthConstraints(widths);

    QTextTable *table = cursor.insertTable(rows.size(), columns, tableFormat);
    for (int row = 0; row < rows.size(); ++row) {
        const int sourceRow = sourceRows.at(row);
        for (int column = 0; column < columns; ++column) {
            QTextTableCell cell = table->cellAt(row, column);
            QTextTableCellFormat cellFormat = cell.format().toTableCellFormat();
            if (row == 0)
                cellFormat.setBackground(QColor(0x1a, 0x35, 0x27));
            else if ((row % 2) == 0)
                cellFormat.setBackground(QColor(0x15, 0x24, 0x1c));
            else
                cellFormat.setBackground(QColor(0x11, 0x1d, 0x17));
            cell.setFormat(cellFormat);

            QTextCursor cellCursor = cell.firstCursorPosition();
            QTextBlockFormat block = baseBlockFormat(options.lineSpacing);
            block.setBottomMargin(0.0);
            block.setAlignment(column < alignments.size()
                                   ? alignments.at(column)
                                   : Qt::AlignLeft);
            cellCursor.setBlockFormat(block);

            QTextCharFormat text;
            text.setFont(options.baseFont);
            text.setForeground(row == 0 ? QColor(0xe3, 0xf5, 0xec)
                                        : QColor(0xc8, 0xe0, 0xd4));
            if (row == 0)
                text.setFontWeight(QFont::DemiBold);
            ReadSourceRanges ranges;
            if (column < rows.at(row).size()) {
                const ReadTableCell &sourceCell = rows.at(row).at(column);
                insertInline(cellCursor, sourceCell.text, text, options,
                             lineStarts.at(sourceRow) + sourceCell.sourceStart,
                             &ranges);
            }

            attachSourceData(cell.firstCursorPosition().block(), sourceRow,
                             lineStarts.at(sourceRow),
                             sourceLines.at(sourceRow).size(), ranges);
        }
    }

    cursor = table->lastCursorPosition();
    cursor.movePosition(QTextCursor::NextBlock);
}
} // namespace

QString MarkdownReadRenderer::wikiLinkHref(const QString &target) {
    if (target.isEmpty())
        return {};
    return QStringLiteral("emerald-note:") +
           QString::fromLatin1(QUrl::toPercentEncoding(target));
}

QString MarkdownReadRenderer::wikiTargetFromHref(const QString &href) {
    static const QString prefix = QStringLiteral("emerald-note:");
    if (!href.startsWith(prefix, Qt::CaseSensitive))
        return {};
    return QUrl::fromPercentEncoding(href.mid(prefix.size()).toUtf8());
}

namespace {
int scaledOffset(int offset, int fromLength, int toLength) {
    if (fromLength <= 0 || toLength <= 0)
        return 0;
    return qBound(0, qRound(qreal(offset) * toLength / fromLength),
                  toLength);
}

int sourcePositionForReadPosition(QTextDocument *readDocument,
                                  int readPosition, bool preferNext) {
    if (!readDocument)
        return 0;
    readPosition = qBound(0, readPosition,
                          qMax(0, readDocument->characterCount() - 1));
    const QTextBlock block = readDocument->findBlock(readPosition);
    const auto *data = block.isValid()
                           ? dynamic_cast<const ReadBlockData *>(block.userData())
                           : nullptr;
    if (!data)
        return 0;

    const int column = readPosition - block.position();
    const ReadSourceRange *nearest = nullptr;
    int nearestDistance = std::numeric_limits<int>::max();
    for (const ReadSourceRange &range : data->ranges) {
        const int readEnd = range.readStart + range.readLength;
        if (column == range.readStart && preferNext)
            return range.sourceStart;
        if (column > range.readStart && column < readEnd) {
            return range.sourceStart +
                   scaledOffset(column - range.readStart, range.readLength,
                                range.sourceLength);
        }
        if (column == readEnd && !preferNext)
            return range.sourceStart + range.sourceLength;
        const int distance = column < range.readStart
                                 ? range.readStart - column
                                 : column - readEnd;
        if (distance < nearestDistance) {
            nearest = &range;
            nearestDistance = distance;
        }
    }
    if (nearest) {
        return column < nearest->readStart
                   ? nearest->sourceStart
                   : nearest->sourceStart + nearest->sourceLength;
    }
    return data->sourceStart +
           scaledOffset(column, qMax(0, block.text().size()),
                        data->sourceLength);
}

int readPositionForSourcePosition(QTextDocument *readDocument,
                                  int sourcePosition, bool preferNext) {
    if (!readDocument)
        return 0;
    int nearestPosition = 0;
    int nearestDistance = std::numeric_limits<int>::max();
    for (QTextBlock block = readDocument->firstBlock(); block.isValid();
         block = block.next()) {
        const auto *data =
            dynamic_cast<const ReadBlockData *>(block.userData());
        if (!data)
            continue;
        for (const ReadSourceRange &range : data->ranges) {
            const int sourceEnd = range.sourceStart + range.sourceLength;
            if (sourcePosition == range.sourceStart && preferNext)
                return block.position() + range.readStart;
            if (sourcePosition > range.sourceStart &&
                sourcePosition < sourceEnd) {
                return block.position() + range.readStart +
                       scaledOffset(sourcePosition - range.sourceStart,
                                    range.sourceLength, range.readLength);
            }
            if (sourcePosition == sourceEnd && !preferNext)
                return block.position() + range.readStart + range.readLength;
            const bool before = sourcePosition < range.sourceStart;
            const int distance = before ? range.sourceStart - sourcePosition
                                        : sourcePosition - sourceEnd;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestPosition = block.position() + range.readStart +
                                  (before ? 0 : range.readLength);
            }
        }
        if (data->ranges.isEmpty()) {
            const int sourceEnd = data->sourceStart + data->sourceLength;
            const bool before = sourcePosition < data->sourceStart;
            const bool inside = !before && sourcePosition <= sourceEnd;
            if (inside) {
                return block.position() +
                       scaledOffset(sourcePosition - data->sourceStart,
                                    data->sourceLength, block.text().size());
            }
            const int distance = before ? data->sourceStart - sourcePosition
                                        : sourcePosition - sourceEnd;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestPosition = block.position() +
                                  (before ? 0 : block.text().size());
            }
        }
    }
    return qBound(0, nearestPosition,
                  qMax(0, readDocument->characterCount() - 1));
}
} // namespace

QTextCursor MarkdownReadRenderer::mapToReadCursor(
    QTextDocument *readDocument, const QTextCursor &sourceCursor) {
    QTextCursor mapped(readDocument);
    if (!readDocument || sourceCursor.isNull())
        return mapped;
    const bool hasSelection = sourceCursor.hasSelection();
    const bool forward = sourceCursor.anchor() <= sourceCursor.position();
    mapped.setPosition(readPositionForSourcePosition(
        readDocument, sourceCursor.anchor(), !hasSelection || forward));
    mapped.setPosition(
        readPositionForSourcePosition(readDocument, sourceCursor.position(),
                                      !hasSelection || !forward),
        QTextCursor::KeepAnchor);
    return mapped;
}

QTextCursor MarkdownReadRenderer::mapToSourceCursor(
    QTextDocument *sourceDocument, const QTextCursor &readCursor) {
    QTextCursor mapped(sourceDocument);
    if (!sourceDocument || readCursor.isNull())
        return mapped;
    QTextDocument *readDocument = readCursor.document();
    const bool hasSelection = readCursor.hasSelection();
    const bool forward = readCursor.anchor() <= readCursor.position();
    mapped.setPosition(qBound(
        0,
        sourcePositionForReadPosition(readDocument, readCursor.anchor(),
                                      !hasSelection || forward),
        qMax(0, sourceDocument->characterCount() - 1)));
    mapped.setPosition(
        qBound(0,
               sourcePositionForReadPosition(readDocument,
                                             readCursor.position(),
                                             !hasSelection || !forward),
               qMax(0, sourceDocument->characterCount() - 1)),
        QTextCursor::KeepAnchor);
    return mapped;
}

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
    bool reuseCurrentBlock = false;
    for (int sourceBlock = 0; sourceBlock < lines.size(); ++sourceBlock) {
        const QString line = lines.at(sourceBlock);
        const int lineStart = lineStarts.at(sourceBlock);
        int sourceEndBlock = sourceBlock;

        if (sourceBlock == 0 && MascotSeed::fromLine(line) != 0)
            continue;

        if (isPipeTableRow(line) && sourceBlock + 1 < lines.size()) {
            const ReadTableRow header = splitMarkdownTableRow(line);
            QList<Qt::Alignment> alignments;
            if (tableSeparator(splitMarkdownTableRow(lines.at(sourceBlock + 1)),
                               &alignments)) {
                QList<ReadTableRow> tableRows{header};
                QList<int> sourceRows{sourceBlock};
                sourceEndBlock = sourceBlock + 1;
                for (int blockNumber = sourceBlock + 2;
                     blockNumber < lines.size() &&
                     isPipeTableRow(lines.at(blockNumber));
                     ++blockNumber) {
                    tableRows.append(
                        splitMarkdownTableRow(lines.at(blockNumber)));
                    sourceRows.append(blockNumber);
                    sourceEndBlock = blockNumber;
                }
                insertReadTable(cursor, tableRows, sourceRows, alignments,
                                lines, lineStarts, options);
                firstOutput = false;
                reuseCurrentBlock = true;
                sourceBlock = sourceEndBlock;
                continue;
            }
        }

        QTextBlockFormat block = baseBlockFormat(options.lineSpacing);
        QTextCharFormat text = body;
        QTextCharFormat object;
        QString content = line;
        int contentSourceOffset = lineStart;
        QString renderedPrefix;
        int prefixSourceStart = lineStart;
        int prefixSourceLength = 0;
        int taskSourceStart = lineStart;
        int taskSourceLength = 0;
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
            contentSourceOffset = lineStart + heading.capturedStart(2);
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
                contentSourceOffset = lineStart + quoteEnd;
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
                content = list.captured(4);
                contentSourceOffset =
                    lineStart + list.capturedStart(4);
                if (list.capturedStart(3) >= 0) {
                    taskItem = true;
                    checkedTask =
                        !list.captured(3).trimmed().isEmpty();
                    taskSourceStart =
                        lineStart + list.capturedStart(2);
                    taskSourceLength =
                        list.capturedStart(4) - list.capturedStart(2);
                    if (checkedTask) {
                        text.setFontStrikeOut(true);
                        text.setForeground(QColor(0x78, 0x93, 0x84));
                    }
                } else if (!marker.at(0).isDigit()) {
                    static const QChar bullets[] = {QChar(0x2022), QChar(0x25E6),
                                                    QChar(0x25AA)};
                    marker = QString(bullets[depth % 3]);
                }
                if (!taskItem) {
                    renderedPrefix = marker + QLatin1Char(' ');
                    prefixSourceStart =
                        lineStart + list.capturedStart(2);
                    prefixSourceLength =
                        list.capturedStart(4) - list.capturedStart(2);
                }
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

        if (!firstOutput) {
            if (reuseCurrentBlock)
                cursor.setBlockFormat(block);
            else
                cursor.insertBlock(block);
        } else {
            cursor.setBlockFormat(block);
        }
        firstOutput = false;
        reuseCurrentBlock = false;

        ReadSourceRanges ranges;
        const int sourceEnd = lineStarts.at(sourceEndBlock) +
                              lines.at(sourceEndBlock).size();
        if (object.objectType() == MarkdownReadObjectRenderer::ObjectType) {
            insertMappedText(
                cursor,
                QString(1, QChar(QChar::ObjectReplacementCharacter)), object,
                lineStart, sourceEnd - lineStart, &ranges);
        } else if (parseInline) {
            if (taskItem) {
                insertMappedText(
                    cursor,
                    QString(1, QChar(QChar::ObjectReplacementCharacter)),
                    MarkdownReadObjectRenderer::checkboxFormat(
                        options.baseFont, checkedTask),
                    taskSourceStart, taskSourceLength, &ranges);
                insertMappedText(cursor, QStringLiteral(" "), text,
                                 contentSourceOffset, 0, &ranges);
            } else if (!renderedPrefix.isEmpty()) {
                insertMappedText(cursor, renderedPrefix, text,
                                 prefixSourceStart, prefixSourceLength,
                                 &ranges);
            }
            insertInline(cursor, content, text, options, contentSourceOffset,
                         &ranges);
        } else {
            insertMappedText(cursor, content, text, lineStart, line.size(),
                             &ranges);
        }
        attachSourceData(cursor.block(), sourceBlock, lineStart,
                         sourceEnd - lineStart, ranges);
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
