#pragma once

#include <QFrame>
#include <QList>
#include <QStringList>

class QLineEdit;
class QLabel;
class QListWidget;
class QHideEvent;
class SearchIndex;

// A Telescope-style centred search overlay: a text field over a live result
// list, floating in the upper-middle of the window. Type to filter, Up/Down to
// move, Enter to open, Esc to dismiss. It reads the shared SearchIndex and asks
// the window to open the chosen note via openRequested().
class SearchPopup : public QFrame {
    Q_OBJECT
public:
    struct BrokenLinkItem {
        QString label;
        QString path;
        int position = 0;
        int length = 0;
    };

    SearchPopup(const SearchIndex *index, QWidget *parent);

    // titlesOnly = a quick "go to note" picker that matches note titles only.
    void showCentered(bool titlesOnly = false);

    // A quick vault switcher: show the given vault folders (full paths), filter
    // by name as you type, and emit openVaultRequested() for the chosen one.
    void showVaults(const QStringList &dirs);

    // A template picker: show the given template files (full paths), filter by
    // name as you type, and emit templateRequested() for the chosen one.
    void showTemplates(const QStringList &files);

    // A filterable broken-link report. Activating a row opens and selects the
    // exact [[link]] occurrence in its source note.
    void showBrokenLinks(const QList<BrokenLinkItem> &items);

signals:
    void openRequested(const QString &path, const QString &query);
    void openVaultRequested(const QString &path);
    void templateRequested(const QString &path);
    void brokenLinkRequested(const QString &path, int position, int length);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setModeTitle(const QString &title);
    void refresh(const QString &text);
    void accept();      // open the highlighted result
    void reposition();  // keep centred over the parent

    const SearchIndex *m_index = nullptr;
    QLabel *m_title = nullptr;
    QLineEdit *m_input = nullptr;
    QListWidget *m_results = nullptr;
    bool m_titlesOnly = false;
    bool m_vaultMode = false;     // listing vault folders instead of notes
    bool m_templateMode = false;  // listing template files instead of notes
    bool m_brokenLinkMode = false; // listing broken wiki-link occurrences
    QStringList m_vaultDirs;      // candidate vault folder paths (full)
    QStringList m_templateFiles;  // candidate template file paths (full)
    QList<BrokenLinkItem> m_brokenLinkItems;
};
