#pragma once

#include "Note.h"

#include <QHash>
#include <QString>
#include <QVector>

// Compact, GUI-free index of the vault's [[wiki-link]] relationships. Source
// notes retain normalized target counts so changing the set of note titles can
// re-resolve edges without rereading every Markdown file.
class LinkGraphIndex {
public:
  struct Node {
    QString path; // empty for an unresolved target
    QString title;
    QString folder; // top-level folder, empty for the vault root/missing
    int incoming = 0;
    int outgoing = 0;
    bool unresolved = false;
  };

  struct Edge {
    int from = -1;
    int to = -1;
    int occurrences = 0;
  };

  struct Snapshot {
    QVector<Node> nodes;
    QVector<Edge> edges;
    QHash<QString, int> nodeByPath;

    bool isEmpty() const { return nodes.isEmpty(); }
  };

  void clear();
  void setNotes(const QString &vaultRoot, const QVector<Note> &notes);
  void updateNote(const QString &path, const QString &title,
                  const QString &content);
  void removeNote(const QString &path);
  void renamePath(const QString &oldPath, const QString &newPath,
                  const QString &newTitle = QString());

  Snapshot snapshot() const;
  int noteCount() const { return m_notes.size(); }

private:
  struct Target {
    QString title;
    int occurrences = 0;
  };
  struct Source {
    QString title;
    QHash<QString, Target> targets; // case-folded title -> occurrence count
  };

  QString folderForPath(const QString &path) const;

  QString m_vaultRoot;
  QVector<Note> m_notes; // same ordering/resolution precedence as Vault
  QHash<QString, Source> m_sources; // absolute path -> parsed outbound targets
};
