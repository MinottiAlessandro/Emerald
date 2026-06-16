#pragma once

#include "core/SearchIndex.h"
#include <QMainWindow>
#include <QString>
#include <QStringList>

class Vault;
class MarkdownEditor;
class SearchPopup;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QTimer;
class QAction;
class QMenu;
class QPoint;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildActions();
    void buildUi();
    void openSettings();
    void loadSettings();

    void chooseVault();
    void openVault(const QString &path);
    void openInitialNote(); // open Home or the last-edited note on launch
    void refreshTree();
    void openNoteByPath(const QString &path, bool record = true);
    void saveCurrent();
    void newNote();
    void renameCurrent(const QString &rawTitle);
    void onLinkClicked(const QString &target);
    void selectInTree(const QString &path);
    void openSearch();
    void openQuickOpen();
    void onTreeItemClicked(QTreeWidgetItem *item, int column);
    void onTreeContextMenu(const QPoint &pos);
    void newNoteIn(const QString &dir);
    void newFolderIn(const QString &dir);
    void deleteEntry(const QString &path, bool isFolder);
    void navigateBack();
    void navigateForward();
    void pushHistory(const QString &path);
    void updateNavActions();

    Vault *m_vault = nullptr;
    SearchIndex m_searchIndex;

    MarkdownEditor *m_editor = nullptr;
    QWidget *m_centerColumn = nullptr; // width-capped title + body column
    QLineEdit *m_titleEdit = nullptr;
    QTreeWidget *m_noteTree = nullptr;
    SearchPopup *m_searchPopup = nullptr;
    QTimer *m_saveTimer = nullptr;
    QMenu *m_gearMenu = nullptr;
    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;

    QString m_currentPath;
    QString m_currentTitle;
    bool m_loading = false;

    QStringList m_history; // visited note paths (browser-style)
    int m_histIndex = -1;
};
