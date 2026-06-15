#include "MainWindow.h"

#include "GraphView.h"
#include "MarkdownEditor.h"
#include "core/Vault.h"

#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QListWidget>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>

namespace {
constexpr int kPathRole = Qt::UserRole;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    buildUi();
    buildMenu();

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(700);
    connect(m_saveTimer, &QTimer::timeout, this, &MainWindow::saveCurrent);
    connect(m_editor, &MarkdownEditor::textChanged, this, [this] {
        if (!m_loading)
            m_saveTimer->start();
    });

    const QString last = QSettings().value(QStringLiteral("lastVault")).toString();
    if (!last.isEmpty() && QDir(last).exists())
        openVault(last);
    else
        statusBar()->showMessage(tr("Open a vault to begin  (Ctrl+O)"));
}

MainWindow::~MainWindow() { delete m_vault; }

void MainWindow::buildUi() {
    m_editor = new MarkdownEditor(this);
    setCentralWidget(m_editor);
    connect(m_editor, &MarkdownEditor::linkClicked, this,
            &MainWindow::onLinkClicked);

    m_noteList = new QListWidget(this);
    auto *sidebar = new QDockWidget(tr("Notes"), this);
    sidebar->setWidget(m_noteList);
    sidebar->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, sidebar);
    connect(m_noteList, &QListWidget::itemClicked, this, [this](QListWidgetItem *i) {
        openNoteByPath(i->data(kPathRole).toString());
    });

    m_backlinks = new QListWidget(this);
    auto *backDock = new QDockWidget(tr("Backlinks"), this);
    backDock->setWidget(m_backlinks);
    backDock->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::RightDockWidgetArea, backDock);
    connect(m_backlinks, &QListWidget::itemClicked, this, [this](QListWidgetItem *i) {
        const QString path = i->data(kPathRole).toString();
        if (!path.isEmpty())
            openNoteByPath(path);
    });

    m_graph = new GraphView(this);
    m_graphDock = new QDockWidget(tr("Graph"), this);
    m_graphDock->setWidget(m_graph);
    m_graphDock->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetFloatable |
                             QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, m_graphDock);
    m_graphDock->hide();
    connect(m_graph, &GraphView::noteActivated, this, &MainWindow::onLinkClicked);
    connect(m_graphDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible)
            refreshGraph();
    });
}

void MainWindow::buildMenu() {
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Open Vault…"), QKeySequence::Open, this,
                    &MainWindow::chooseVault);
    file->addAction(tr("&New Note"), QKeySequence::New, this,
                    &MainWindow::newNote);
    file->addSeparator();
    file->addAction(tr("Save"), QKeySequence::Save, this,
                    &MainWindow::saveCurrent);
    file->addSeparator();
    file->addAction(tr("Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(tr("&Graph"), QKeySequence(Qt::CTRL | Qt::Key_G), this,
                    &MainWindow::toggleGraph);
}

void MainWindow::toggleGraph() {
    if (m_graphDock->isVisible()) {
        m_graphDock->hide();
        return;
    }
    if (!m_graphInit) {
        m_graphInit = true;
        m_graphDock->setFloating(true);
        m_graphDock->resize(760, 560);
        m_graphDock->move(geometry().center() - QPoint(380, 280));
    }
    m_graphDock->show();
    m_graphDock->raise();
}

void MainWindow::refreshGraph() {
    if (!m_vault || !m_graphDock->isVisible())
        return;
    const LinkIndex::Graph g = m_index.graph();
    QList<QPair<QString, bool>> nodes;
    nodes.reserve(g.nodes.size());
    for (const LinkIndex::NodeInfo &n : g.nodes)
        nodes.append({n.title, n.resolved});
    m_graph->setGraph(nodes, g.edges, m_currentTitle);
}

void MainWindow::chooseVault() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Open Vault"), QDir::homePath());
    if (!dir.isEmpty())
        openVault(dir);
}

void MainWindow::openVault(const QString &path) {
    saveCurrent();
    delete m_vault;
    m_vault = new Vault(path);
    m_vault->scan();
    m_index.rebuild(*m_vault);

    m_currentPath.clear();
    m_currentTitle.clear();
    m_loading = true;
    m_editor->clear();
    m_loading = false;
    m_backlinks->clear();

    refreshNoteList();
    QSettings().setValue(QStringLiteral("lastVault"), path);
    setWindowTitle(QStringLiteral("Emerald — %1").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(
        tr("%1 notes").arg(m_vault->notes().size()), 4000);
}

void MainWindow::refreshNoteList() {
    m_noteList->clear();
    if (!m_vault)
        return;
    QStringList titles;
    for (const Note &n : m_vault->notes()) {
        auto *item = new QListWidgetItem(n.title, m_noteList);
        item->setData(kPathRole, n.path);
        titles << n.title;
    }
    m_editor->setCompletions(titles);
    selectInList(m_noteList, m_currentPath);
}

void MainWindow::openNoteByPath(const QString &path) {
    if (!m_vault || path.isEmpty())
        return;
    saveCurrent();

    m_loading = true;
    m_editor->setPlainText(m_vault->read(path));
    m_loading = false;

    m_currentPath = path;
    m_currentTitle = Vault::titleFromPath(path);
    setWindowTitle(QStringLiteral("Emerald — %1").arg(m_currentTitle));
    selectInList(m_noteList, path);
    updateBacklinks();
    if (m_graphDock->isVisible())
        m_graph->setCurrent(m_currentTitle);
}

void MainWindow::saveCurrent() {
    if (!m_vault || m_currentPath.isEmpty())
        return;
    const QString content = m_editor->toPlainText();
    m_vault->write(m_currentPath, content);
    m_index.updateNote(m_currentTitle, content);
}

void MainWindow::newNote() {
    if (!m_vault) {
        chooseVault();
        if (!m_vault)
            return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("New Note"), tr("Title:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const Note note = m_vault->createNote(name.trimmed());
    m_index.updateNote(note.title, m_vault->read(note.path));
    refreshNoteList();
    openNoteByPath(note.path);
    refreshGraph();
}

void MainWindow::onLinkClicked(const QString &target) {
    if (!m_vault)
        return;
    QString path = m_vault->pathForTitle(target);
    bool created = false;
    if (path.isEmpty()) {
        const Note note = m_vault->createNote(target);
        m_index.updateNote(note.title, m_vault->read(note.path));
        refreshNoteList();
        path = note.path;
        created = true;
    }
    openNoteByPath(path);
    if (created)
        refreshGraph(); // a new node appeared
}

void MainWindow::updateBacklinks() {
    m_backlinks->clear();
    for (const QString &title : m_index.backlinks(m_currentTitle)) {
        auto *item = new QListWidgetItem(title, m_backlinks);
        item->setData(kPathRole, m_vault->pathForTitle(title));
    }
    if (m_backlinks->count() == 0) {
        auto *empty = new QListWidgetItem(tr("No backlinks"), m_backlinks);
        empty->setFlags(Qt::NoItemFlags);
    }
}

void MainWindow::selectInList(QListWidget *list, const QString &path) {
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(kPathRole).toString() == path) {
            list->setCurrentRow(i);
            return;
        }
    }
    list->clearSelection();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveCurrent();
    event->accept();
}
