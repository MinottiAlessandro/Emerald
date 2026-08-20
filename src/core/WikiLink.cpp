#include "WikiLink.h"

#include "MarkdownComment.h"

#include <QRegularExpression>

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

int headingPosition(const QString &markdown, const QString &headingTarget) {
    const QString wanted = headingTarget.trimmed().normalized(
        QString::NormalizationForm_C);
    if (wanted.isEmpty())
        return -1;

    const QString visible = MarkdownComment::masked(markdown);
    static const QRegularExpression fenceRe(
        QStringLiteral("^\\s*(`{3,}|~{3,})\\s*(\\S*).*$"));
    static const QRegularExpression headingRe(
        QStringLiteral("^(#{1,6})\\s+(.+)$"));
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
            if (match.hasMatch() &&
                match.captured(2)
                        .trimmed()
                        .normalized(QString::NormalizationForm_C)
                        .compare(wanted, Qt::CaseInsensitive) == 0)
                return lineStart + match.capturedStart(2);
        }
        if (lineEnd == visible.size())
            break;
        lineStart = lineEnd + 1;
    }
    return -1;
}

} // namespace WikiLink
