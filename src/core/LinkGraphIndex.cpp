#include "LinkGraphIndex.h"

#include "MarkdownWikiLinkScanner.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

void LinkGraphIndex::clear() {
  m_vaultRoot.clear();
  m_notes.clear();
  m_sources.clear();
}

void LinkGraphIndex::setNotes(const QString &vaultRoot,
                              const QVector<Note> &notes) {
  m_vaultRoot = vaultRoot;
  m_notes = notes;

  QSet<QString> livePaths;
  livePaths.reserve(notes.size());
  for (const Note &note : notes) {
    livePaths.insert(note.path);
    auto source = m_sources.find(note.path);
    if (source == m_sources.end())
      m_sources.insert(note.path, Source{note.title, {}});
    else
      source->title = note.title;
  }
  for (auto it = m_sources.begin(); it != m_sources.end();) {
    if (!livePaths.contains(it.key()))
      it = m_sources.erase(it);
    else
      ++it;
  }
}

void LinkGraphIndex::updateNote(const QString &path, const QString &title,
                                const QString &content) {
  if (path.isEmpty())
    return;
  Source &source = m_sources[path];
  source.title = title;
  source.targets.clear();
  for (const MarkdownWikiLinkScanner::Link &link :
       MarkdownWikiLinkScanner::scan(content)) {
    if (link.target.isEmpty())
      continue;
    const QString key = link.target.toCaseFolded();
    Target &target = source.targets[key];
    if (target.title.isEmpty())
      target.title = link.target;
    ++target.occurrences;
  }
}

void LinkGraphIndex::removeNote(const QString &path) {
  m_sources.remove(path);
  m_notes.erase(
      std::remove_if(m_notes.begin(), m_notes.end(),
                     [&path](const Note &note) { return note.path == path; }),
      m_notes.end());
}

void LinkGraphIndex::renamePath(const QString &oldPath, const QString &newPath,
                                const QString &newTitle) {
  if (oldPath.isEmpty() || newPath.isEmpty())
    return;
  auto source = m_sources.find(oldPath);
  if (source != m_sources.end()) {
    Source moved = std::move(source.value());
    m_sources.erase(source);
    if (!newTitle.isEmpty())
      moved.title = newTitle;
    m_sources.insert(newPath, std::move(moved));
  }
  for (Note &note : m_notes) {
    if (note.path != oldPath)
      continue;
    note.path = newPath;
    if (!newTitle.isEmpty())
      note.title = newTitle;
    break;
  }
}

QString LinkGraphIndex::folderForPath(const QString &path) const {
  if (m_vaultRoot.isEmpty() || path.isEmpty())
    return {};
  const QString relative = QDir(m_vaultRoot).relativeFilePath(path);
  const int slash = relative.indexOf(QLatin1Char('/'));
  return slash > 0 ? relative.left(slash) : QString();
}

LinkGraphIndex::Snapshot LinkGraphIndex::snapshot() const {
  Snapshot result;
  result.nodes.reserve(m_notes.size());
  result.nodeByPath.reserve(m_notes.size());

  // Match Vault::pathForTitle(): the first case-insensitive title in the
  // vault's current ordering wins when duplicate stems exist.
  QHash<QString, int> resolvedByTitle;
  resolvedByTitle.reserve(m_notes.size());
  for (const Note &note : m_notes) {
    const int id = result.nodes.size();
    result.nodes.push_back(
        {note.path, note.title, folderForPath(note.path), 0, 0, false});
    result.nodeByPath.insert(note.path, id);
    const QString key = note.title.toCaseFolded();
    if (!resolvedByTitle.contains(key))
      resolvedByTitle.insert(key, id);
  }

  QHash<QString, int> missingByTitle;
  for (const Note &note : m_notes) {
    const int from = result.nodeByPath.value(note.path, -1);
    const auto source = m_sources.constFind(note.path);
    if (from < 0 || source == m_sources.constEnd())
      continue;

    QStringList targetKeys = source->targets.keys();
    targetKeys.sort(Qt::CaseInsensitive);
    for (const QString &key : targetKeys) {
      const Target &target = source->targets.value(key);
      int to = resolvedByTitle.value(key, -1);
      if (to < 0) {
        to = missingByTitle.value(key, -1);
        if (to < 0) {
          to = result.nodes.size();
          missingByTitle.insert(key, to);
          result.nodes.push_back({{}, target.title, {}, 0, 0, true});
        }
      }
      result.edges.push_back({from, to, target.occurrences});
      ++result.nodes[from].outgoing;
      ++result.nodes[to].incoming;
    }
  }
  return result;
}
