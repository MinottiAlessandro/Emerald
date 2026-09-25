#include "SearchIndex.h"

#include "MarkdownComment.h"
#include "Perf.h"
#include "Vault.h"
#include <QSet>
#include <algorithm>

namespace {
QString searchableContent(QString content) {
    content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    content.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return MarkdownComment::masked(content);
}

QVector<int> prefixPositions(const QString &text, const QString &token) {
    QVector<int> positions;
    int from = 0;
    while (from < text.size()) {
        const int at = text.indexOf(token, from, Qt::CaseInsensitive);
        if (at < 0)
            break;
        if (at == 0 || !text.at(at - 1).isLetterOrNumber())
            positions.append(at);
        from = at + qMax(1, int(token.size()));
    }
    return positions;
}

QString makeSnippet(const QString &line, int position, int length) {
    const int start = qMax(0, position - 40);
    const int end =
        qMin(int(line.size()), qMax(start + 120, position + length));
    return (start > 0 ? QStringLiteral("…") : QString()) +
           line.mid(start, qMin(160, end - start)).simplified() +
           (end < line.size() ? QStringLiteral("…") : QString());
}

template <typename Fn>
void forEachToken(const QString &text, Fn fn) {
    QString token;
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber()) {
            token.append(ch.toLower());
        } else if (!token.isEmpty()) {
            fn(token);
            token.clear();
        }
    }
    if (!token.isEmpty())
        fn(token);
}
} // namespace

QStringList SearchIndex::tokenize(const QString &text) {
    QStringList tokens;
    forEachToken(text, [&](const QString &token) { tokens << token; });
    return tokens;
}

void SearchIndex::indexDoc(int id, const QString &content) {
    Doc &doc = m_docs[id];
    doc.searchContent = qCompress(content.toUtf8(), 1);
    doc.lineStarts = {0};
    QHash<QString, QVector<int>> positions;
    forEachToken(doc.title, [&](const QString &term) { positions[term]; });
    QString token;
    int start = 0;
    for (int i = 0; i <= content.size(); ++i) {
        if (i < content.size() && content.at(i).isLetterOrNumber()) {
            if (token.isEmpty())
                start = i;
            token.append(content.at(i).toLower());
        } else {
            if (!token.isEmpty()) {
                positions[token].append(start);
                token.clear();
            }
            if (i < content.size() && content.at(i) == QLatin1Char('\n'))
                doc.lineStarts.append(i + 1);
        }
    }
    doc.terms = QStringList(positions.keyBegin(), positions.keyEnd());
    for (const QString &term : doc.terms) {
        QVector<Posting> &posting = m_postings[term];
        if (posting.isEmpty())
            m_termsDirty = true;
        posting.append({id, positions.take(term)});
    }
}

void SearchIndex::unindexDoc(int id) {
    const QStringList terms = m_docs[id].terms;
    for (const QString &term : terms) {
        auto it = m_postings.find(term);
        if (it == m_postings.end())
            continue;
        it->erase(std::remove_if(it->begin(), it->end(),
                                 [id](const Posting &p) {
                                     return p.docId == id;
                                 }),
                  it->end());
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
        // Mask comments without shifting source offsets, including the mascot
        // header, so each indexed occurrence maps back to the editor.
        const QString body = searchableContent(vault.read(n.path));
        m_docs.insert(id, Doc{n.path, n.title, {}});
        m_byPath.insert(n.path, id);
        indexDoc(id, body);
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
    const QString body = searchableContent(content);
    if (it != m_byPath.end()) {
        id = it.value();
        unindexDoc(id);
        Doc &doc = m_docs[id];
        doc.title = title;
        doc.terms.clear();
    } else {
        id = m_nextId++;
        m_docs.insert(id, Doc{path, title, {}});
        m_byPath.insert(path, id);
    }
    indexDoc(id, body);
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
    const QString queryText = query.trimmed();
    const QStringList tokens = tokenize(queryText);
    if (tokens.isEmpty())
        return {};
    ensureSortedTerms();

    // Every token is a prefix; retain the existing AND filter over notes.
    struct Hit {
        int position;
        bool exact;
    };
    QVector<QHash<int, QVector<Hit>>> tokenHits;
    QSet<int> candidates;
    bool first = true;
    for (const QString &token : tokens) {
        QSet<int> forToken;
        tokenHits.append(QHash<int, QVector<Hit>>{});
        auto &hits = tokenHits.last();
        auto lo = std::lower_bound(m_sortedTerms.cbegin(), m_sortedTerms.cend(),
                                   token);
        for (auto it = lo; it != m_sortedTerms.cend() && it->startsWith(token);
             ++it) {
            const auto posting = m_postings.constFind(*it);
            if (posting == m_postings.constEnd())
                continue;
            for (const Posting &p : *posting) {
                forToken.insert(p.docId);
                auto &occurrences = hits[p.docId];
                for (int position : p.positions)
                    occurrences.append({position, it->size() == token.size()});
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
    for (int id : candidates) {
        const Doc &doc = m_docs[id];
        int titleTerms = 0;
        for (const QString &token : tokens)
            if (!prefixPositions(doc.title, token).isEmpty())
                ++titleTerms;
        if (titleTerms == tokens.size()) {
            const bool exact =
                doc.title.compare(queryText, Qt::CaseInsensitive) == 0;
            results.append({doc.path, doc.title, {}, exact ? 8000 : 4000});
        }
        QVector<QVector<Hit>> hits;
        for (auto &byDoc : tokenHits) {
            hits.append(byDoc.take(id));
            std::sort(hits.last().begin(), hits.last().end(),
                      [](const Hit &a, const Hit &b) {
                          return a.position < b.position;
                      });
        }
        int anchorToken = 0;
        while (anchorToken < hits.size() && hits.at(anchorToken).isEmpty())
            ++anchorToken;
        if (anchorToken == hits.size())
            continue;
        const QString content =
            QString::fromUtf8(qUncompress(doc.searchContent));
        int previousStart = -1;
        int previousEnd = -1;
        for (const Hit &anchor : hits.at(anchorToken)) {
            const int lineIndex =
                std::upper_bound(doc.lineStarts.cbegin(), doc.lineStarts.cend(),
                                 anchor.position) -
                doc.lineStarts.cbegin() - 1;
            const int lineStart = doc.lineStarts.at(lineIndex);
            const int lineEnd = lineIndex + 1 < doc.lineStarts.size()
                                    ? doc.lineStarts.at(lineIndex + 1) - 1
                                    : content.size();
            int start = anchor.position;
            int end = start + tokens.at(anchorToken).size();
            int matched = 0;
            int exactTerms = 0;
            for (int i = 0; i < hits.size(); ++i) {
                const auto &positions = hits.at(i);
                if (positions.isEmpty())
                    continue;
                auto it = std::lower_bound(positions.cbegin(), positions.cend(),
                                           anchor.position,
                                           [](const Hit &hit, int position) {
                                               return hit.position < position;
                                           });
                const Hit *nearest =
                    it == positions.cend() ? &positions.last() : &*it;
                if (it != positions.cbegin() &&
                    (it - 1)->position >= lineStart &&
                    (nearest->position >= lineEnd ||
                     anchor.position - (it - 1)->position <
                         qAbs(nearest->position - anchor.position)))
                    nearest = &*(it - 1);
                if (nearest->position < lineStart ||
                    nearest->position >= lineEnd)
                    continue;
                start = qMin(start, nearest->position);
                end = qMax(end, nearest->position + int(tokens.at(i).size()));
                ++matched;
                exactTerms += nearest->exact;
            }
            if (start == previousStart && end == previousEnd)
                continue;
            previousStart = start;
            previousEnd = end;
            const bool phrase =
                QStringView(content)
                    .mid(start, end - start)
                    .compare(queryText, Qt::CaseInsensitive) == 0;
            const int score = (phrase ? 5000 : 0) +
                              (matched == tokens.size() ? 2000 : 0) +
                              exactTerms * 50 + titleTerms * 10 +
                              qMax(0, 500 - (end - start));
            results.append(
                {doc.path, doc.title,
                 limit > 0
                     ? QString()
                     : makeSnippet(content.mid(lineStart, lineEnd - lineStart),
                                   start - lineStart, end - start),
                 score, start, end - start, lineIndex + 1});
        }
    }
    const auto better = [](const Result &a, const Result &b) {
        if (a.score != b.score)
            return a.score > b.score;
        const int titleOrder = a.title.compare(b.title, Qt::CaseInsensitive);
        if (titleOrder != 0)
            return titleOrder < 0;
        if (a.path != b.path)
            return a.path < b.path;
        return a.position < b.position;
    };
    if (limit > 0 && results.size() > limit) {
        std::partial_sort(results.begin(), results.begin() + limit,
                          results.end(), better);
        results.resize(limit);
    } else {
        std::sort(results.begin(), results.end(), better);
    }
    if (limit > 0) {
        for (Result &result : results) {
            if (result.position < 0)
                continue;
            const Doc &doc = m_docs[m_byPath.value(result.path)];
            const QString content =
                QString::fromUtf8(qUncompress(doc.searchContent));
            const int start =
                content.lastIndexOf(QLatin1Char('\n'), result.position) + 1;
            int end = content.indexOf(QLatin1Char('\n'), result.position);
            if (end < 0)
                end = content.size();
            result.snippet =
                makeSnippet(content.mid(start, end - start),
                            result.position - start, result.length);
        }
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
