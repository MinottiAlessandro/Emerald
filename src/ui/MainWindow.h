#pragma once

#include "core/LinkIndex.h"
#include <QMainWindow>
#include <QString>

class Vault;
class MarkdownEditor;
class GraphView;
class QListWidget;
class QDockWidget;
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
    void toggleGraph();
    void refreshGraph();

    Vault *m_vault = nullptr;
    LinkIndex m_index;

    MarkdownEditor *m_editor = nullptr;
    QListWidget *m_noteList = nullptr;
    QListWidget *m_backlinks = nullptr;
    GraphView *m_graph = nullptr;
    QDockWidget *m_graphDock = nullptr;
    QTimer *m_saveTimer = nullptr;
    bool m_graphInit = false;

    QString m_currentPath;
    QString m_currentTitle;
    bool m_loading = false;
};
