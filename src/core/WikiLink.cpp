#include "WikiLink.h"

#include "MarkdownComment.h"

#include <QRegularExpression>

namespace {
QList<WikiLink::Heading> headingOccurrences(const QString &markdown) {
    QList<WikiLink::Heading> result;
    const QString visible = MarkdownComment::masked(markdown);
    static const QRegularExpression fenceRe(
        QStringLiteral("^\\s*(`{3,}|~{3,})\\s*(\\S*).*$"));
    static const QRegularExpression headingRe(
        QStringLiteral("^\\s{0,3}(#{1,6})\\s+(.+)$"));
    static const QRegularExpression closingHashes(
        QStringLiteral("\\s+#+\\s*$"));
    bool insideFence = false;
    QChar fenceCharacter;
    int fenceLength = 0;
    int lineStart = 0;
    while (lineStart <= visible.size()) {
        int lineEnd = visible.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0)
            lineEnd = visible.size();
        const QString line = visible.mid(lineStart, lineEnd - lineStart);
        const QRegularExpressionMatch fence = fenceRe.match(line);
        if (fence.hasMatch()) {
            const QString marker = fence.captured(1);
            if (!insideFence) {
                insideFence = true;
                fenceCharacter = marker.front();
                fenceLength = marker.size();
            } else if (marker.front() == fenceCharacter &&
                       marker.size() >= fenceLength &&
                       fence.captured(2).isEmpty()) {
                insideFence = false;
                fenceCharacter = QChar();
                fenceLength = 0;
            }
        } else if (!insideFence) {
            const QRegularExpressionMatch match = headingRe.match(line);
            if (match.hasMatch()) {
                QString text = match.captured(2).trimmed();
                text.remove(closingHashes);
                text = text.trimmed();
                if (!text.isEmpty())
                    result.append({text, int(match.capturedLength(1)),
                                   lineStart + int(match.capturedStart(2))});
            }
        }
        if (lineEnd == visible.size())
            break;
        lineStart = lineEnd + 1;
    }
    return result;
}
} // namespace

namespace WikiLink {

const QRegularExpression &pattern() {
    static const QRegularExpression re(QStringLiteral("\\[\\[([^\\[\\]]+)\\]\\]"));
    return re;
}

QString cleanTarget(const QString &inner) {
    return cleanDestination(inner).section(QLatin1Char('#'), 0, 0).trimmed();
}

QString cleanDestination(const QString &inner) {
    return inner.section(QLatin1Char('|'), 0, 0).trimmed();
}

QString heading(const QString &inner) {
    const QString destination = cleanDestination(inner);
    const int separator = destination.indexOf(QLatin1Char('#'));
    return separator >= 0 ? destination.mid(separator + 1).trimmed()
                          : QString();
}

QString displayText(const QString &inner) {
    const QString destination = cleanDestination(inner);
    const int pipe = inner.indexOf(QLatin1Char('|'));
    if (pipe >= 0)
        return inner.mid(pipe + 1).trimmed();
    const QString target = cleanTarget(destination);
    return target.isEmpty() ? heading(destination) : target;
}

QStringList headings(const QString &markdown) {
    QStringList result;
    for (const Heading &occurrence : headingOccurrences(markdown))
        if (!result.contains(occurrence.text, Qt::CaseInsensitive))
            result.append(occurrence.text);
    return result;
}

QList<Heading> headingOutline(const QString &markdown) {
    return headingOccurrences(markdown);
}

int headingPosition(const QString &markdown, const QString &headingTarget) {
    const QString wanted = headingTarget.trimmed().normalized(
        QString::NormalizationForm_C);
    if (wanted.isEmpty())
        return -1;

    for (const Heading &occurrence : headingOccurrences(markdown))
        if (occurrence.text.normalized(QString::NormalizationForm_C)
                .compare(wanted, Qt::CaseInsensitive) == 0)
            return occurrence.position;
    return -1;
}

} // namespace WikiLink
