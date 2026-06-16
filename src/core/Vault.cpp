#include "Vault.h"

#include "WikiLink.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>

Vault::Vault(QString rootPath) : m_root(std::move(rootPath)) {}

QString Vault::titleFromPath(const QString &path) {
    return QFileInfo(path).completeBaseName();
}

void Vault::scan() {
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
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    return in.readAll();
}

bool Vault::write(const QString &path, const QString &content) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    return true;
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
    for (const QChar c : title)
        if (QStringLiteral("/\\:*?\"<>|").contains(c))
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
    if (QFileInfo::exists(newPath))
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
    int changed = 0;
    for (const Note &n : m_notes) {
        const QString content = read(n.path);
        const QString updated = replaceLinkTargets(content, oldTitle, newTitle);
        if (updated != content && write(n.path, updated))
            ++changed;
    }
    return changed;
}

Note Vault::createNote(const QString &title) {
    const QString existing = pathForTitle(title);
    if (!existing.isEmpty())
        return Note{existing, titleFromPath(existing)};

    const QString path = QDir(m_root).filePath(title + QStringLiteral(".md"));
    // The title is shown by the editor's title field, so the body starts empty
    // rather than repeating it as an "# H1".
    write(path, QString());

    Note note{path, title};
    m_notes.push_back(note);
    std::sort(m_notes.begin(), m_notes.end(), [](const Note &a, const Note &b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return note;
}

Note Vault::createNoteIn(const QString &dir, const QString &title) {
    const QString folder = dir.isEmpty() ? m_root : dir;
    const QString path = QDir(folder).filePath(title + QStringLiteral(".md"));
    if (!QFileInfo::exists(path))
        write(path, QString());
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
    if (fi.isDir())
        return QDir(path).removeRecursively();
    return QFile::remove(path);
}
