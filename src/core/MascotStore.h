#pragma once

#include <QHash>
#include <QString>

// Per-vault persistence of each note's mascot state, saved as JSON in
// <vault>/.emerald/mascots.json so mascots travel with the vault (copy or sync
// the folder and the creatures come along). Keyed by each note's path relative
// to the vault root. A note is in one of three states:
//   - absent:     never generated; auto-generation may create one.
//   - has a seed: a 64-bit seed captured when the mascot was generated. The
//                 seed alone re-creates the creature, so it's "frozen" — later
//                 edits don't change it, and it survives restarts.
//   - suppressed: the user deleted it. Auto-generation must stay hands-off
//                 (sticky delete); only a manual Generate brings one back.
class MascotStore {
public:
    // Point at a vault root and load its mascots.json (clears state for an
    // empty/missing file). Pass an empty root to detach.
    void load(const QString &vaultRoot);

    // The frozen seed for a note, or 0 if it has no live mascot.
    quint64 seed(const QString &relPath) const;
    // True if the user deleted this note's mascot (auto must stay hands-off).
    bool suppressed(const QString &relPath) const;

    // Generate: store a seed and clear any suppressed flag. Persists.
    void setSeed(const QString &relPath, quint64 seed);
    // Delete: drop the seed and mark suppressed (sticky). Persists.
    void suppress(const QString &relPath);

    // Keep state in step with the vault. rename() is prefix-aware, so it also
    // remaps every note under a renamed/moved folder. Each persists.
    void rename(const QString &oldRel, const QString &newRel);
    void remove(const QString &relPath);        // a single note
    void removeFolder(const QString &relPrefix); // a folder and its contents

private:
    struct State {
        quint64 seed = 0;     // 0 once suppressed
        bool suppressed = false;
    };
    void save() const;

    QString m_root;                  // vault root, "" when detached
    QHash<QString, State> m_states;  // relPath -> state
};
