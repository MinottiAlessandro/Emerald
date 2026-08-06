#include "MarkdownReadRenderer.h"

#include "core/MascotSeed.h"
#include "MathRender.h"

#include <QRegularExpression>
#include <QTextBlockFormat>
#include <QTextBlockUserData>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

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
                  const QTextCharFormat &base) {
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
                insertInline(cursor, label, link);
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
                                 text.mid(pos + 1, labelEnd - pos - 1), link);
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
                insertInline(cursor, text.mid(pos + 2, end - pos - 2), format);
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
                QTextCharFormat formula = base;
                formula.setForeground(QColor(0x6f, 0xcf, 0xc0));
                formula.setFontItalic(true);
                cursor.insertText(math.captured(1), formula);
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
                insertInline(cursor, text.mid(pos + 1, end - pos - 1), emphasis);
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
    const QRegularExpression mathFenceRe(QStringLiteral("^\\s*\\$\\$\\s*$"));
    const QRegularExpression listRe(QStringLiteral(
        "^(\\s*)([-*+]|\\d+[.)])\\s+(?:\\[([ xX])\\]\\s+)?(.*)$"));
    const QRegularExpression ruleRe(
        QStringLiteral("^\\s*([-*_])\\s*(?:\\1\\s*){2,}$"));
    const QRegularExpression tableRe(QStringLiteral("^\\s*\\|.*\\|\\s*$"));

    bool firstOutput = true;
    bool inCode = false;
    bool inMath = false;
    int sourceStart = 0;
    for (int sourceBlock = 0; sourceBlock < lines.size(); ++sourceBlock) {
        const QString line = lines.at(sourceBlock);
        const int lineStart = sourceStart;
        sourceStart += line.size() + 1;

        if (sourceBlock == 0 && MascotSeed::fromLine(line) != 0)
            continue;

        const auto fence = fenceRe.match(line);
        if (!inMath && fence.hasMatch()) {
            inCode = !inCode;
            continue;
        }
        if (!inCode && mathFenceRe.match(line).hasMatch()) {
            inMath = !inMath;
            continue;
        }

        QTextBlockFormat block = baseBlockFormat(options.lineSpacing);
        QTextCharFormat text = body;
        QString content = line;
        bool parseInline = true;

        if (inCode) {
            block.setLeftMargin(14.0);
            block.setRightMargin(14.0);
            block.setBackground(QColor(0x12, 0x1d, 0x18));
            block.setBottomMargin(0.0);
            text.setFont(mono);
            text.setForeground(QColor(0xc7, 0xdd, 0xd1));
            parseInline = false;
        } else if (inMath) {
            block.setAlignment(Qt::AlignCenter);
            block.setLeftMargin(18.0);
            block.setRightMargin(18.0);
            text.setForeground(QColor(0x6f, 0xcf, 0xc0));
            text.setFontItalic(true);
            parseInline = false;
        } else if (const auto displayMath = MathRender::displayPattern().match(line);
                   displayMath.hasMatch()) {
            content = displayMath.captured(1);
            block.setAlignment(Qt::AlignCenter);
            block.setTopMargin(baseSize * 0.35);
            block.setBottomMargin(baseSize * 0.35);
            text.setForeground(QColor(0x6f, 0xcf, 0xc0));
            text.setFontItalic(true);
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
                if (!list.captured(3).isNull()) {
                    marker = list.captured(3).trimmed().isEmpty()
                                 ? QString(QChar(0x2610))
                                 : QString(QChar(0x2611));
                } else if (!marker.at(0).isDigit()) {
                    static const QChar bullets[] = {QChar(0x2022), QChar(0x25E6),
                                                    QChar(0x25AA)};
                    marker = QString(bullets[depth % 3]);
                }
                content = marker + QLatin1Char(' ') + list.captured(4);
                block.setLeftMargin(18.0 + depth * 22.0);
                block.setTextIndent(-14.0);
            } else if (ruleRe.match(content).hasMatch()) {
                content = QString(28, QChar(0x2500));
                text.setForeground(QColor(0x48, 0x70, 0x5b));
                block.setAlignment(Qt::AlignCenter);
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

        if (parseInline)
            insertInline(cursor, content, text);
        else
            cursor.insertText(content, text);
        attachSourceData(cursor.block(), sourceBlock, lineStart, line.size());
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
