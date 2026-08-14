#pragma once

#include "Note.h"
#include <QString>
#include <QStringList>
#include <QVector>

// A vault is a folder of Markdown notes. Vault owns no GUI state — it just
// scans the folder, reads/writes files, and resolves link targets to paths.
class Vault {
public:
    struct BrokenLink {
        enum class State { MissingNote, EmptyNote };

        QString target;
        QString sourcePath;
        int sourcePosition = 0;
        int sourceLength = 0;
        int line = 0;
        State state = State::MissingNote;
    };

    explicit Vault(QString rootPath);

    QString root() const { return m_root; }

    // (Re)scan the folder for *.md files and sub-folders, sorted by title/path.
    void scan();
    const QVector<Note> &notes() const { return m_notes; }
    // Sub-folder paths relative to the root (so empty folders are shown too).
    const QStringList &folders() const { return m_folders; }

    QString read(const QString &path) const;
    bool write(const QString &path, const QString &content) const;

    // Resolve an existing regular file named by a relative vault path. The
    // returned path is canonical and guaranteed to remain beneath the canonical
    // vault root; absolute paths, traversal, missing files, directories, and
    // symlinks that escape the vault are rejected with an empty result.
    QString resolveExistingFileWithinRoot(const QString &relativePath) const;

    // Resolve a link target (e.g. "My Note") to an existing note's path,
    // matched case-insensitively by title. Empty string if none exists.
    QString pathForTitle(const QString &title) const;

    // Create a new note file named <title>.md and append it to the vault.
    // Returns the created note (or the existing one if it already exists), or
    // an empty note when the title is unsafe or the file cannot be written.
    Note createNote(const QString &title);

    // Create <title>.md inside an absolute folder within the vault (used by the
    // folder tree's context menu). Returns the note, the existing one, or an
    // empty note when the title is unsafe or the file cannot be written.
    Note createNoteIn(const QString &dir, const QString &title);

    // Make a sub-folder `name` inside the absolute folder `dir`. Returns true
    // on success (false if it exists or the name is invalid).
    bool createFolder(const QString &dir, const QString &name);

    // Move a note file or a folder (contents included) to the OS trash. Returns
    // true on success. Call scan() afterwards to refresh the listing.
    bool remove(const QString &path);

    // Move a note or folder into destDir (within the vault). Returns the new
    // path, or empty on failure (collision, or moving a folder into itself).
    QString movePath(const QString &srcPath, const QString &destDir);

    // Rename the note at oldPath to <newTitle>.md in the same folder, updating
    // the in-memory list. Returns the new path, or empty on failure (a name
    // collision or an invalid title).
    QString renameNote(const QString &oldPath, const QString &newTitle);

    // Rewrite every semantic [[oldTitle]] link (preserving any |alias or
    // #heading) to point at newTitle across all notes. Commented links remain
    // untouched. Returns the number of files changed.
    int updateLinksTo(const QString &oldTitle, const QString &newTitle);
    // Same rewrite, returning the paths that actually changed so callers can
    // incrementally refresh dependent indexes.
    QStringList updateLinksToPaths(const QString &oldTitle, const QString &newTitle);

    // Find every semantic [[wiki-link]] whose target does not exist or whose
    // note has no body text. Results retain their exact source location so the
    // UI can take the user directly to the link that needs attention.
    QVector<BrokenLink> brokenLinks() const;

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
    QStringList m_folders;
};
