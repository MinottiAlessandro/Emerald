#include "WikiLink.h"

#include <QRegularExpression>

namespace WikiLink {

const QRegularExpression &pattern() {
    static const QRegularExpression re(QStringLiteral("\\[\\[([^\\[\\]]+)\\]\\]"));
    return re;
}

QString cleanTarget(const QString &inner) {
    return inner.section(QLatin1Char('|'), 0, 0)
        .section(QLatin1Char('#'), 0, 0)
        .trimmed();
}

} // namespace WikiLink
