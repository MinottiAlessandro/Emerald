#pragma once

#include "core/SearchIndex.h"
#include <QHash>
#include <QMainWindow>
#include <QPoint>
#include <QString>
#include <QStringList>

class Vault;
class MarkdownEditor;
class SearchPopup;
class Updater;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;
class QTimer;
class QAction;
class QMenu;
class QPoint;
class QSplitter;
class QFrame;
class QFileSystemWatcher;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildActions();
    void buildUi();
    void openSettings();
    void loadSettings();
    void openManual();
    void checkForUpdates();       // query GitHub for a newer release, then update
    void newVault();              // create a fresh vault folder, then open it
    void deleteCurrentNote();     // delete the open note (with confirmation)
    void changeFontSize(int delta); // +1 / -1 step; 0 resets to the default
    void onEditorContextMenu(const QPoint &pos);

    void chooseVault();
    void openVaultSwitcher(); // quick picker for sibling vaults (Ctrl+Shift+O)
    void openVault(const QString &path);
    void openInitialNote(); // open Home or the last-edited note on launch
    void refreshTree();
    void openNoteByPath(const QString &path, bool record = true);
    void saveCurrent();
    void newNote();
    void renameCurrent(const QString &rawTitle);
    void onLinkClicked(const QString &target);
    void onFileChanged(const QString &path); // external edit on disk
    void watchCurrent();
    void selectInTree(const QString &path);
    void openSearch();
    void openQuickOpen();
    void notify(const QString &text, int ms = 3000); // transient toast message
    void openFindInFile();
    void findInFile(bool forward);
    void positionFindBar();
    void positionToast(); // re-center the toast over the editor's bottom edge
    void onTreeItemClicked(QTreeWidgetItem *item, int column);
    void onTreeContextMenu(const QPoint &pos);
    void newNoteIn(const QString &dir);
    void newFolderIn(const QString &dir);
    void deleteEntries(const QStringList &paths);
    void clearStaleSettingsFor(const QString &path, bool isFolder);
    void reconcileAfterDeletion(); // refresh tree + open a fallback if needed
    void moveItems(const QStringList &srcPaths, const QString &destDir);
    void navigateBack();
    void navigateForward();
    void pushHistory(const QString &path);
    void updateNavActions();

    Vault *m_vault = nullptr;
    SearchIndex m_searchIndex;

    MarkdownEditor *m_editor = nullptr;
    QWidget *m_centerColumn = nullptr; // width-capped title + body column
    QSplitter *m_splitter = nullptr;   // collapsible sidebar | editor
    QWidget *m_splitHandle = nullptr;  // its drag handle (click to toggle)
    QPoint m_handlePressPos;
    QLineEdit *m_titleEdit = nullptr;
    QLabel *m_toast = nullptr;         // auto-hiding notice (replaces status bar)
    QTimer *m_toastTimer = nullptr;
    QFrame *m_findBar = nullptr;       // in-note find overlay
    QLineEdit *m_findInput = nullptr;
    QTreeWidget *m_noteTree = nullptr;
    SearchPopup *m_searchPopup = nullptr;
    Updater *m_updater = nullptr;
    QTimer *m_saveTimer = nullptr;
    QMenu *m_gearMenu = nullptr;
    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_deleteAction = nullptr; // Delete Note (shared by menu + context menu)

    QFileSystemWatcher *m_watcher = nullptr;
    QString m_currentPath;
    QString m_currentTitle;
    QString m_lastSavedContent; // body as last written/loaded, to spot extern edits
    bool m_loading = false;

    QStringList m_history; // visited note paths (browser-style)
    int m_histIndex = -1;
    QHash<QString, int> m_cursorPositions; // note path -> last caret position
};
