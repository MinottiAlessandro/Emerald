#include "MascotStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
// Where the per-vault state lives, relative to the vault root. A hidden folder
// keeps app data out of the way of the user's notes (cf. Obsidian's .obsidian).
QString storePath(const QString &root) {
    return QDir(root).filePath(QStringLiteral(".emerald/mascots.json"));
}
} // namespace

void MascotStore::load(const QString &vaultRoot) {
    m_root = vaultRoot;
    m_states.clear();
    if (m_root.isEmpty())
        return;

    QFile f(storePath(m_root));
    if (!f.open(QIODevice::ReadOnly))
        return; // no file yet: every note simply starts mascot-less
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject mascots = root.value(QStringLiteral("mascots")).toObject();
    for (auto it = mascots.begin(); it != mascots.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        State st;
        st.suppressed = o.value(QStringLiteral("suppressed")).toBool();
        // Seeds are stored as strings: JSON numbers are doubles and would lose
        // the low bits of a 64-bit value.
        st.seed = o.value(QStringLiteral("seed")).toString().toULongLong();
        if (st.seed != 0 || st.suppressed)
            m_states.insert(it.key(), st);
    }
}

QStringList MascotStore::notesWithMascots() const {
    QStringList out;
    for (auto it = m_states.begin(); it != m_states.end(); ++it)
        if (it.value().seed != 0 && !it.value().suppressed)
            out << it.key();
    out.sort(Qt::CaseInsensitive);
    return out;
}

quint64 MascotStore::seed(const QString &relPath) const {
    return m_states.value(relPath).seed;
}

bool MascotStore::suppressed(const QString &relPath) const {
    return m_states.value(relPath).suppressed;
}

void MascotStore::setSeed(const QString &relPath, quint64 seed) {
    State &st = m_states[relPath];
    st.seed = seed;
    st.suppressed = false;
    save();
}

void MascotStore::suppress(const QString &relPath) {
    State &st = m_states[relPath];
    st.seed = 0;
    st.suppressed = true;
    save();
}

void MascotStore::rename(const QString &oldRel, const QString &newRel) {
    bool changed = false;
    // Remap the entry itself and anything beneath it (so renaming/moving a
    // folder carries its notes' mascots along).
    const QString prefix = oldRel + QLatin1Char('/');
    const auto keys = m_states.keys();
    for (const QString &key : keys) {
        QString dst;
        if (key == oldRel)
            dst = newRel;
        else if (key.startsWith(prefix))
            dst = newRel + key.mid(oldRel.length());
        else
            continue;
        m_states.insert(dst, m_states.take(key));
        changed = true;
    }
    if (changed)
        save();
}

void MascotStore::remove(const QString &relPath) {
    if (m_states.remove(relPath) > 0)
        save();
}

void MascotStore::removeFolder(const QString &relPrefix) {
    const QString prefix = relPrefix + QLatin1Char('/');
    bool changed = false;
    const auto keys = m_states.keys();
    for (const QString &key : keys)
        if (key == relPrefix || key.startsWith(prefix))
            changed |= m_states.remove(key) > 0;
    if (changed)
        save();
}

void MascotStore::save() const {
    if (m_root.isEmpty())
        return;
    QJsonObject mascots;
    for (auto it = m_states.begin(); it != m_states.end(); ++it) {
        QJsonObject o;
        if (it.value().suppressed)
            o.insert(QStringLiteral("suppressed"), true);
        else
            o.insert(QStringLiteral("seed"),
                     QString::number(it.value().seed));
        mascots.insert(it.key(), o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("mascots"), mascots);

    const QString path = storePath(m_root);
    QDir().mkpath(QFileInfo(path).path()); // ensure <vault>/.emerald exists
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
