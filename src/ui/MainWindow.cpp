#include "MainWindow.h"

#include "MarkdownEditor.h"
#include "SearchPopup.h"
#include "core/Vault.h"

#include <QAction>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QHash>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <functional>

namespace {
constexpr int kPathRole = Qt::UserRole;     // leaf: the note's file path
constexpr int kDirRole = Qt::UserRole + 1;  // folder: its absolute path
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    buildActions();
    buildUi();
    loadSettings();

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

    // The note's title is shown (and edited) as the first line above the body;
    // it maps to the filename, so the ".md" is never shown. Committing an edit
    // renames the file.
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setObjectName(QStringLiteral("noteTitle"));
    m_titleEdit->setPlaceholderText(tr("Untitled"));
    m_titleEdit->setFrame(false);
    connect(m_titleEdit, &QLineEdit::editingFinished, this,
            [this] { renameCurrent(m_titleEdit->text()); });

    // Title + body stacked in a width-capped column, centered with stretch
    // spacers. The fixed measure means resizing the side panels doesn't reflow
    // or repaint the text.
    auto *column = new QWidget(this);
    auto *colLayout = new QVBoxLayout(column);
    colLayout->setContentsMargins(0, 0, 0, 0);
    colLayout->setSpacing(0);
    colLayout->addWidget(m_titleEdit);
    colLayout->addWidget(m_editor, 1);
    column->setMaximumWidth(820);
    column->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *center = new QWidget(this);
    auto *row = new QHBoxLayout(center);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addStretch(0);
    row->addWidget(column, 1);
    row->addStretch(0);
    setCentralWidget(center);

    m_noteTree = new QTreeWidget(this);
    m_noteTree->setHeaderHidden(true);
    m_noteTree->setIndentation(12);
    m_noteTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_noteTree, &QTreeWidget::itemClicked, this,
            &MainWindow::onTreeItemClicked);
    connect(m_noteTree, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::onTreeContextMenu);

    // Sidebar header: a big "Notes" title with the back/forward arrows on the
    // right (replaces the old top toolbar).
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("sideHeader"));
    auto *hrow = new QHBoxLayout(header);
    hrow->setContentsMargins(10, 4, 4, 4);
    hrow->setSpacing(2);
    auto *title = new QLabel(tr("Notes"), header);
    title->setObjectName(QStringLiteral("sideTitle"));
    auto *backBtn = new QToolButton(header);
    backBtn->setObjectName(QStringLiteral("navButton"));
    backBtn->setDefaultAction(m_backAction);
    auto *fwdBtn = new QToolButton(header);
    fwdBtn->setObjectName(QStringLiteral("navButton"));
    fwdBtn->setDefaultAction(m_forwardAction);
    hrow->addWidget(title);
    hrow->addStretch();
    hrow->addWidget(backBtn);
    hrow->addWidget(fwdBtn);

    // Sidebar footer: a gear button holding settings + the file actions.
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("sideFooter"));
    auto *frow = new QHBoxLayout(footer);
    frow->setContentsMargins(8, 4, 8, 4);
    auto *gear = new QToolButton(footer);
    gear->setObjectName(QStringLiteral("gearButton"));
    gear->setText(QStringLiteral("⚙"));
    gear->setToolTip(tr("Menu & settings"));
    gear->setPopupMode(QToolButton::InstantPopup);
    gear->setMenu(m_gearMenu);
    frow->addWidget(gear);
    frow->addStretch();

    auto *side = new QWidget(this);
    auto *col = new QVBoxLayout(side);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(header);
    col->addWidget(m_noteTree, 1);
    col->addWidget(footer);

    auto *sidebar = new QDockWidget(this);
    sidebar->setWidget(side);
    sidebar->setTitleBarWidget(new QWidget(sidebar)); // hide the default title
    sidebar->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, sidebar);

    m_searchPopup = new SearchPopup(&m_searchIndex, this);
    connect(m_searchPopup, &SearchPopup::openRequested, this,
            [this](const QString &path, const QString &query) {
                openNoteByPath(path);
                const QStringList tokens = SearchIndex::tokenize(query);
                if (!tokens.isEmpty())
                    m_editor->jumpToMatch(tokens.first());
            });
}

void MainWindow::buildActions() {
    struct Spec {
        const char *text;
        QKeySequence::StandardKey key;
        void (MainWindow::*slot)();
    };
    const Spec specs[] = {
        {QT_TR_NOOP("Open Vault…"), QKeySequence::Open, &MainWindow::chooseVault},
        {QT_TR_NOOP("New Note"), QKeySequence::New, &MainWindow::newNote},
        {QT_TR_NOOP("Search…"), QKeySequence::Find, &MainWindow::openSearch},
        {QT_TR_NOOP("Save"), QKeySequence::Save, &MainWindow::saveCurrent},
    };

    m_gearMenu = new QMenu(this);
    auto *settings = m_gearMenu->addAction(tr("Settings…"));
    connect(settings, &QAction::triggered, this, &MainWindow::openSettings);
    m_gearMenu->addSeparator();
    for (const Spec &s : specs) {
        auto *act = new QAction(tr(s.text), this);
        act->setShortcut(s.key);
        connect(act, &QAction::triggered, this, s.slot);
        addAction(act); // keep the shortcut live without a menubar
        m_gearMenu->addAction(act);
    }
    m_gearMenu->addSeparator();
    auto *quit = new QAction(tr("Quit"), this);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    addAction(quit);
    m_gearMenu->addAction(quit);

    // Navigation actions drive both the header arrow buttons and the shortcuts.
    m_backAction = new QAction(QStringLiteral("←"), this);
    m_backAction->setToolTip(tr("Back  (Alt+Left)"));
    m_backAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    connect(m_backAction, &QAction::triggered, this, &MainWindow::navigateBack);
    addAction(m_backAction);
    m_forwardAction = new QAction(QStringLiteral("→"), this);
    m_forwardAction->setToolTip(tr("Forward  (Alt+Right)"));
    m_forwardAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    connect(m_forwardAction, &QAction::triggered, this,
            &MainWindow::navigateForward);
    addAction(m_forwardAction);
    updateNavActions();
}

void MainWindow::loadSettings() {
    QSettings s;
    // With no custom font saved, keep the editor's built-in fallback chain
    // (Inter -> Liberation Sans -> sans-serif) untouched.
    if (!s.contains(QStringLiteral("editorFontFamily")) &&
        !s.contains(QStringLiteral("editorFontSize")))
        return;
    QFont f = m_editor->font();
    if (s.contains(QStringLiteral("editorFontFamily")))
        f.setFamily(s.value(QStringLiteral("editorFontFamily")).toString());
    if (s.contains(QStringLiteral("editorFontSize")))
        f.setPointSize(s.value(QStringLiteral("editorFontSize")).toInt());
    m_editor->applyFont(f);
}

void MainWindow::openSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Settings"));
    auto *form = new QFormLayout(&dlg);

    auto *fontBox = new QFontComboBox(&dlg);
    fontBox->setCurrentFont(m_editor->font());
    auto *sizeBox = new QSpinBox(&dlg);
    sizeBox->setRange(8, 32);
    sizeBox->setValue(m_editor->font().pointSize());

    form->addRow(tr("Editor font"), fontBox);
    form->addRow(tr("Font size"), sizeBox);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // Live preview as the user changes the controls.
    auto preview = [this, fontBox, sizeBox] {
        QFont f = fontBox->currentFont();
        f.setPointSize(sizeBox->value());
        m_editor->applyFont(f);
    };
    connect(fontBox, &QFontComboBox::currentFontChanged, &dlg, preview);
    connect(sizeBox, qOverload<int>(&QSpinBox::valueChanged), &dlg, preview);

    const QFont original = m_editor->font();
    if (dlg.exec() == QDialog::Accepted) {
        QFont f = fontBox->currentFont();
        f.setPointSize(sizeBox->value());
        m_editor->applyFont(f);
        QSettings s;
        s.setValue(QStringLiteral("editorFontFamily"), f.family());
        s.setValue(QStringLiteral("editorFontSize"), f.pointSize());
    } else {
        m_editor->applyFont(original); // revert the live preview
    }
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
    m_titleEdit->blockSignals(true);
    m_titleEdit->clear();
    m_titleEdit->blockSignals(false);

    refreshTree();
    QSettings().setValue(QStringLiteral("lastVault"), path);
    setWindowTitle(QStringLiteral("Emerald — %1").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(
        tr("%1 notes").arg(m_vault->notes().size()), 4000);
}

void MainWindow::refreshTree() {
    m_noteTree->clear();
    if (!m_vault)
        return;
    const QDir rootDir(m_vault->root());
    QHash<QString, QTreeWidgetItem *> folders;

    // Find or build the (possibly nested) folder node for a relative dir path.
    std::function<QTreeWidgetItem *(const QString &)> ensure =
        [&](const QString &rel) -> QTreeWidgetItem * {
        if (rel.isEmpty() || rel == QStringLiteral("."))
            return m_noteTree->invisibleRootItem();
        if (auto it = folders.constFind(rel); it != folders.constEnd())
            return it.value();
        const int slash = rel.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem *parent =
            ensure(slash < 0 ? QString() : rel.left(slash));
        auto *node = new QTreeWidgetItem(parent);
        node->setText(0, slash < 0 ? rel : rel.mid(slash + 1));
        node->setData(0, kDirRole, rootDir.filePath(rel));
        node->setFlags(node->flags() & ~Qt::ItemIsSelectable);
        folders.insert(rel, node);
        return node;
    };

    for (const QString &rel : m_vault->folders())
        ensure(rel);

    QStringList titles;
    for (const Note &n : m_vault->notes()) {
        const QString dirRel =
            rootDir.relativeFilePath(QFileInfo(n.path).absolutePath());
        auto *leaf = new QTreeWidgetItem(ensure(dirRel));
        leaf->setText(0, n.title);
        leaf->setData(0, kPathRole, n.path);
        titles << n.title;
    }
    m_noteTree->expandAll();
    m_editor->setCompletions(titles);
    selectInTree(m_currentPath);
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
    m_titleEdit->blockSignals(true);
    m_titleEdit->setText(m_currentTitle);
    m_titleEdit->blockSignals(false);
    setWindowTitle(QStringLiteral("Emerald — %1").arg(m_currentTitle));
    selectInTree(path);
    if (record)
        pushHistory(path);
    updateNavActions();
}

void MainWindow::renameCurrent(const QString &rawTitle) {
    if (!m_vault || m_currentPath.isEmpty())
        return;
    const QString newTitle = rawTitle.trimmed();
    if (newTitle == m_currentTitle)
        return;

    auto revertField = [this] {
        m_titleEdit->blockSignals(true);
        m_titleEdit->setText(m_currentTitle);
        m_titleEdit->blockSignals(false);
    };
    if (!Vault::isValidTitle(newTitle)) {
        revertField();
        statusBar()->showMessage(tr("Invalid note name"), 3000);
        return;
    }

    saveCurrent(); // flush the body before the file moves
    const QString oldTitle = m_currentTitle;
    const QString oldPath = m_currentPath;
    const QString newPath = m_vault->renameNote(oldPath, newTitle);
    if (newPath.isEmpty()) {
        revertField();
        statusBar()->showMessage(
            tr("A note named “%1” already exists").arg(newTitle), 3000);
        return;
    }

    m_vault->updateLinksTo(oldTitle, newTitle);
    m_currentPath = newPath;
    m_currentTitle = newTitle;
    for (QString &p : m_history)
        if (p == oldPath)
            p = newPath;
    m_searchIndex.rebuild(*m_vault); // paths and link text changed vault-wide
    refreshTree();
    setWindowTitle(QStringLiteral("Emerald — %1").arg(newTitle));
    statusBar()->showMessage(tr("Renamed to “%1”").arg(newTitle), 3000);
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
    refreshTree();
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
        refreshTree();
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

void MainWindow::selectInTree(const QString &path) {
    for (QTreeWidgetItemIterator it(m_noteTree); *it; ++it) {
        if ((*it)->data(0, kPathRole).toString() == path) {
            m_noteTree->setCurrentItem(*it);
            return;
        }
    }
    m_noteTree->clearSelection();
}

void MainWindow::openSearch() {
    if (m_vault)
        m_searchPopup->showCentered();
}

void MainWindow::onTreeItemClicked(QTreeWidgetItem *item, int) {
    const QString path = item->data(0, kPathRole).toString();
    if (!path.isEmpty())
        openNoteByPath(path);
}

void MainWindow::onTreeContextMenu(const QPoint &pos) {
    if (!m_vault)
        return;
    QTreeWidgetItem *item = m_noteTree->itemAt(pos);
    // Target folder: the clicked folder, the folder of the clicked note, or the
    // vault root when clicking empty space.
    QString dir = m_vault->root();
    if (item) {
        const QString folder = item->data(0, kDirRole).toString();
        dir = folder.isEmpty()
                  ? QFileInfo(item->data(0, kPathRole).toString()).absolutePath()
                  : folder;
    }
    QMenu menu(this);
    menu.addAction(tr("New Note"), this, [this, dir] { newNoteIn(dir); });
    menu.addAction(tr("New Folder"), this, [this, dir] { newFolderIn(dir); });
    menu.exec(m_noteTree->viewport()->mapToGlobal(pos));
}

void MainWindow::newNoteIn(const QString &dir) {
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("New Note"), tr("Title:"),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || !Vault::isValidTitle(name))
        return;
    const Note note = m_vault->createNoteIn(dir, name);
    m_vault->scan();
    m_searchIndex.rebuild(*m_vault);
    refreshTree();
    openNoteByPath(note.path);
}

void MainWindow::newFolderIn(const QString &dir) {
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (!m_vault->createFolder(dir, name)) {
        statusBar()->showMessage(tr("Couldn't create that folder"), 3000);
        return;
    }
    m_vault->scan();
    refreshTree();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveCurrent();
    event->accept();
}
