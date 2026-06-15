#pragma once

#include "Note.h"
#include <QString>
#include <QVector>

// A vault is a folder of Markdown notes. Vault owns no GUI state — it just
// scans the folder, reads/writes files, and resolves link targets to paths.
class Vault {
public:
    explicit Vault(QString rootPath);

    QString root() const { return m_root; }

    // (Re)scan the folder for *.md files, sorted by title.
    void scan();
    const QVector<Note> &notes() const { return m_notes; }

    QString read(const QString &path) const;
    bool write(const QString &path, const QString &content) const;

    // Resolve a link target (e.g. "My Note") to an existing note's path,
    // matched case-insensitively by title. Empty string if none exists.
    QString pathForTitle(const QString &title) const;

    // Create a new note file named <title>.md and append it to the vault.
    // Returns the created note (or the existing one if it already exists).
    Note createNote(const QString &title);

    static QString titleFromPath(const QString &path);

private:
    QString m_root;
    QVector<Note> m_notes;
};
