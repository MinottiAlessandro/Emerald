#include "SearchIndex.h"

#include "Vault.h"
#include <QRegularExpression>
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
    const QSet<QString> unique(all.begin(), all.end());
    doc.terms = QStringList(unique.begin(), unique.end());
    for (const QString &term : doc.terms) {
        QSet<int> &posting = m_postings[term];
        if (posting.isEmpty())
            m_termsDirty = true; // a new word entered the vocabulary
        posting.insert(id);
    }
}

void SearchIndex::unindexDoc(int id) {
    const QStringList terms = m_docs[id].terms;
    for (const QString &term : terms) {
        auto it = m_postings.find(term);
        if (it == m_postings.end())
            continue;
        it->remove(id);
        if (it->isEmpty()) {
            m_postings.erase(it);
            m_termsDirty = true;
        }
    }
}

void SearchIndex::rebuild(const Vault &vault) {
    m_docs.clear();
    m_byPath.clear();
    m_postings.clear();
    m_nextId = 0;
    m_termsDirty = true;
    for (const Note &n : vault.notes()) {
        const int id = m_nextId++;
        m_docs.insert(id, Doc{n.path, n.title, vault.read(n.path), {}});
        m_byPath.insert(n.path, id);
        indexDoc(id);
    }
}

void SearchIndex::updateNote(const QString &path, const QString &title,
                             const QString &content) {
    auto it = m_byPath.find(path);
    int id;
    if (it != m_byPath.end()) {
        id = it.value();
        unindexDoc(id);
        Doc &doc = m_docs[id];
        doc.title = title;
        doc.content = content;
        doc.terms.clear();
    } else {
        id = m_nextId++;
        m_docs.insert(id, Doc{path, title, content, {}});
        m_byPath.insert(path, id);
    }
    indexDoc(id);
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
    const QStringList tokens = tokenize(query);
    if (tokens.isEmpty())
        return {};
    ensureSortedTerms();

    // Every token is treated as a prefix; a note must match all of them.
    QSet<int> candidates;
    bool first = true;
    for (const QString &token : tokens) {
        QSet<int> forToken;
        auto lo = std::lower_bound(m_sortedTerms.cbegin(), m_sortedTerms.cend(),
                                   token);
        for (auto it = lo; it != m_sortedTerms.cend() && it->startsWith(token);
             ++it)
            forToken.unite(m_postings.value(*it));
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
        int score = 0;
        for (const QString &token : tokens) {
            if (doc.title.contains(token, Qt::CaseInsensitive))
                score += 100; // title hits rank first
            int from = 0;
            while ((from = doc.content.indexOf(token, from, Qt::CaseInsensitive)) !=
                   -1) {
                ++score;
                from += token.length();
            }
        }
        results.append({doc.path, doc.title, makeSnippet(doc.content, tokens.first()),
                        score});
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

QList<SearchIndex::Result> SearchIndex::searchTitles(const QString &query,
                                                     int limit) const {
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
