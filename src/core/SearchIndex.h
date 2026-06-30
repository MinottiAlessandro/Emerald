#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

class Vault;

// Fast in-memory full-text search.
//
// Keeps an inverted index (word -> note ids + frequency). A query is split into
// word prefixes; a note matches when it contains every prefix (AND). Candidates
// come straight from the index, so a query only touches the postings for its
// words — not the whole corpus — which keeps it fast as the vault grows.
class SearchIndex {
public:
    struct Result {
        QString path;
        QString title;
        QString snippet; // a line of context around the first match
        int score = 0;
    };

    void rebuild(const Vault &vault);
    void clear();
    void updateNote(const QString &path, const QString &title,
                    const QString &content);
    void removeNote(const QString &path);
    void renamePath(const QString &oldPath, const QString &newPath,
                    const QString &newTitle = QString());

    QList<Result> search(const QString &query, int limit = 50) const;

    // Match only against note titles (for a quick "go to note" picker). A note
    // matches when its title contains every query token; shorter titles and
    // prefix matches rank higher.
    QList<Result> searchTitles(const QString &query, int limit = 50) const;

    // Lowercased word tokens (split on non-letter/non-digit runs).
    static QStringList tokenize(const QString &text);

private:
    struct Doc {
        QString path;
        QString title;
        QStringList terms; // unique terms, kept so the doc can be un-indexed
    };
    struct Posting {
        int docId = 0;
        int frequency = 0;
    };

    void indexDoc(int id, const QString &content);
    void unindexDoc(int id);
    void ensureSortedTerms() const;

    QHash<int, Doc> m_docs;
    QHash<QString, int> m_byPath;         // path -> doc id
    QHash<QString, QVector<Posting>> m_postings; // term -> doc ids + frequency
    int m_nextId = 0;

    mutable QStringList m_sortedTerms; // vocabulary, sorted, for prefix lookup
    mutable bool m_termsDirty = true;
};
