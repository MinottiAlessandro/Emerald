#pragma once

#include "core/SearchIndex.h"
#include <QMainWindow>
#include <QString>
#include <QStringList>

class Vault;
class MarkdownEditor;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QTimer;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildMenu();

    void chooseVault();
    void openVault(const QString &path);
    void refreshNoteList();
    void openNoteByPath(const QString &path, bool record = true);
    void saveCurrent();
    void newNote();
    void onLinkClicked(const QString &target);
    void selectInList(QListWidget *list, const QString &path);
    void onSearchChanged(const QString &text);
    void onNoteItemClicked(QListWidgetItem *item);
    void navigateBack();
    void navigateForward();
    void pushHistory(const QString &path);
    void updateNavActions();

    Vault *m_vault = nullptr;
    SearchIndex m_searchIndex;

    MarkdownEditor *m_editor = nullptr;
    QListWidget *m_noteList = nullptr;
    QLineEdit *m_searchBox = nullptr;
    QTimer *m_saveTimer = nullptr;
    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;

    QString m_currentPath;
    QString m_currentTitle;
    bool m_loading = false;

    QStringList m_history; // visited note paths (browser-style)
    int m_histIndex = -1;
};
