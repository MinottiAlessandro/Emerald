#include "LinkIndex.h"

#include "Vault.h"
#include <QRegularExpression>
#include <algorithm>

QStringList LinkIndex::parseTargets(const QString &content) {
    static const QRegularExpression re(QStringLiteral("\\[\\[([^\\[\\]]+)\\]\\]"));
    QStringList targets;
    auto it = re.globalMatch(content);
    while (it.hasNext()) {
        QString inner = it.next().captured(1);
        // Drop alias ("Foo|bar") and heading ("Foo#sec") parts.
        inner = inner.section(QLatin1Char('|'), 0, 0)
                    .section(QLatin1Char('#'), 0, 0)
                    .trimmed();
        if (!inner.isEmpty())
            targets << inner;
    }
    return targets;
}

void LinkIndex::removeForward(const QString &lowerSource) {
    const auto old = m_forward.value(lowerSource);
    for (const QString &t : old)
        m_back[t].remove(lowerSource);
    m_forward.remove(lowerSource);
}

void LinkIndex::rebuild(const Vault &vault) {
    m_forward.clear();
    m_back.clear();
    m_display.clear();

    for (const Note &n : vault.notes()) {
        const QString lower = n.title.toLower();
        m_display.insert(lower, n.title);
    }
    for (const Note &n : vault.notes())
        updateNote(n.title, vault.read(n.path));
}

void LinkIndex::updateNote(const QString &title, const QString &content) {
    const QString lowerSource = title.toLower();
    m_display.insert(lowerSource, title);
    removeForward(lowerSource);

    QSet<QString> targets;
    for (const QString &t : parseTargets(content)) {
        const QString lowerT = t.toLower();
        targets.insert(lowerT);
        m_back[lowerT].insert(lowerSource);
        if (!m_display.contains(lowerT))
            m_display.insert(lowerT, t); // unresolved target keeps its casing
    }
    if (!targets.isEmpty())
        m_forward.insert(lowerSource, targets);
}

QStringList LinkIndex::backlinks(const QString &title) const {
    const QString lowerT = title.toLower();
    QStringList result;
    for (const QString &source : m_back.value(lowerT))
        result << m_display.value(source, source);
    std::sort(result.begin(), result.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    return result;
}
