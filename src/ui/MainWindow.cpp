#include "MainWindow.h"

#include "MarkdownEditor.h"
#include "SearchPopup.h"
#include "core/Vault.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListWidget>
#include <QMenuBar>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

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
    connect(m_editor, &MarkdownEditor::linkClicked, this,
            &MainWindow::onLinkClicked);
    connect(m_editor, &MarkdownEditor::navigateBack, this,
            &MainWindow::navigateBack);
    connect(m_editor, &MarkdownEditor::navigateForward, this,
            &MainWindow::navigateForward);

    // Center the (width-capped) editor with stretch spacers. This keeps the
    // editor's width constant while the window/side panels resize, so dragging
    // a dock doesn't reflow or repaint the text.
    auto *center = new QWidget(this);
    auto *row = new QHBoxLayout(center);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    // The editor (stretch 1) grows first until it hits its max width, then the
    // zero-stretch spacers absorb the remainder, centering it.
    row->addStretch(0);
    row->addWidget(m_editor, 1);
    row->addStretch(0);
    setCentralWidget(center);

    m_noteList = new QListWidget(this);

    auto *side = new QWidget(this);
    auto *col = new QVBoxLayout(side);
    col->setContentsMargins(6, 6, 6, 6);
    col->setSpacing(6);
    col->addWidget(m_noteList);

    auto *sidebar = new QDockWidget(tr("Notes"), this);
    sidebar->setWidget(side);
    sidebar->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, sidebar);

    connect(m_noteList, &QListWidget::itemClicked, this,
            &MainWindow::onNoteItemClicked);

    m_searchPopup = new SearchPopup(&m_searchIndex, this);
    connect(m_searchPopup, &SearchPopup::openRequested, this,
            [this](const QString &path, const QString &query) {
                openNoteByPath(path);
                const QStringList tokens = SearchIndex::tokenize(query);
                if (!tokens.isEmpty())
                    m_editor->jumpToMatch(tokens.first());
            });
}

void MainWindow::buildMenu() {
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Open Vault…"), QKeySequence::Open, this,
                    &MainWindow::chooseVault);
    file->addAction(tr("&New Note"), QKeySequence::New, this,
                    &MainWindow::newNote);
    file->addAction(tr("&Search…"), QKeySequence::Find, this,
                    &MainWindow::openSearch);
    file->addSeparator();
    file->addAction(tr("Save"), QKeySequence::Save, this,
                    &MainWindow::saveCurrent);
    file->addSeparator();
    file->addAction(tr("Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto *nav = addToolBar(tr("Navigation"));
    nav->setMovable(false);
    nav->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_backAction = nav->addAction(QStringLiteral("←"));
    m_backAction->setToolTip(tr("Back  (Alt+Left)"));
    m_backAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    connect(m_backAction, &QAction::triggered, this, &MainWindow::navigateBack);
    m_forwardAction = nav->addAction(QStringLiteral("→"));
    m_forwardAction->setToolTip(tr("Forward  (Alt+Right)"));
    m_forwardAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    connect(m_forwardAction, &QAction::triggered, this,
            &MainWindow::navigateForward);
    updateNavActions();
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
    m_searchIndex.rebuild(*m_vault);

    m_currentPath.clear();
    m_currentTitle.clear();
    m_history.clear();
    m_histIndex = -1;
    updateNavActions();
    m_loading = true;
    m_editor->clear();
    m_loading = false;

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

void MainWindow::openNoteByPath(const QString &path, bool record) {
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
    if (record)
        pushHistory(path);
    updateNavActions();
}

void MainWindow::saveCurrent() {
    if (!m_vault || m_currentPath.isEmpty())
        return;
    const QString content = m_editor->toPlainText();
    m_vault->write(m_currentPath, content);
    m_searchIndex.updateNote(m_currentPath, m_currentTitle, content);
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
    const QString content = m_vault->read(note.path);
    m_searchIndex.updateNote(note.path, note.title, content);
    refreshNoteList();
    openNoteByPath(note.path);
}

void MainWindow::onLinkClicked(const QString &target) {
    if (!m_vault)
        return;
    QString path = m_vault->pathForTitle(target);
    if (path.isEmpty()) {
        const Note note = m_vault->createNote(target);
        const QString content = m_vault->read(note.path);
        m_searchIndex.updateNote(note.path, note.title, content);
        refreshNoteList();
        path = note.path;
    }
    openNoteByPath(path);
}

void MainWindow::navigateBack() {
    if (m_histIndex > 0) {
        --m_histIndex;
        openNoteByPath(m_history.at(m_histIndex), false);
    }
}

void MainWindow::navigateForward() {
    if (m_histIndex >= 0 && m_histIndex < m_history.size() - 1) {
        ++m_histIndex;
        openNoteByPath(m_history.at(m_histIndex), false);
    }
}

void MainWindow::pushHistory(const QString &path) {
    if (m_histIndex >= 0 && m_history.at(m_histIndex) == path)
        return; // re-opening the current note shouldn't add an entry
    while (m_history.size() > m_histIndex + 1)
        m_history.removeLast(); // opening a note drops the forward branch
    m_history.append(path);
    m_histIndex = m_history.size() - 1;
}

void MainWindow::updateNavActions() {
    if (m_backAction)
        m_backAction->setEnabled(m_histIndex > 0);
    if (m_forwardAction)
        m_forwardAction->setEnabled(m_histIndex >= 0 &&
                                    m_histIndex < m_history.size() - 1);
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

void MainWindow::openSearch() {
    if (m_vault)
        m_searchPopup->showCentered();
}

void MainWindow::onNoteItemClicked(QListWidgetItem *item) {
    const QString path = item->data(kPathRole).toString();
    if (!path.isEmpty())
        openNoteByPath(path);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveCurrent();
    event->accept();
}
