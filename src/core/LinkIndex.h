#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

class Vault;

// Tracks [[wiki-link]] relationships across the vault: which notes a note
// links to (forward) and which notes link back to it (backlinks).
//
// Keys are stored lowercased so matching is case-insensitive; a separate
// display map preserves the original casing for presentation.
class LinkIndex {
public:
    void rebuild(const Vault &vault);

    // Recompute one note's outgoing links after its content changes.
    void updateNote(const QString &title, const QString &content);

    // Display-cased titles of notes that link to `title`, sorted.
    QStringList backlinks(const QString &title) const;

    // Extract cleaned link targets from note content. Strips |alias and
    // #heading suffixes, e.g. "[[Foo|bar]]" and "[[Foo#sec]]" -> "Foo".
    static QStringList parseTargets(const QString &content);

private:
    void removeForward(const QString &lowerSource);

    QHash<QString, QSet<QString>> m_forward; // lowerSource -> {lowerTarget}
    QHash<QString, QSet<QString>> m_back;     // lowerTarget -> {lowerSource}
    QHash<QString, QString> m_display;        // lower -> display title
};
