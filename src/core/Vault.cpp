#include "Vault.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
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

Note Vault::createNote(const QString &title) {
    const QString existing = pathForTitle(title);
    if (!existing.isEmpty())
        return Note{existing, titleFromPath(existing)};

    const QString path = QDir(m_root).filePath(title + QStringLiteral(".md"));
    write(path, QStringLiteral("# %1\n\n").arg(title));

    Note note{path, title};
    m_notes.push_back(note);
    std::sort(m_notes.begin(), m_notes.end(), [](const Note &a, const Note &b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return note;
}
