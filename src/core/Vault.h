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

    // Rename the note at oldPath to <newTitle>.md in the same folder, updating
    // the in-memory list. Returns the new path, or empty on failure (a name
    // collision or an invalid title).
    QString renameNote(const QString &oldPath, const QString &newTitle);

    // Rewrite every [[oldTitle]] link (preserving any |alias or #heading) to
    // point at newTitle, across all notes. Returns the number of files changed.
    int updateLinksTo(const QString &oldTitle, const QString &newTitle);

    static QString titleFromPath(const QString &path);
    // A title usable as a filename (non-empty, no path/illegal characters).
    static bool isValidTitle(const QString &title);
    // The pure-string link rewrite used by updateLinksTo (exposed for testing).
    static QString replaceLinkTargets(const QString &content,
                                      const QString &oldTitle,
                                      const QString &newTitle);

private:
    QString m_root;
    QVector<Note> m_notes;
};
