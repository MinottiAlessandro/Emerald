#pragma once

#include "core/Note.h"
#include "core/LinkGraphIndex.h"
#include "core/SearchIndex.h"
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

class Vault;
class MarkdownEditor;
class GraphPage;
class Mascot;
class SearchPopup;
class Updater;
class QAbstractItemModel;
class QBoxLayout;
class QTreeView;
class QModelIndex;
class QLineEdit;
class QLabel;
class QTimer;
class QAction;
class QMenu;
class QPoint;
class QSplitter;
class QFrame;
class QFileSystemWatcher;
class QThread;
class QImage;
class QResizeEvent;
class QToolButton;
class QStackedWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildActions();
    void buildUi();
    void openSettings();
    void loadSettings();
    void loadVaultSpellLanguages();
    void refreshThemeUi();
    void setReadMode(bool enabled, bool persist = true);
    void updateReadModeUi();
    bool ensureVaultWritable();
    void updateResponsiveLayout();
    void applyEditorColumnWidth();
    void applyMobileSplit();
    void updateSplitterHandleWidth();
    void updateMobileNavigationControls();
    void showMobileNotes();
    void showMobileEditor();
    void openManual();
    void showWhatsNew();          // bundled notes for the running release
    void maybeShowWhatsNew(bool hadPersistentSettings);
    void checkForUpdates();       // query GitHub for a newer release, then update
    void newVault();              // create a fresh vault folder, then open it
    void deleteCurrentNote();     // delete the open note (with confirmation)
    void changeFontSize(int delta); // +1 / -1 step; 0 resets to the default
    void toggleSidebar(); // collapse/restore the left pane (Ctrl+\ or handle click)
    void onEditorContextMenu(const QPoint &pos);

    void chooseVault();
    void openVaultSwitcher(); // quick picker for sibling vaults (Ctrl+Shift+O)
    void openVault(const QString &path);
    void updateVaultTitle();
    void startIndexRebuild();
    void openGraphView();
    void openLocalGraphView();
    void openInitialNote(); // open Home or the last-edited note on launch
    // Rebuild the sidebar from the vault. Folder expansion is preserved across
    // rebuilds (so a rename/move/new-note doesn't collapse the tree); a fresh
    // vault passes false so it always opens fully collapsed.
    void refreshTree(bool preserveExpansion = true);
    void openNoteByPath(const QString &path, bool record = true,
                        bool saveBeforeOpen = true);
    void showGraphView(bool local, const QString &rootPath, bool record = true,
                       bool saveBeforeOpen = true);
    void showNotePage();
    void refreshGraphPage();
    void saveCurrent();
    // Resolve the configured default note folder, falling back to the vault
    // root when no folder is selected or the saved folder no longer exists.
    QString defaultNoteDirectory() const;
    void newNote();
    void renameCurrent(const QString &rawTitle);
    void onLinkClicked(const QString &target);
    void onFileChanged(const QString &path); // external edit on disk
    // Reconcile the open note with disk after an external change: reload when
    // there are no unsaved edits, re-arm the (possibly dropped) file watch, and
    // never clobber the user's buffer. Driven by onFileChanged and the rescan.
    void syncOpenNoteFromDisk();
    void watchCurrent();
    // A vault folder changed on disk (a note added, removed, or renamed by
    // another program). Coalesced via m_rescanTimer, then rescan + refreshTree.
    void onVaultDirChanged(const QString &path);
    // Rescan the vault listing and update the search index only for notes whose
    // path/title changed since the previous listing.
    void rescanVaultIncremental(bool preserveExpansion = true);
    struct NoteFileMeta {
        QString title;
        qint64 size = -1;
        QDateTime modified;
    };
    NoteFileMeta noteFileMeta(const Note &note) const;
    QHash<QString, NoteFileMeta> scannedNoteMeta() const;
    void updateIndexForScannedVault(const QHash<QString, NoteFileMeta> &previous);
    void markNoteMetaCurrent(const QString &path, const QString &title);
    // Keep the watcher's directory list in sync with the vault's folders (root
    // + sub-folders) so externally-created notes are noticed wherever they land.
    void watchVaultDirs();
    void selectInTree(const QString &path);
    void openSearch();
    void openQuickOpen();
    void openBrokenLinks();
    void openBrokenLinkSource(const QString &path, int position, int length);
    void insertTemplate(); // open the template picker (Ctrl+T)
    void insertImage();    // copy/select an image and insert a Markdown link
    void insertImagesFromFiles(const QStringList &paths);
    void insertPastedImage(const QImage &image);
    QString attachImageFile(const QString &sourcePath);
    QString savePastedImageAttachment(const QImage &image);
    QString uniqueAttachmentPath(const QString &baseName,
                                 const QString &suffix) const;
    QString imageMarkdownForPath(const QString &path,
                                 const QString &altText) const;
    void insertImageMarkdownLines(const QStringList &lines);
    // Insert the chosen template at the caret (or at the body start when the
    // caret isn't in the body), expanding {{date}} / {{time}} / {{title}}.
    void onTemplateChosen(const QString &path);
    void notify(const QString &text, int ms = 3000); // transient toast message
    void openFindInFile();
    void findInFile(bool forward);
    void positionFindBar();
    void buildShortcutCheatsheet();
    void positionShortcutCheatsheet();
    void showShortcutCheatsheet();
    void hideShortcutCheatsheet();
    void positionToast(); // re-center the toast over the editor's bottom edge
    void positionMascot(); // pin the mascot to the editor's bottom-right corner
    void refreshMascot();  // sync the corner mascot to the open note's seed
    void onMascotSeedChanged(quint64 seed); // editor reported a new/edited seed
    void generateMascot(); // create/replace this note's mascot from its content
    void deleteMascot();   // remove this note's mascot (clears the header line)
    void openMascotGallery(); // transient grid of every mascot in the vault
    void maybeAutoGenerateMascot(); // auto-create once past the char threshold
    void updateMascotActions();     // enable/disable the menu entries
    // Notes whose mascot the user deleted by hand: auto-generation skips them so
    // it won't keep recreating one (manual Generate still works and re-enables
    // it). Kept per note path in QSettings — auto-gen is a local opt-in, so its
    // suppression is local too.
    bool autoMascotOff(const QString &path) const;
    void setAutoMascotOff(const QString &path, bool off);
    void onTreeIndexClicked(const QModelIndex &index);
    void onTreeContextMenu(const QPoint &pos);
    void newNoteIn(const QString &dir);
    void newFolderIn(const QString &dir);
    void deleteEntries(const QStringList &paths);
    void clearStaleSettingsFor(const QString &path, bool isFolder);
    void reconcileAfterDeletion(); // refresh tree + open a fallback if needed
    void moveItems(const QStringList &srcPaths, const QString &destDir);
    void navigateBack();
    void navigateForward();
    bool hasUnsavedDraft() const;
    struct PageLocation {
        enum class Kind { Note, GlobalGraph, LocalGraph };
        Kind kind = Kind::Note;
        QString path;
        QString viewState;

        bool operator==(const PageLocation &other) const {
            return kind == other.kind && path == other.path;
        }
    };
    void captureCurrentPageState();
    void openHistoryLocation(const PageLocation &location,
                             bool saveBeforeOpen = true);
    void pushHistory(const PageLocation &location);
    // Drop history entries whose file no longer exists (e.g. deleted notes),
    // keeping the index on the current note when it survived.
    void pruneHistory();
    void updateNavActions();
    // Per-note caret positions, remembered so reopening a note restores the
    // caret. Persisted to settings so it survives restarts (saveCursorPositions
    // also folds in the open note's current caret).
    void saveCursorPositions();
    void loadCursorPositions();

    Vault *m_vault = nullptr;
    SearchIndex m_searchIndex;
    LinkGraphIndex m_linkGraphIndex;

    MarkdownEditor *m_editor = nullptr;
    GraphPage *m_graphPage = nullptr;
    QStackedWidget *m_pageStack = nullptr;
    QWidget *m_notePage = nullptr;
    QWidget *m_centerColumn = nullptr; // width-capped title + body column
    QWidget *m_centerPane = nullptr;   // editor-side pane; the mascot's parent
    QWidget *m_mobileEditorBar = nullptr;
    QToolButton *m_mobileNotesButton = nullptr;
    QToolButton *m_mobileEditorButton = nullptr;
    QSplitter *m_splitter = nullptr;   // collapsible sidebar | editor
    QWidget *m_splitHandle = nullptr;  // its drag handle (click to toggle)
    QPoint m_handlePressPos;
    QLineEdit *m_titleEdit = nullptr;
    QLabel *m_sideTitle = nullptr;
    QLabel *m_toast = nullptr;         // auto-hiding notice (replaces status bar)
    QTimer *m_toastTimer = nullptr;
    QFrame *m_findBar = nullptr;       // in-note find overlay
    QLineEdit *m_findInput = nullptr;
    QFrame *m_shortcutCheatsheet = nullptr; // visible only while Alt+X is held
    QBoxLayout *m_shortcutColumnsLayout = nullptr;
    QTimer *m_shortcutReleaseTimer = nullptr; // filters native repeat releases
    QTreeView *m_noteTree = nullptr;
    QAbstractItemModel *m_noteTreeModel = nullptr;
    SearchPopup *m_searchPopup = nullptr;
    Updater *m_updater = nullptr;
    QTimer *m_saveTimer = nullptr;
    QTimer *m_rescanTimer = nullptr; // coalesces vault-folder change bursts
    QTimer *m_reloadTimer = nullptr; // coalesces open-note file-change bursts
    QThread *m_indexThread = nullptr; // builds the startup search index off the UI thread
    QMenu *m_gearMenu = nullptr;
    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_readModeAction = nullptr;
    QAction *m_graphAction = nullptr;
    QAction *m_localGraphAction = nullptr;
    QAction *m_newNoteAction = nullptr;
    QAction *m_renameAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_insertTemplateAction = nullptr;
    QAction *m_deleteAction = nullptr; // Delete Note (shared by menu + context menu)
    QAction *m_insertImageAction = nullptr;
    QAction *m_genMascotAction = nullptr;
    QAction *m_delMascotAction = nullptr;
    QAction *m_findAction = nullptr;

    Mascot *m_mascot = nullptr;   // per-note creature in the bottom-right corner

    QFileSystemWatcher *m_watcher = nullptr;
    QString m_currentPath;
    QString m_currentTitle;
    QString m_pendingNoteDir; // target folder for an unsaved New Note draft
    quint64 m_lastSavedFingerprint = 0; // body fingerprint as last written/loaded
    bool m_loading = false;
    bool m_readMode = false; // persisted independently for each open vault
    PageLocation::Kind m_activePage = PageLocation::Kind::Note;

    QVector<PageLocation> m_history; // notes and graph pages (browser-style)
    int m_histIndex = -1;
    QHash<QString, int> m_cursorPositions; // note path -> last caret position
    QHash<QString, NoteFileMeta> m_noteMeta; // path -> title/size/mtime snapshot
    int m_indexGeneration = 0;
    int m_editorColumnWidth = 820;
    bool m_editorFullWidth = false;
    QList<int> m_desktopSplitterSizes;
    bool m_mobileLayout = false;
    bool m_mobileShowingNotes = true;
    bool m_shortcutCheatsheetHeld = false;
};
