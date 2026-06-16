#include "MainWindow.h"

#include "MarkdownEditor.h"
#include "SearchPopup.h"
#include "core/Vault.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDropEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QHash>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {
constexpr int kPathRole = Qt::UserRole;     // leaf: the note's file path
constexpr int kDirRole = Qt::UserRole + 1;  // folder: its absolute path

QString manualText() {
    return QStringLiteral(
        "Welcome to **Emerald** — a tiny, fast, Obsidian-style note app. This "
        "note is generated; edit or delete it freely.\n"
        "\n"
        "## Writing\n"
        "Markup melts away on every line except the one your cursor is on:\n"
        "\n"
        "- **bold**, *italic*, ***both***, ~~strikethrough~~ and ==highlight==\n"
        "- `inline code` and fenced blocks:\n"
        "\n"
        "```cpp\n"
        "int answer = 42;\n"
        "```\n"
        "\n"
        "> Blockquotes — press Enter to keep quoting, Enter on an empty line to "
        "stop.\n"
        "\n"
        "Horizontal rule:\n"
        "\n"
        "---\n"
        "\n"
        "## Lists & tasks\n"
        "- bullets render as glyphs (Tab / Shift+Tab to indent)\n"
        "  - nested item\n"
        "1. numbered lists auto-increment on Enter\n"
        "- [ ] a task — click the box to toggle\n"
        "- [x] a done task\n"
        "\n"
        "## Tables\n"
        "| Feature | Shortcut |\n"
        "| --- | --- |\n"
        "| Search | Ctrl+F |\n"
        "| Go to note | Ctrl+P |\n"
        "\n"
        "## Links\n"
        "Type `[[` to autocomplete a link. `[[Welcome]]` jumps to a note "
        "(click it when rendered, Ctrl+click while editing). Use "
        "`[[Welcome|alias]]` to show a different label. Renaming a note's title "
        "rewrites links to it.\n"
        "\n"
        "## Getting around\n"
        "- **Title** — the first line is the file name (no `.md`); edit it to "
        "rename.\n"
        "- **Sidebar** — notes live in a folder tree; right-click to create or "
        "delete; single-click a folder to fold it.\n"
        "- **Navigation** — the ← → arrows (Alt+Left / Alt+Right or the mouse "
        "side buttons) walk your history.\n"
        "- **Search** — Ctrl+F searches everything; Ctrl+P jumps by title.\n"
        "- **Settings** — the gear holds font, editor width, new-note folder "
        "and a Home note.\n");
}

// A tree that draws a faint vertical guide for each nesting level, so notes
// inside a folder read clearly as sub-items.
class NoteTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    // Called with (source note/folder path, destination folder path; empty
    // dest = vault root) when an item is dropped.
    std::function<void(const QString &, const QString &)> onMove;

protected:
    void dropEvent(QDropEvent *event) override {
        QTreeWidgetItem *src = currentItem();
        if (!src) {
            event->ignore();
            return;
        }
        const QString dirRole = src->data(0, kDirRole).toString();
        const QString srcPath =
            dirRole.isEmpty() ? src->data(0, kPathRole).toString() : dirRole;
        QString destDir; // empty => root
        if (QTreeWidgetItem *target = itemAt(event->position().toPoint())) {
            const QString d = target->data(0, kDirRole).toString();
            destDir = d.isEmpty()
                          ? QFileInfo(target->data(0, kPathRole).toString())
                                .absolutePath()
                          : d;
        }
        event->acceptProposedAction();
        if (onMove && !srcPath.isEmpty())
            onMove(srcPath, destDir); // MainWindow moves on disk + rebuilds
        // Don't call the base: the tree is rebuilt from the vault instead.
    }

    void drawBranches(QPainter *painter, const QRect &rect,
                      const QModelIndex &index) const override {
        QTreeWidget::drawBranches(painter, rect, index);
        int depth = 0;
        for (QModelIndex a = index.parent(); a.isValid(); a = a.parent())
            ++depth;
        if (depth == 0)
            return;
        const int ind = indentation();
        painter->save();
        painter->setPen(QColor(0x2a, 0x2e, 0x42));
        for (int level = 1; level <= depth; ++level) {
            const int x = rect.right() - ind * (depth - level) - ind / 2;
            painter->drawLine(x, rect.top(), x, rect.bottom());
        }
        painter->restore();
    }
};

// A small themed folder glyph drawn once and reused for every folder row.
const QIcon &folderIcon() {
    static const QIcon icon = [] {
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#7aa2f7"));
        p.drawRoundedRect(QRectF(2, 3.5, 6, 3), 1, 1);   // tab
        p.drawRoundedRect(QRectF(2, 5, 12, 8.5), 1.5, 1.5); // body
        return QIcon(pm);
    }();
    return icon;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    buildActions();
    buildUi();
    loadSettings();
    statusBar()->setSizeGripEnabled(false);

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
    m_centerColumn = new QWidget(this);
    auto *colLayout = new QVBoxLayout(m_centerColumn);
    colLayout->setContentsMargins(0, 0, 0, 0);
    colLayout->setSpacing(0);
    colLayout->addWidget(m_titleEdit);
    colLayout->addWidget(m_editor, 1);
    m_centerColumn->setMaximumWidth(820); // overridden by the saved setting
    m_centerColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *center = new QWidget(this);
    auto *row = new QHBoxLayout(center);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addStretch(0);
    row->addWidget(m_centerColumn, 1);
    row->addStretch(0);

    auto *tree = new NoteTree(this);
    m_noteTree = tree;
    m_noteTree->setHeaderHidden(true);
    m_noteTree->setIndentation(16);
    m_noteTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_noteTree->setDragEnabled(true);
    m_noteTree->setAcceptDrops(true);
    m_noteTree->setDropIndicatorShown(true);
    m_noteTree->setDragDropMode(QAbstractItemView::InternalMove);
    tree->onMove = [this](const QString &src, const QString &dest) {
        moveItem(src, dest);
    };
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
    side->setObjectName(QStringLiteral("sidebar"));
    side->setMinimumWidth(0); // allow the splitter to collapse it fully
    auto *col = new QVBoxLayout(side);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(header);
    col->addWidget(m_noteTree, 1);
    col->addWidget(footer);

    // A splitter so the sidebar can be dragged narrower and collapse fully;
    // dragging the handle back from the left edge reopens it.
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("mainSplitter"));
    m_splitter->addWidget(side);
    m_splitter->addWidget(center);
    m_splitter->setCollapsible(0, true);
    m_splitter->setCollapsible(1, false);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setHandleWidth(2);
    m_splitter->setSizes({260, 900});
    setCentralWidget(m_splitter);
    // Clicking (not dragging) the handle collapses / reopens the sidebar.
    m_splitHandle = m_splitter->handle(1);
    if (m_splitHandle)
        m_splitHandle->installEventFilter(this);

    m_searchPopup = new SearchPopup(&m_searchIndex, this);
    connect(m_searchPopup, &SearchPopup::openRequested, this,
            [this](const QString &path, const QString &query) {
                openNoteByPath(path);
                const QStringList tokens = SearchIndex::tokenize(query);
                if (!tokens.isEmpty())
                    m_editor->jumpToMatch(tokens.first());
            });

    // In-note find bar, floating at the top-right of the editor.
    m_findBar = new QFrame(m_editor);
    m_findBar->setObjectName(QStringLiteral("findBar"));
    auto *fh = new QHBoxLayout(m_findBar);
    fh->setContentsMargins(8, 4, 8, 4);
    fh->setSpacing(4);
    m_findInput = new QLineEdit(m_findBar);
    m_findInput->setObjectName(QStringLiteral("findInput"));
    m_findInput->setPlaceholderText(tr("Find in note…  (Enter / Shift+Enter)"));
    fh->addWidget(m_findInput);
    m_findBar->hide();
    m_findInput->installEventFilter(this);
    m_editor->installEventFilter(this); // reposition the bar on editor resize
    connect(m_findInput, &QLineEdit::textChanged, this, [this] {
        // Incremental: search from the start of the current selection.
        QTextCursor c = m_editor->textCursor();
        c.setPosition(c.selectionStart());
        m_editor->setTextCursor(c);
        findInFile(true);
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
        {QT_TR_NOOP("Save"), QKeySequence::Save, &MainWindow::saveCurrent},
    };

    m_gearMenu = new QMenu(this);
    auto *settings = m_gearMenu->addAction(tr("Settings…"));
    connect(settings, &QAction::triggered, this, &MainWindow::openSettings);
    auto *manual = m_gearMenu->addAction(tr("Manual"));
    connect(manual, &QAction::triggered, this, &MainWindow::openManual);
    m_gearMenu->addSeparator();
    for (const Spec &s : specs) {
        auto *act = new QAction(tr(s.text), this);
        act->setShortcut(s.key);
        connect(act, &QAction::triggered, this, s.slot);
        addAction(act); // keep the shortcut live without a menubar
        m_gearMenu->addAction(act);
    }
    // Find in the current note (Ctrl+F) and global vault search (Ctrl+Shift+F).
    auto *findHere = new QAction(tr("Find in Note…"), this);
    findHere->setShortcut(QKeySequence::Find); // Ctrl+F
    connect(findHere, &QAction::triggered, this, &MainWindow::openFindInFile);
    addAction(findHere);
    m_gearMenu->addAction(findHere);
    auto *search = new QAction(tr("Search Vault…"), this);
    search->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(search, &QAction::triggered, this, &MainWindow::openSearch);
    addAction(search);
    m_gearMenu->addAction(search);
    // Quick "go to note" picker (titles only).
    auto *goTo = new QAction(tr("Go to Note…"), this);
    goTo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(goTo, &QAction::triggered, this, &MainWindow::openQuickOpen);
    addAction(goTo);
    m_gearMenu->addAction(goTo);
    m_gearMenu->addSeparator();
    auto *quit = new QAction(tr("Quit"), this);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    addAction(quit);
    m_gearMenu->addAction(quit);

    // Navigation actions drive both the header arrow buttons and the shortcuts.
    // Two bindings each: Alt+Arrow (browser-style) and Ctrl+[ / Ctrl+] (editor
    // style, like VS Code / Vim jumplists).
    m_backAction = new QAction(QStringLiteral("←"), this);
    m_backAction->setToolTip(tr("Back  (Alt+Left or Ctrl+[)"));
    m_backAction->setShortcuts({QKeySequence(Qt::ALT | Qt::Key_Left),
                                QKeySequence(Qt::CTRL | Qt::Key_BracketLeft)});
    connect(m_backAction, &QAction::triggered, this, &MainWindow::navigateBack);
    addAction(m_backAction);
    m_forwardAction = new QAction(QStringLiteral("→"), this);
    m_forwardAction->setToolTip(tr("Forward  (Alt+Right or Ctrl+])"));
    m_forwardAction->setShortcuts({QKeySequence(Qt::ALT | Qt::Key_Right),
                                   QKeySequence(Qt::CTRL | Qt::Key_BracketRight)});
    connect(m_forwardAction, &QAction::triggered, this,
            &MainWindow::navigateForward);
    addAction(m_forwardAction);
    updateNavActions();
}

void MainWindow::loadSettings() {
    QSettings s;
    m_centerColumn->setMaximumWidth(
        s.value(QStringLiteral("editorWidth"), 820).toInt());
    const QByteArray split = s.value(QStringLiteral("splitterState")).toByteArray();
    if (!split.isEmpty())
        m_splitter->restoreState(split);
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

    QSettings s;

    auto *fontBox = new QFontComboBox(&dlg);
    fontBox->setCurrentFont(m_editor->font());
    auto *sizeBox = new QSpinBox(&dlg);
    sizeBox->setRange(8, 32);
    sizeBox->setValue(m_editor->font().pointSize());
    auto *widthBox = new QSpinBox(&dlg);
    widthBox->setRange(500, 1600);
    widthBox->setSingleStep(20);
    widthBox->setSuffix(tr(" px"));
    widthBox->setValue(m_centerColumn->maximumWidth());

    // New-note folder + Home note pickers (need an open vault).
    auto *folderBox = new QComboBox(&dlg);
    auto *homeBox = new QComboBox(&dlg);
    folderBox->addItem(tr("(Vault root)"), QString());
    homeBox->addItem(tr("(None)"), QString());
    if (m_vault) {
        const QDir root(m_vault->root());
        for (const QString &rel : m_vault->folders())
            folderBox->addItem(rel, rel);
        for (const Note &n : m_vault->notes())
            homeBox->addItem(n.title, root.relativeFilePath(n.path));
        const int fi = folderBox->findData(s.value(QStringLiteral("newNoteFolder")));
        if (fi >= 0)
            folderBox->setCurrentIndex(fi);
        const int hi = homeBox->findData(s.value(QStringLiteral("homeNote")));
        if (hi >= 0)
            homeBox->setCurrentIndex(hi);
    } else {
        folderBox->setEnabled(false);
        homeBox->setEnabled(false);
    }

    form->addRow(tr("Editor font"), fontBox);
    form->addRow(tr("Font size"), sizeBox);
    form->addRow(tr("Editor width"), widthBox);
    form->addRow(tr("New notes in"), folderBox);
    form->addRow(tr("Home note"), homeBox);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // Live preview of font + width as the user changes the controls.
    auto preview = [this, fontBox, sizeBox, widthBox] {
        QFont f = fontBox->currentFont();
        f.setPointSize(sizeBox->value());
        m_editor->applyFont(f);
        m_centerColumn->setMaximumWidth(widthBox->value());
    };
    connect(fontBox, &QFontComboBox::currentFontChanged, &dlg, preview);
    connect(sizeBox, qOverload<int>(&QSpinBox::valueChanged), &dlg, preview);
    connect(widthBox, qOverload<int>(&QSpinBox::valueChanged), &dlg, preview);

    const QFont originalFont = m_editor->font();
    const int originalWidth = m_centerColumn->maximumWidth();
    if (dlg.exec() == QDialog::Accepted) {
        QFont f = fontBox->currentFont();
        f.setPointSize(sizeBox->value());
        m_editor->applyFont(f);
        m_centerColumn->setMaximumWidth(widthBox->value());
        s.setValue(QStringLiteral("editorFontFamily"), f.family());
        s.setValue(QStringLiteral("editorFontSize"), f.pointSize());
        s.setValue(QStringLiteral("editorWidth"), widthBox->value());
        if (m_vault) {
            s.setValue(QStringLiteral("newNoteFolder"), folderBox->currentData());
            s.setValue(QStringLiteral("homeNote"), homeBox->currentData());
        }
    } else {
        m_editor->applyFont(originalFont); // revert the live preview
        m_centerColumn->setMaximumWidth(originalWidth);
    }
}

void MainWindow::openManual() {
    if (!m_vault) {
        chooseVault();
        if (!m_vault)
            return;
    }
    const QString title = QStringLiteral("Emerald Manual");
    QString path = m_vault->pathForTitle(title);
    if (path.isEmpty()) {
        const Note note = m_vault->createNote(title);
        m_vault->write(note.path, manualText());
        path = note.path;
        m_vault->scan();
        m_searchIndex.rebuild(*m_vault);
        refreshTree();
    }
    openNoteByPath(path);
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
    openInitialNote();
}

void MainWindow::openInitialNote() {
    if (!m_vault)
        return;
    QSettings s;
    // A configured Home note wins; otherwise reopen the last-edited note.
    const QString home = s.value(QStringLiteral("homeNote")).toString();
    if (!home.isEmpty()) {
        const QString p = QDir(m_vault->root()).filePath(home);
        if (QFileInfo::exists(p)) {
            openNoteByPath(p);
            return;
        }
    }
    const QString last = s.value(QStringLiteral("lastNote")).toString();
    if (!last.isEmpty() && QFileInfo::exists(last) &&
        last.startsWith(m_vault->root()))
        openNoteByPath(last);
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
        node->setIcon(0, folderIcon());
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
    QSettings().setValue(QStringLiteral("lastNote"), path); // reopen on launch
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
    // Create in the configured folder (default: vault root), falling back to
    // the root if the saved folder no longer exists.
    QString dir = m_vault->root();
    const QString rel = QSettings().value(QStringLiteral("newNoteFolder")).toString();
    if (!rel.isEmpty()) {
        const QString candidate = QDir(m_vault->root()).filePath(rel);
        if (QDir(candidate).exists())
            dir = candidate;
    }
    newNoteIn(dir);
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
        m_searchPopup->showCentered(false);
}

void MainWindow::openQuickOpen() {
    if (m_vault)
        m_searchPopup->showCentered(true); // titles only
}

void MainWindow::openFindInFile() {
    positionFindBar();
    m_findBar->show();
    m_findBar->raise();
    m_findInput->setFocus();
    m_findInput->selectAll();
}

void MainWindow::findInFile(bool forward) {
    const QString text = m_findInput->text();
    if (text.isEmpty())
        return;
    QTextDocument::FindFlags flags;
    if (!forward)
        flags |= QTextDocument::FindBackward;
    if (!m_editor->find(text, flags)) { // wrap around
        QTextCursor c = m_editor->textCursor();
        c.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
        m_editor->setTextCursor(c);
        m_editor->find(text, flags);
    }
}

void MainWindow::positionFindBar() {
    if (!m_findBar)
        return;
    m_findBar->adjustSize();
    const int w = qMin(320, m_editor->width() - 24);
    m_findBar->setFixedWidth(w);
    m_findBar->move(m_editor->width() - w - 14, 8);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_findInput && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            m_findBar->hide();
            m_editor->setFocus();
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            findInFile(!(ke->modifiers() & Qt::ShiftModifier));
            return true;
        }
    } else if (watched == m_editor && event->type() == QEvent::Resize &&
               m_findBar && m_findBar->isVisible()) {
        positionFindBar();
    } else if (watched == m_splitHandle) {
        // A click (press + release without a drag) toggles the sidebar; a real
        // drag is left to the splitter.
        if (event->type() == QEvent::MouseButtonPress) {
            m_handlePressPos = static_cast<QMouseEvent *>(event)->globalPosition()
                                   .toPoint();
        } else if (event->type() == QEvent::MouseButtonRelease) {
            const QPoint up = static_cast<QMouseEvent *>(event)->globalPosition()
                                  .toPoint();
            if ((up - m_handlePressPos).manhattanLength() < 4) {
                const QList<int> sizes = m_splitter->sizes();
                const int total = sizes.value(0) + sizes.value(1);
                if (sizes.value(0) > 0)
                    m_splitter->setSizes({0, total}); // collapse
                else
                    m_splitter->setSizes({220, total - 220}); // reopen at min
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onTreeItemClicked(QTreeWidgetItem *item, int) {
    const QString path = item->data(0, kPathRole).toString();
    if (!path.isEmpty()) {
        openNoteByPath(path);
        return;
    }
    // A single click on a folder row folds / unfolds it.
    if (!item->data(0, kDirRole).toString().isEmpty())
        item->setExpanded(!item->isExpanded());
}

void MainWindow::onTreeContextMenu(const QPoint &pos) {
    if (!m_vault)
        return;
    QTreeWidgetItem *item = m_noteTree->itemAt(pos);
    const QString notePath = item ? item->data(0, kPathRole).toString() : QString();
    const QString folderPath = item ? item->data(0, kDirRole).toString() : QString();

    // Where "New" creates: the clicked folder, the clicked note's folder, or
    // the vault root for empty space.
    QString dir = m_vault->root();
    if (!folderPath.isEmpty())
        dir = folderPath;
    else if (!notePath.isEmpty())
        dir = QFileInfo(notePath).absolutePath();

    QMenu menu(this);
    menu.addAction(tr("New Note"), this, [this, dir] { newNoteIn(dir); });
    menu.addAction(tr("New Folder"), this, [this, dir] { newFolderIn(dir); });
    if (!notePath.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Delete Note"), this,
                       [this, notePath] { deleteEntry(notePath, false); });
    } else if (!folderPath.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Delete Folder"), this,
                       [this, folderPath] { deleteEntry(folderPath, true); });
    }
    menu.exec(m_noteTree->viewport()->mapToGlobal(pos));
}

void MainWindow::deleteEntry(const QString &path, bool isFolder) {
    const QString name =
        isFolder ? QFileInfo(path).fileName() : Vault::titleFromPath(path);
    const QString question =
        isFolder
            ? tr("Delete the folder “%1” and everything inside it?").arg(name)
            : tr("Delete the note “%1”?").arg(name);
    if (QMessageBox::question(this, tr("Delete"), question) != QMessageBox::Yes)
        return;
    if (!m_vault->remove(path)) {
        statusBar()->showMessage(tr("Couldn't delete “%1”").arg(name), 3000);
        return;
    }

    // Clear settings that pointed at the deleted note/folder so they don't go
    // stale (Home note / new-note folder).
    {
        QSettings s;
        const QString rel = QDir(m_vault->root()).relativeFilePath(path);
        const QString home = s.value(QStringLiteral("homeNote")).toString();
        const QString nf = s.value(QStringLiteral("newNoteFolder")).toString();
        if (home == rel || (isFolder && home.startsWith(rel + QLatin1Char('/'))))
            s.remove(QStringLiteral("homeNote"));
        if (isFolder &&
            (nf == rel || nf.startsWith(rel + QLatin1Char('/'))))
            s.remove(QStringLiteral("newNoteFolder"));
    }

    // If the open note was removed (on its own or inside a deleted folder),
    // clear the editor and prune it from history.
    const bool currentGone =
        !m_currentPath.isEmpty() && !QFileInfo::exists(m_currentPath);
    m_vault->scan();
    m_searchIndex.rebuild(*m_vault);
    if (currentGone) {
        m_currentPath.clear();
        m_currentTitle.clear();
        m_history.erase(
            std::remove_if(m_history.begin(), m_history.end(),
                           [](const QString &p) { return !QFileInfo::exists(p); }),
            m_history.end());
        m_histIndex = m_history.size() - 1;
        updateNavActions();
        refreshTree();

        // Show the Home note, else the most recent still-open note, else blank.
        QString fallback;
        const QString home = QSettings().value(QStringLiteral("homeNote")).toString();
        if (!home.isEmpty()) {
            const QString p = QDir(m_vault->root()).filePath(home);
            if (QFileInfo::exists(p))
                fallback = p;
        }
        if (fallback.isEmpty() && !m_history.isEmpty())
            fallback = m_history.last();
        if (!fallback.isEmpty()) {
            openNoteByPath(fallback);
        } else {
            m_loading = true;
            m_editor->clear();
            m_loading = false;
            m_titleEdit->blockSignals(true);
            m_titleEdit->clear();
            m_titleEdit->blockSignals(false);
            setWindowTitle(QStringLiteral("Emerald"));
        }
    } else {
        refreshTree();
    }
    statusBar()->showMessage(tr("Deleted “%1”").arg(name), 3000);
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

void MainWindow::moveItem(const QString &srcPath, const QString &destDirIn) {
    if (!m_vault)
        return;
    const QString destDir = destDirIn.isEmpty() ? m_vault->root() : destDirIn;
    const QString newPath = m_vault->movePath(srcPath, destDir);
    if (newPath.isEmpty()) {
        statusBar()->showMessage(tr("Couldn't move it there"), 3000);
        return;
    }
    // Follow the open note / history if they lived in what just moved.
    auto remap = [&](QString &p) {
        if (p == srcPath)
            p = newPath;
        else if (p.startsWith(srcPath + QLatin1Char('/')))
            p = newPath + p.mid(srcPath.length());
    };
    remap(m_currentPath);
    for (QString &p : m_history)
        remap(p);

    m_vault->scan();
    m_searchIndex.rebuild(*m_vault);
    refreshTree();
    if (!m_currentPath.isEmpty()) {
        setWindowTitle(
            QStringLiteral("Emerald — %1").arg(Vault::titleFromPath(m_currentPath)));
        selectInTree(m_currentPath);
        QSettings().setValue(QStringLiteral("lastNote"), m_currentPath);
    }
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
    QSettings().setValue(QStringLiteral("splitterState"), m_splitter->saveState());
    event->accept();
}
