#pragma once

#include <QFrame>
#include <QStringList>

class QLineEdit;
class QListWidget;
class SearchIndex;

// A Telescope-style centred search overlay: a text field over a live result
// list, floating in the upper-middle of the window. Type to filter, Up/Down to
// move, Enter to open, Esc to dismiss. It reads the shared SearchIndex and asks
// the window to open the chosen note via openRequested().
class SearchPopup : public QFrame {
    Q_OBJECT
public:
    SearchPopup(const SearchIndex *index, QWidget *parent);

    // titlesOnly = a quick "go to note" picker that matches note titles only.
    void showCentered(bool titlesOnly = false);

    // A quick vault switcher: show the given vault folders (full paths), filter
    // by name as you type, and emit openVaultRequested() for the chosen one.
    void showVaults(const QStringList &dirs);

    // A template picker: show the given template files (full paths), filter by
    // name as you type, and emit templateRequested() for the chosen one.
    void showTemplates(const QStringList &files);

signals:
    void openRequested(const QString &path, const QString &query);
    void openVaultRequested(const QString &path);
    void templateRequested(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void refresh(const QString &text);
    void accept();      // open the highlighted result
    void reposition();  // keep centred over the parent

    const SearchIndex *m_index = nullptr;
    QLineEdit *m_input = nullptr;
    QListWidget *m_results = nullptr;
    bool m_titlesOnly = false;
    bool m_vaultMode = false;     // listing vault folders instead of notes
    bool m_templateMode = false;  // listing template files instead of notes
    QStringList m_vaultDirs;      // candidate vault folder paths (full)
    QStringList m_templateFiles;  // candidate template file paths (full)
};
