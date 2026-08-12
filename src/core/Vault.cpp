#include "Vault.h"

#include "MarkdownWikiLinkScanner.h"
#include "MascotSeed.h"
#include "Perf.h"
#include "WikiLink.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>

Vault::Vault(QString rootPath) : m_root(std::move(rootPath)) {}

QString Vault::titleFromPath(const QString &path) {
    return QFileInfo(path).completeBaseName();
}

void Vault::scan() {
    EMERALD_PROFILE_SCOPE("Vault::scan");
    m_notes.clear();
    QDirIterator it(m_root, {QStringLiteral("*.md")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        m_notes.push_back(Note{path, titleFromPath(path)});
    }
    std::sort(m_notes.begin(), m_notes.end(), [](const Note &a, const Note &b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });

    m_folders.clear();
    const QDir rootDir(m_root);
    QDirIterator dirs(m_root, QDir::Dirs | QDir::NoDotAndDotDot,
                      QDirIterator::Subdirectories);
    while (dirs.hasNext())
        m_folders.push_back(rootDir.relativeFilePath(dirs.next()));
    m_folders.sort(Qt::CaseInsensitive);
}

QString Vault::read(const QString &path) const {
    QFile f(path);
    // No QIODevice::Text: we normalize end-of-lines ourselves so the in-memory
    // model is always LF, regardless of how the file was written on disk.
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QString content = QString::fromUtf8(f.readAll());
    content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    content.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return content;
}

bool Vault::write(const QString &path, const QString &content) const {
    QFile f(path);
    // No QIODevice::Text: write raw UTF-8 so newlines stay LF on every platform
    // (Text mode would translate to CRLF on Windows). Vault files are byte-for-
    // byte identical across OSes, which keeps git/cloud-synced vaults clean.
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = content.toUtf8();
    return f.write(bytes) == bytes.size();
}

QString Vault::resolveExistingFileWithinRoot(const QString &relativePath) const {
    if (relativePath.isEmpty())
        return {};

    const QString normalized = QDir::fromNativeSeparators(relativePath);
    if (QDir::isAbsolutePath(normalized))
        return {};
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.contains(QStringLiteral("..")))
        return {};

    const QString root = QDir(m_root).canonicalPath();
    if (root.isEmpty())
        return {};
    const QFileInfo candidate(QDir(root).filePath(QDir::cleanPath(normalized)));
    if (!candidate.exists() || !candidate.isFile())
        return {};
    const QString canonical = candidate.canonicalFilePath();
    if (canonical.isEmpty())
        return {};

#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    const QString rootPrefix =
        root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    return canonical.startsWith(rootPrefix, cs) ? canonical : QString();
}

QString Vault::pathForTitle(const QString &title) const {
    for (const Note &n : m_notes) {
        if (n.title.compare(title, Qt::CaseInsensitive) == 0)
            return n.path;
    }
    return {};
}

bool Vault::isValidTitle(const QString &title) {
    if (title.isEmpty() || title == QStringLiteral(".") ||
        title == QStringLiteral(".."))
        return false;
    // Windows silently strips trailing dots/spaces from filenames, which would
    // cause a renamed/created note to land somewhere other than its title.
    if (title.endsWith(QLatin1Char('.')) || title.endsWith(QLatin1Char(' ')))
        return false;
    for (const QChar c : title)
        if (c.unicode() < 0x20 || QStringLiteral("/\\:*?\"<>|").contains(c))
            return false;
    // Windows reserved device names (CON, NUL, COM1-9, LPT1-9, ...). Reserved
    // even with an extension, so match the segment before the first dot,
    // case-insensitively.
    static const QRegularExpression reserved(
        QStringLiteral("^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(title).hasMatch())
        return false;
    return true;
}

QString Vault::replaceLinkTargets(const QString &content,
                                  const QString &oldTitle,
                                  const QString &newTitle) {
    QString result;
    int last = 0;
    auto it = WikiLink::pattern().globalMatch(content);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString inner = m.captured(1);
        // The target is the inner text before any #heading or |alias.
        int cut = inner.size();
        const int hash = inner.indexOf(QLatin1Char('#'));
        const int pipe = inner.indexOf(QLatin1Char('|'));
        if (hash >= 0)
            cut = qMin(cut, hash);
        if (pipe >= 0)
            cut = qMin(cut, pipe);
        if (inner.left(cut).trimmed().compare(oldTitle, Qt::CaseInsensitive) != 0)
            continue;
        result += content.mid(last, m.capturedStart(1) - last);
        result += newTitle + inner.mid(cut); // keep the |alias / #heading
        last = m.capturedEnd(1);
    }
    result += content.mid(last);
    return result;
}

QString Vault::renameNote(const QString &oldPath, const QString &newTitle) {
    if (!isValidTitle(newTitle))
        return {};
    const QFileInfo fi(oldPath);
    const QString newPath =
        QDir(fi.absolutePath()).filePath(newTitle + QStringLiteral(".md"));
    if (newPath == oldPath)
        return oldPath; // unchanged
    // On case-insensitive filesystems (Windows, default macOS) newPath "exists"
    // when it differs from oldPath only by case — that's the same file, so a
    // case-only retitle is allowed. canonicalFilePath() resolves both to the
    // real on-disk entry, so a genuine collision still has a different canonical.
    if (QFileInfo::exists(newPath) &&
        QFileInfo(newPath).canonicalFilePath() != fi.canonicalFilePath())
        return {}; // would clobber another note
    if (!QFile::rename(oldPath, newPath))
        return {};

    for (Note &n : m_notes) {
        if (n.path == oldPath) {
            n.path = newPath;
            n.title = newTitle;
            break;
        }
    }
    std::sort(m_notes.begin(), m_notes.end(), [](const Note &a, const Note &b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return newPath;
}

int Vault::updateLinksTo(const QString &oldTitle, const QString &newTitle) {
    return updateLinksToPaths(oldTitle, newTitle).size();
}

QStringList Vault::updateLinksToPaths(const QString &oldTitle,
                                      const QString &newTitle) {
    QStringList changedPaths;
    for (const Note &n : m_notes) {
        const QString content = read(n.path);
        const QString updated = replaceLinkTargets(content, oldTitle, newTitle);
        if (updated != content && write(n.path, updated)) {
            changedPaths << n.path;
        }
    }
    return changedPaths;
}

QVector<Vault::BrokenLink> Vault::brokenLinks() const {
    EMERALD_PROFILE_SCOPE("Vault::brokenLinks");

    // pathForTitle() resolves the first case-insensitive title in the sorted
    // vault list. Build the same lookup once so scanning links stays O(notes +
    // links), including vaults with duplicate filenames in different folders.
    QHash<QString, QString> pathsByTitle;
    pathsByTitle.reserve(m_notes.size());
    for (const Note &note : m_notes) {
        const QString key = note.title.toCaseFolded();
        if (!pathsByTitle.contains(key))
            pathsByTitle.insert(key, note.path);
    }

    QHash<QString, bool> emptyByPath;
    emptyByPath.reserve(m_notes.size());
    QVector<BrokenLink> issues;
    for (const Note &source : m_notes) {
        const QString content = read(source.path);
        for (const MarkdownWikiLinkScanner::Link &link :
             MarkdownWikiLinkScanner::scan(content)) {
            const QString &target = link.target;
            if (target.isEmpty())
                continue;

            const QString targetPath =
                pathsByTitle.value(target.toCaseFolded());
            BrokenLink::State state = BrokenLink::State::MissingNote;
            if (!targetPath.isEmpty()) {
                auto emptyIt = emptyByPath.constFind(targetPath);
                bool empty = false;
                if (emptyIt == emptyByPath.constEnd()) {
                    const QString targetContent =
                        targetPath == source.path ? content : read(targetPath);
                    const qint64 targetBytes = QFileInfo(targetPath).size();
                    empty = targetBytes == 0 ||
                            (!targetContent.isEmpty() &&
                             MascotSeed::strip(targetContent).trimmed().isEmpty());
                    emptyByPath.insert(targetPath, empty);
                } else {
                    empty = emptyIt.value();
                }
                if (!empty)
                    continue;
                state = BrokenLink::State::EmptyNote;
            }

            issues.push_back({target, source.path, link.position, link.length,
                              link.line, state});
        }
    }
    return issues;
}

Note Vault::createNote(const QString &title) {
    if (!isValidTitle(title))
        return {};

    const QString existing = pathForTitle(title);
    if (!existing.isEmpty())
        return Note{existing, titleFromPath(existing)};

    const QString path = QDir(m_root).filePath(title + QStringLiteral(".md"));
    // The title is shown by the editor's title field, so the body starts empty
    // rather than repeating it as an "# H1".
    if (!write(path, QString()))
        return {};

    Note note{path, title};
    m_notes.push_back(note);
    std::sort(m_notes.begin(), m_notes.end(), [](const Note &a, const Note &b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return note;
}

Note Vault::createNoteIn(const QString &dir, const QString &title) {
    if (!isValidTitle(title))
        return {};

    const QString folder = dir.isEmpty() ? m_root : dir;
    const QString path = QDir(folder).filePath(title + QStringLiteral(".md"));
    const QFileInfo existing(path);
    if (existing.exists() && !existing.isFile())
        return {};
    if (!existing.exists() && !write(path, QString()))
        return {};
    return Note{path, title};
}

bool Vault::createFolder(const QString &dir, const QString &name) {
    if (!isValidTitle(name))
        return false;
    const QString folder = dir.isEmpty() ? m_root : dir;
    return QDir(folder).mkdir(name);
}

bool Vault::remove(const QString &path) {
    const QFileInfo fi(path);
    if (!fi.exists())
        return false;
    // Move to the OS trash (recoverable) rather than deleting outright — this
    // handles both a single note and a whole folder with its contents. Returns
    // false when the platform offers no trash for that location; the caller then
    // reports the failure and leaves the file in place (safer than a silent,
    // unrecoverable delete).
    return QFile::moveToTrash(path);
}

QString Vault::movePath(const QString &srcPath, const QString &destDir) {
    const QFileInfo fi(srcPath);
    if (!fi.exists())
        return {};
    const QString dest = QDir(destDir).filePath(fi.fileName());
    if (dest == srcPath)
        return srcPath; // already there
    if (QFileInfo::exists(dest))
        return {}; // a name collision in the target folder
    // Don't move a folder into itself or one of its descendants.
    if (fi.isDir() && (destDir == srcPath ||
                       destDir.startsWith(srcPath + QLatin1Char('/'))))
        return {};
    return QFile::rename(srcPath, dest) ? dest : QString();
}
