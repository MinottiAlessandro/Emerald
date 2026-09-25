#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

class Vault;

// Fast in-memory full-text search.
//
// Keeps an inverted index (word -> note ids + source positions). A query is split into
// word prefixes; a note matches when it contains every prefix (AND). Candidates
// come straight from the index, so a query only touches the postings for its
// words — not the whole corpus — which keeps it fast as the vault grows.
// Author-only HTML comments are masked before content enters the index.
class SearchIndex {
public:
    struct Result {
        QString path;
        QString title;
        QString snippet; // context around this occurrence
        int score = 0;
        int position = -1; // UTF-16 source offset; -1 denotes a title match
        int length = 0;
        int line = 0; // one-based source line; 0 for title matches
    };

    void rebuild(const Vault &vault);
    void clear();
    void updateNote(const QString &path, const QString &title,
                    const QString &content);
    void removeNote(const QString &path);
    void renamePath(const QString &oldPath, const QString &newPath,
                    const QString &newTitle = QString());

    // Individual occurrences ranked across the vault. A non-positive limit
    // returns every match; callers can display the list incrementally.
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
        // Compressed, comment-masked source retains exact editor offsets and
        // avoids rereading every candidate file on each query. Decompress only
        // one candidate at a time, never a second uncompressed vault copy.
        QByteArray searchContent;
        QVector<int> lineStarts;
    };
    struct Posting {
        int docId = 0;
        QVector<int>
            positions; // body occurrences; title-only postings are empty
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
