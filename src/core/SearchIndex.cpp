#include "SearchIndex.h"

#include "MascotSeed.h"
#include "Perf.h"
#include "Vault.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {
QString makeSnippet(const QString &content, const QString &token) {
    int pos = content.indexOf(token, 0, Qt::CaseInsensitive);
    if (pos < 0)
        return content.left(120).simplified();
    const int start = content.lastIndexOf(QLatin1Char('\n'), pos) + 1;
    int end = content.indexOf(QLatin1Char('\n'), pos);
    if (end < 0)
        end = content.length();
    QString line = content.mid(start, end - start).simplified();
    if (line.length() > 120)
        line = line.left(117) + QStringLiteral("…");
    return line;
}
} // namespace

QStringList SearchIndex::tokenize(const QString &text) {
    static const QRegularExpression sep(QStringLiteral("[^\\p{L}\\p{N}]+"));
    return text.toLower().split(sep, Qt::SkipEmptyParts);
}

void SearchIndex::indexDoc(int id) {
    Doc &doc = m_docs[id];
    const QStringList all = tokenize(doc.title + QLatin1Char(' ') + doc.content);
    doc.termFreq.clear();
    for (const QString &term : all)
        ++doc.termFreq[term];
    doc.terms = QStringList(doc.termFreq.keyBegin(), doc.termFreq.keyEnd());
    for (const QString &term : doc.terms) {
        QVector<int> &posting = m_postings[term];
        if (posting.isEmpty())
            m_termsDirty = true; // a new word entered the vocabulary
        posting.append(id);
    }
}

void SearchIndex::unindexDoc(int id) {
    const QStringList terms = m_docs[id].terms;
    for (const QString &term : terms) {
        auto it = m_postings.find(term);
        if (it == m_postings.end())
            continue;
        it->removeAll(id);
        if (it->isEmpty()) {
            m_postings.erase(it);
            m_termsDirty = true;
        }
    }
}

void SearchIndex::rebuild(const Vault &vault) {
    EMERALD_PROFILE_SCOPE("SearchIndex::rebuild");
    clear();
    for (const Note &n : vault.notes()) {
        const int id = m_nextId++;
        // Drop a leading mascot header line so it doesn't pollute the index.
        m_docs.insert(id, Doc{n.path, n.title,
                              MascotSeed::strip(vault.read(n.path)), {}, {}});
        m_byPath.insert(n.path, id);
        indexDoc(id);
    }
}

void SearchIndex::clear() {
    m_docs.clear();
    m_byPath.clear();
    m_postings.clear();
    m_nextId = 0;
    m_termsDirty = true;
    m_sortedTerms.clear();
}

void SearchIndex::updateNote(const QString &path, const QString &title,
                             const QString &content) {
    auto it = m_byPath.find(path);
    int id;
    const QString body = MascotSeed::strip(content); // ignore the header line
    if (it != m_byPath.end()) {
        id = it.value();
        unindexDoc(id);
        Doc &doc = m_docs[id];
        doc.title = title;
        doc.content = body;
        doc.terms.clear();
        doc.termFreq.clear();
    } else {
        id = m_nextId++;
        m_docs.insert(id, Doc{path, title, body, {}, {}});
        m_byPath.insert(path, id);
    }
    indexDoc(id);
}

void SearchIndex::removeNote(const QString &path) {
    auto it = m_byPath.find(path);
    if (it == m_byPath.end())
        return;
    const int id = it.value();
    unindexDoc(id);
    m_byPath.erase(it);
    m_docs.remove(id);
}

void SearchIndex::renamePath(const QString &oldPath, const QString &newPath,
                             const QString &newTitle) {
    if (oldPath == newPath && newTitle.isEmpty())
        return;
    auto it = m_byPath.find(oldPath);
    if (it == m_byPath.end())
        return;
    const int id = it.value();
    m_byPath.erase(it);
    m_byPath.insert(newPath, id);
    Doc &doc = m_docs[id];
    doc.path = newPath;
    if (!newTitle.isEmpty())
        doc.title = newTitle;
}

void SearchIndex::ensureSortedTerms() const {
    if (!m_termsDirty)
        return;
    m_sortedTerms = m_postings.keys();
    std::sort(m_sortedTerms.begin(), m_sortedTerms.end());
    m_termsDirty = false;
}

QList<SearchIndex::Result> SearchIndex::search(const QString &query,
                                               int limit) const {
    EMERALD_PROFILE_SCOPE("SearchIndex::search");
    const QStringList tokens = tokenize(query);
    if (tokens.isEmpty())
        return {};
    ensureSortedTerms();

    // Every token is treated as a prefix; a note must match all of them.
    QSet<int> candidates;
    QHash<int, int> frequencyScore;
    bool first = true;
    for (const QString &token : tokens) {
        QSet<int> forToken;
        auto lo = std::lower_bound(m_sortedTerms.cbegin(), m_sortedTerms.cend(),
                                   token);
        for (auto it = lo; it != m_sortedTerms.cend() && it->startsWith(token);
             ++it) {
            const QString term = *it;
            const auto posting = m_postings.constFind(term);
            if (posting == m_postings.constEnd())
                continue;
            for (int id : *posting) {
                forToken.insert(id);
                const auto doc = m_docs.constFind(id);
                if (doc != m_docs.constEnd())
                    frequencyScore[id] += doc->termFreq.value(term);
            }
        }
        if (first) {
            candidates = forToken;
            first = false;
        } else {
            candidates.intersect(forToken);
        }
        if (candidates.isEmpty())
            return {};
    }

    QList<Result> results;
    results.reserve(candidates.size());
    for (int id : candidates) {
        const Doc &doc = m_docs[id];
        int score = frequencyScore.value(id);
        for (const QString &token : tokens) {
            if (doc.title.contains(token, Qt::CaseInsensitive))
                score += 100; // title hits rank first
        }
        results.append({doc.path, doc.title, QString(), score});
    }
    std::sort(results.begin(), results.end(),
              [](const Result &a, const Result &b) {
                  if (a.score != b.score)
                      return a.score > b.score;
                  return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
              });
    if (results.size() > limit)
        results.erase(results.begin() + limit, results.end());
    for (Result &r : results) {
        const auto it = m_byPath.constFind(r.path);
        if (it != m_byPath.constEnd())
            r.snippet = makeSnippet(m_docs[*it].content, tokens.first());
    }
    return results;
}

QList<SearchIndex::Result> SearchIndex::searchTitles(const QString &query,
                                                     int limit) const {
    EMERALD_PROFILE_SCOPE("SearchIndex::searchTitles");
    const QStringList tokens = tokenize(query);
    if (tokens.isEmpty())
        return {};

    QList<Result> results;
    for (auto it = m_docs.cbegin(); it != m_docs.cend(); ++it) {
        const QString title = it->title.toLower();
        bool all = true;
        int score = 0;
        for (const QString &token : tokens) {
            const int at = title.indexOf(token);
            if (at < 0) {
                all = false;
                break;
            }
            if (at == 0)
                score += 50; // prefix match ranks higher
        }
        if (!all)
            continue;
        score -= title.length(); // shorter (closer) titles first
        results.append({it->path, it->title, QString(), score});
    }
    std::sort(results.begin(), results.end(),
              [](const Result &a, const Result &b) {
                  if (a.score != b.score)
                      return a.score > b.score;
                  return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
              });
    if (results.size() > limit)
        results.erase(results.begin() + limit, results.end());
    return results;
}
