#include "MarkdownImage.h"

#include "MarkdownComment.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace MarkdownImage {
namespace {

bool isEscaped(const QString &text, int position) {
    int slashes = 0;
    for (int i = position - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
        ++slashes;
    return (slashes % 2) != 0;
}

QString unescape(QString text) {
    QString result;
    result.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\\') && i + 1 < text.size())
            result += text.at(++i);
        else
            result += text.at(i);
    }
    return result;
}

QString decodedTarget(const QString &raw) {
    return QUrl::fromPercentEncoding(unescape(raw).toUtf8());
}

int closingBracket(const QString &text, int opening) {
    int depth = 0;
    for (int i = opening + 1; i < text.size(); ++i) {
        if (isEscaped(text, i))
            continue;
        if (text.at(i) == QLatin1Char('[')) {
            ++depth;
        } else if (text.at(i) == QLatin1Char(']')) {
            if (depth == 0)
                return i;
            --depth;
        }
    }
    return -1;
}

int closingReferenceBracket(const QString &text, int opening) {
    for (int i = opening + 1; i < text.size(); ++i) {
        if (isEscaped(text, i))
            continue;
        if (text.at(i) == QLatin1Char('['))
            return -1;
        if (text.at(i) == QLatin1Char(']'))
            return i;
    }
    return -1;
}

void skipSpace(const QString &text, int *position) {
    while (*position < text.size() && text.at(*position).isSpace())
        ++*position;
}

bool parseTitle(const QString &text, int *position, QString *title) {
    if (*position >= text.size())
        return false;
    const QChar opening = text.at(*position);
    QChar closing;
    if (opening == QLatin1Char('"'))
        closing = QLatin1Char('"');
    else if (opening == QLatin1Char('\''))
        closing = QLatin1Char('\'');
    else if (opening == QLatin1Char('('))
        closing = QLatin1Char(')');
    else
        return false;

    const int contentStart = ++*position;
    while (*position < text.size()) {
        if (text.at(*position) == closing && !isEscaped(text, *position)) {
            *title = unescape(text.mid(contentStart, *position - contentStart));
            ++*position;
            return true;
        }
        ++*position;
    }
    return false;
}

struct Destination {
    bool valid = false;
    QString target;
    QString title;
    int end = 0;
};

Destination parseInlineDestination(const QString &text, int position) {
    Destination result;
    skipSpace(text, &position);
    QString rawTarget;
    if (position < text.size() && text.at(position) == QLatin1Char('<')) {
        const int start = ++position;
        while (position < text.size() &&
               (text.at(position) != QLatin1Char('>') ||
                isEscaped(text, position)))
            ++position;
        if (position >= text.size())
            return result;
        rawTarget = text.mid(start, position - start);
        ++position;
    } else {
        const int start = position;
        int depth = 0;
        while (position < text.size()) {
            const QChar character = text.at(position);
            if (character == QLatin1Char('\\') && position + 1 < text.size()) {
                position += 2;
                continue;
            }
            if (character.isSpace())
                break;
            if (character == QLatin1Char('(')) {
                if (++depth > 32)
                    return result;
            } else if (character == QLatin1Char(')')) {
                if (depth == 0)
                    break;
                --depth;
            }
            ++position;
        }
        if (depth != 0)
            return result;
        rawTarget = text.mid(start, position - start);
    }

    const int afterTarget = position;
    skipSpace(text, &position);
    QString title;
    if (position < text.size() && text.at(position) != QLatin1Char(')')) {
        // CommonMark requires whitespace between the destination and title.
        if (position == afterTarget || !parseTitle(text, &position, &title))
            return result;
        skipSpace(text, &position);
    }
    if (position >= text.size() || text.at(position) != QLatin1Char(')'))
        return result;

    result.valid = true;
    result.target = decodedTarget(rawTarget);
    result.title = title;
    result.end = position + 1;
    return result;
}

Destination parseDefinitionDestination(const QString &text, int position) {
    Destination result;
    skipSpace(text, &position);
    if (position >= text.size())
        return result;

    QString rawTarget;
    if (text.at(position) == QLatin1Char('<')) {
        const int start = ++position;
        while (position < text.size() &&
               (text.at(position) != QLatin1Char('>') ||
                isEscaped(text, position)))
            ++position;
        if (position >= text.size())
            return result;
        rawTarget = text.mid(start, position - start);
        ++position;
    } else {
        const int start = position;
        int depth = 0;
        while (position < text.size() && !text.at(position).isSpace()) {
            if (text.at(position) == QLatin1Char('\\') &&
                position + 1 < text.size()) {
                position += 2;
                continue;
            }
            if (text.at(position) == QLatin1Char('(')) {
                if (++depth > 32)
                    return result;
            } else if (text.at(position) == QLatin1Char(')')) {
                if (depth == 0)
                    return result;
                --depth;
            }
            ++position;
        }
        if (position == start || depth != 0)
            return result;
        rawTarget = text.mid(start, position - start);
    }

    const int afterTarget = position;
    skipSpace(text, &position);
    QString title;
    if (position < text.size()) {
        if (position == afterTarget || !parseTitle(text, &position, &title))
            return result;
        skipSpace(text, &position);
    }
    if (position != text.size())
        return result;

    result.valid = true;
    result.target = decodedTarget(rawTarget);
    result.title = title;
    result.end = position;
    return result;
}

Dimensions takeDimensions(QString *description, bool allowBareWidth) {
    Dimensions dimensions;
    static const QRegularExpression size(
        QStringLiteral("^(\\d{1,5})(?:\\s*[xX]\\s*(\\d{1,5}))?$"));
    QString candidate;
    int separator = description->lastIndexOf(QLatin1Char('|'));
    if (separator >= 0)
        candidate = description->mid(separator + 1).trimmed();
    else if (allowBareWidth)
        candidate = description->trimmed();
    const auto match = size.match(candidate);
    if (!match.hasMatch())
        return dimensions;

    const int width = match.captured(1).toInt();
    const int height = match.captured(2).toInt();
    if (width <= 0 || width > 32768 || height > 32768)
        return dimensions;
    dimensions.width = width;
    dimensions.height = height;
    if (separator >= 0)
        *description = description->left(separator);
    else
        description->clear();
    return dimensions;
}

QString plainDescription(QString description) {
    description = unescape(description.trimmed());
    // Image descriptions are accessibility text. Preserve words while
    // discarding the small inline-markup subset Emerald renders.
    static const QRegularExpression nestedLink(
        QStringLiteral("!?\\[([^]\\n]*)\\]\\((?:<[^>\\n]*>|[^)\\n]*)\\)"));
    for (;;) {
        const auto match = nestedLink.match(description);
        if (!match.hasMatch())
            break;
        description.replace(match.capturedStart(), match.capturedLength(),
                            match.captured(1));
    }
    description.remove(QRegularExpression(QStringLiteral("[`*_~=]")));
    return description.simplified();
}

Image obsidianImageAt(const QString &line, int start) {
    Image image;
    if (line.mid(start, 3) != QStringLiteral("![[") ||
        isEscaped(line, start))
        return image;
    int end = start + 3;
    while (end + 1 < line.size() && line.mid(end, 2) != QStringLiteral("]]"))
        ++end;
    if (end + 1 >= line.size())
        return image;

    const QString rawInside = line.mid(start + 3, end - start - 3);
    int insideStart = 0;
    while (insideStart < rawInside.size() &&
           rawInside.at(insideStart).isSpace())
        ++insideStart;
    int insideEnd = rawInside.size();
    while (insideEnd > insideStart && rawInside.at(insideEnd - 1).isSpace())
        --insideEnd;
    const QString inside =
        rawInside.mid(insideStart, insideEnd - insideStart);
    QString target = inside;
    Dimensions dimensions = takeDimensions(&target, false);
    target = target.trimmed();
    const int rawTargetLength = target.size();
    const int fragment = target.indexOf(QLatin1Char('#'));
    if (fragment >= 0)
        target = target.left(fragment).trimmed();
    target = decodedTarget(target);
    if (!isLikelyImageTarget(target))
        return image;

    image.valid = true;
    image.resolved = true;
    image.syntax = Syntax::ObsidianEmbed;
    image.start = start;
    image.length = end + 2 - start;
    image.descriptionStart = start + 3 + insideStart;
    image.descriptionLength = qMax(0, rawTargetLength);
    image.description = QFileInfo(target).completeBaseName();
    image.target = target;
    image.dimensions = dimensions;
    return image;
}

bool parseReferenceDefinition(const QString &line, QString *label,
                              Reference *reference, bool *needsTitle) {
    int position = 0;
    while (position < line.size() && position < 4 &&
           line.at(position) == QLatin1Char(' '))
        ++position;
    if (position > 3 || position >= line.size() ||
        line.at(position) != QLatin1Char('[') ||
        (position > 0 && line.at(position - 1) == QLatin1Char('!')))
        return false;
    const int close = closingReferenceBracket(line, position);
    if (close < 0 || close + 1 >= line.size() ||
        line.at(close + 1) != QLatin1Char(':'))
        return false;
    const QString rawLabel = line.mid(position + 1, close - position - 1);
    if (rawLabel.isEmpty() || rawLabel.size() > 999)
        return false;
    const Destination destination =
        parseDefinitionDestination(line, close + 2);
    if (!destination.valid)
        return false;
    *label = normalizeReferenceLabel(rawLabel);
    reference->target = destination.target;
    reference->title = destination.title;
    *needsTitle = destination.title.isEmpty();
    return !label->isEmpty();
}

bool continuationTitle(const QString &line, QString *title) {
    int position = 0;
    while (position < line.size() && position < 4 &&
           line.at(position) == QLatin1Char(' '))
        ++position;
    if (position > 3)
        return false;
    if (!parseTitle(line, &position, title))
        return false;
    skipSpace(line, &position);
    return position == line.size();
}

} // namespace

QString normalizeReferenceLabel(const QString &label) {
    return unescape(label).simplified().toCaseFolded();
}

References collectReferences(const QString &source,
                             QSet<int> *definitionLines) {
    if (definitionLines)
        definitionLines->clear();
    References result;
    const QString masked = MarkdownComment::masked(
        source, MarkdownComment::ranges(source));
    const QStringList lines = masked.split(QLatin1Char('\n'),
                                           Qt::KeepEmptyParts);
    static const QRegularExpression fenceRe(
        QStringLiteral("^ {0,3}(`{3,}|~{3,})(.*)$"));
    bool insideFence = false;
    QChar fenceCharacter;
    int fenceLength = 0;
    for (int lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        const QString &line = lines.at(lineNumber);
        const auto fence = fenceRe.match(line);
        if (fence.hasMatch()) {
            const QString marker = fence.captured(1);
            if (!insideFence) {
                insideFence = true;
                fenceCharacter = marker.front();
                fenceLength = marker.size();
            } else if (marker.front() == fenceCharacter &&
                       marker.size() >= fenceLength &&
                       fence.captured(2).trimmed().isEmpty()) {
                insideFence = false;
            }
            continue;
        }
        if (insideFence)
            continue;

        QString label;
        Reference reference;
        bool needsTitle = false;
        if (!parseReferenceDefinition(line, &label, &reference, &needsTitle))
            continue;
        const int definitionLine = lineNumber;
        bool usedContinuation = false;
        if (needsTitle && lineNumber + 1 < lines.size()) {
            QString title;
            if (continuationTitle(lines.at(lineNumber + 1), &title)) {
                reference.title = title;
                usedContinuation = true;
                ++lineNumber;
            }
        }
        if (!result.contains(label))
            result.insert(label, reference); // first definition wins
        if (definitionLines) {
            definitionLines->insert(definitionLine);
            if (usedContinuation)
                definitionLines->insert(definitionLine + 1);
        }
    }
    return result;
}

Image imageAt(const QString &line, int start, const References &references,
              bool includeUnresolvedReferences) {
    Image image = obsidianImageAt(line, start);
    if (image.valid)
        return image;
    if (start < 0 || start + 1 >= line.size() ||
        line.mid(start, 2) != QStringLiteral("![") || isEscaped(line, start))
        return {};

    const int descriptionEnd = closingBracket(line, start + 1);
    if (descriptionEnd < 0)
        return {};
    const QString rawDescription =
        line.mid(start + 2, descriptionEnd - start - 2);
    QString displayDescription = rawDescription;
    Dimensions dimensions = takeDimensions(&displayDescription, true);

    image.valid = true;
    image.start = start;
    image.descriptionStart = start + 2;
    image.descriptionLength = displayDescription.size();
    image.description = plainDescription(displayDescription);
    image.dimensions = dimensions;

    int position = descriptionEnd + 1;
    if (position < line.size() && line.at(position) == QLatin1Char('(')) {
        const Destination destination =
            parseInlineDestination(line, position + 1);
        if (!destination.valid)
            return {};
        image.resolved = true;
        image.syntax = Syntax::Inline;
        image.target = destination.target;
        image.title = destination.title;
        image.length = destination.end - start;
        return image;
    }

    QString referenceLabel;
    int end = position;
    Syntax syntax = Syntax::ShortcutReference;
    if (position < line.size() && line.at(position) == QLatin1Char('[')) {
        const int referenceEnd = closingReferenceBracket(line, position);
        if (referenceEnd < 0)
            return {};
        referenceLabel = line.mid(position + 1, referenceEnd - position - 1);
        if (referenceLabel.isEmpty()) {
            referenceLabel = displayDescription;
            syntax = Syntax::CollapsedReference;
        } else {
            syntax = Syntax::FullReference;
        }
        end = referenceEnd + 1;
    } else {
        referenceLabel = displayDescription;
    }

    const auto reference = references.constFind(
        normalizeReferenceLabel(referenceLabel));
    image.syntax = syntax;
    image.length = end - start;
    if (reference != references.constEnd()) {
        image.resolved = true;
        image.target = reference->target;
        image.title = reference->title;
        return image;
    }
    return includeUnresolvedReferences ? image : Image{};
}

QVector<Image> imagesInLine(const QString &line, const References &references,
                            bool includeUnresolvedReferences) {
    QVector<Image> images;
    for (int position = 0; position < line.size();) {
        if (line.at(position) == QLatin1Char('`') &&
            !isEscaped(line, position)) {
            int ticks = 1;
            while (position + ticks < line.size() &&
                   line.at(position + ticks) == QLatin1Char('`'))
                ++ticks;
            const QString delimiter(ticks, QLatin1Char('`'));
            const int closing = line.indexOf(delimiter, position + ticks);
            if (closing >= 0) {
                position = closing + ticks;
                continue;
            }
        }
        if (line.at(position) != QLatin1Char('!')) {
            ++position;
            continue;
        }
        const Image image = imageAt(line, position, references,
                                    includeUnresolvedReferences);
        if (!image.valid) {
            ++position;
            continue;
        }
        images.append(image);
        position += qMax(1, image.length);
    }
    return images;
}

Image standaloneImage(const QString &line, const References &references) {
    int first = 0;
    while (first < line.size() && line.at(first).isSpace())
        ++first;
    int last = line.size();
    while (last > first && line.at(last - 1).isSpace())
        --last;
    const Image image = imageAt(line, first, references);
    return image.valid && image.resolved && image.start == first &&
                   image.start + image.length == last
               ? image
               : Image{};
}

bool isLikelyImageTarget(const QString &target) {
    QString path = target;
    const int fragment = path.indexOf(QLatin1Char('#'));
    if (fragment >= 0)
        path.truncate(fragment);
    const QString suffix = QFileInfo(path).suffix().toCaseFolded();
    static const QSet<QString> suffixes{
        QStringLiteral("png"),  QStringLiteral("jpg"),
        QStringLiteral("jpeg"), QStringLiteral("gif"),
        QStringLiteral("bmp"),  QStringLiteral("webp"),
        QStringLiteral("svg"),  QStringLiteral("svgz"),
        QStringLiteral("tif"),  QStringLiteral("tiff"),
        QStringLiteral("ico"),  QStringLiteral("avif"),
        QStringLiteral("heic"), QStringLiteral("heif")};
    return suffixes.contains(suffix);
}

} // namespace MarkdownImage
