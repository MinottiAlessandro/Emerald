#include "MainWindow.h"

#include "MarkdownEditor.h"
#include "core/Vault.h"

#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
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
    m_searchBox = new QLineEdit(this);
    m_searchBox->setObjectName(QStringLiteral("search"));
    m_searchBox->setPlaceholderText(tr("Search notes…"));
    m_searchBox->setClearButtonEnabled(true);

    auto *side = new QWidget(this);
    auto *col = new QVBoxLayout(side);
    col->setContentsMargins(6, 6, 6, 6);
    col->setSpacing(6);
    col->addWidget(m_searchBox);
    col->addWidget(m_noteList);

    auto *sidebar = new QDockWidget(tr("Notes"), this);
    sidebar->setWidget(side);
    sidebar->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, sidebar);

    connect(m_noteList, &QListWidget::itemClicked, this,
            &MainWindow::onNoteItemClicked);
    connect(m_searchBox, &QLineEdit::textChanged, this,
            &MainWindow::onSearchChanged);
    connect(m_searchBox, &QLineEdit::returnPressed, this, [this] {
        if (m_noteList->count() > 0 &&
            (m_noteList->item(0)->flags() & Qt::ItemIsEnabled))
            onNoteItemClicked(m_noteList->item(0));
    });
    auto *focusSearch =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")), this);
    connect(focusSearch, &QShortcut::activated, this, [this] {
        m_searchBox->setFocus();
        m_searchBox->selectAll();
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
    m_searchIndex.rebuild(*m_vault);

    m_currentPath.clear();
    m_currentTitle.clear();
    m_loading = true;
    m_editor->clear();
    m_loading = false;
    m_backlinks->clear();
    m_searchBox->blockSignals(true);
    m_searchBox->clear();
    m_searchBox->blockSignals(false);

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
}

void MainWindow::saveCurrent() {
    if (!m_vault || m_currentPath.isEmpty())
        return;
    const QString content = m_editor->toPlainText();
    m_vault->write(m_currentPath, content);
    m_index.updateNote(m_currentTitle, content);
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
    m_index.updateNote(note.title, content);
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
        m_index.updateNote(note.title, content);
        m_searchIndex.updateNote(note.path, note.title, content);
        refreshNoteList();
        path = note.path;
    }
    openNoteByPath(path);
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

void MainWindow::onSearchChanged(const QString &text) {
    if (!m_vault)
        return;
    if (text.trimmed().isEmpty()) {
        refreshNoteList(); // empty query -> back to the full note list
        return;
    }
    m_noteList->clear();
    const QList<SearchIndex::Result> results = m_searchIndex.search(text);
    for (const SearchIndex::Result &r : results) {
        auto *item = new QListWidgetItem(r.title + QLatin1Char('\n') + r.snippet,
                                         m_noteList);
        item->setData(kPathRole, r.path);
        item->setToolTip(r.snippet);
    }
    if (results.isEmpty()) {
        auto *none = new QListWidgetItem(tr("No matches"), m_noteList);
        none->setFlags(Qt::NoItemFlags);
    }
}

void MainWindow::onNoteItemClicked(QListWidgetItem *item) {
    const QString path = item->data(kPathRole).toString();
    if (path.isEmpty())
        return;
    openNoteByPath(path);
    // If we got here from a search, jump to the first match in the note.
    const QStringList tokens = SearchIndex::tokenize(m_searchBox->text());
    if (!tokens.isEmpty())
        m_editor->jumpToMatch(tokens.first());
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveCurrent();
    event->accept();
}
