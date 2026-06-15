#pragma once

#include "core/LinkIndex.h"
#include <QMainWindow>
#include <QString>

class Vault;
class MarkdownEditor;
class QListWidget;
class QTimer;

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
    void openNoteByPath(const QString &path);
    void saveCurrent();
    void newNote();
    void onLinkClicked(const QString &target);
    void updateBacklinks();
    void selectInList(QListWidget *list, const QString &path);

    Vault *m_vault = nullptr;
    LinkIndex m_index;

    MarkdownEditor *m_editor = nullptr;
    QListWidget *m_noteList = nullptr;
    QListWidget *m_backlinks = nullptr;
    QTimer *m_saveTimer = nullptr;

    QString m_currentPath;
    QString m_currentTitle;
    bool m_loading = false;
};
