#include "LinkIndex.h"

#include "Vault.h"
#include "WikiLink.h"
#include <QRegularExpression>
#include <algorithm>

QStringList LinkIndex::parseTargets(const QString &content) {
    QStringList targets;
    auto it = WikiLink::pattern().globalMatch(content);
    while (it.hasNext()) {
        const QString target = WikiLink::cleanTarget(it.next().captured(1));
        if (!target.isEmpty())
            targets << target;
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
    m_real.clear();

    for (const Note &n : vault.notes()) {
        const QString lower = n.title.toLower();
        m_display.insert(lower, n.title);
        m_real.insert(lower);
    }
    for (const Note &n : vault.notes())
        updateNote(n.title, vault.read(n.path));
}

void LinkIndex::updateNote(const QString &title, const QString &content) {
    const QString lowerSource = title.toLower();
    m_display.insert(lowerSource, title);
    m_real.insert(lowerSource); // a note we have content for is a real file
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

LinkIndex::Graph LinkIndex::graph() const {
    Graph g;
    for (auto it = m_display.constBegin(); it != m_display.constEnd(); ++it)
        g.nodes.append({it.value(), m_real.contains(it.key())});
    for (auto it = m_forward.constBegin(); it != m_forward.constEnd(); ++it) {
        const QString source = m_display.value(it.key(), it.key());
        for (const QString &target : it.value())
            g.edges.append({source, m_display.value(target, target)});
    }
    return g;
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
