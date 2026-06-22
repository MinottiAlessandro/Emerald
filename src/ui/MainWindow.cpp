#include "MainWindow.h"

#include "MarkdownEditor.h"
#include "Mascot.h"
#include "SearchPopup.h"
#include "Updater.h"
#include "core/MascotSeed.h"
#include "core/Vault.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDropEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVariant>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {
constexpr int kPathRole = Qt::UserRole;     // leaf: the note's file path
constexpr int kDirRole = Qt::UserRole + 1;  // folder: its absolute path

// Keep only paths that aren't nested inside another path in the list — moving
// or deleting a folder already carries its contents, so handling a child too
// would just hit a stale path.
QStringList topLevelPaths(const QStringList &paths) {
    QStringList roots;
    for (const QString &p : paths) {
        bool nested = false;
        for (const QString &q : paths)
            if (p != q && p.startsWith(q + QLatin1Char('/'))) {
                nested = true;
                break;
            }
        if (!nested)
            roots << p;
    }
    return roots;
}

// Translate the subset of Moment.js date/time tokens the templates feature
// documents into Qt's QDateTime format. The numeric tokens differ only in case
// between the two: Moment writes the year as Y and the day-of-month as D, where
// Qt uses y and d. Month (M), hour (H/h), minute (m) and second (s) already
// match, so a plain case-fold of Y→y and D→d covers YYYY-MM-DD, HH:mm and the
// like. A format already written in Qt's lowercase style passes through intact.
QString momentToQtDateFormat(QString fmt) {
    fmt.replace(QLatin1Char('Y'), QLatin1Char('y'));
    fmt.replace(QLatin1Char('D'), QLatin1Char('d'));
    return fmt;
}

// Replace {{date}}, {{time}} and {{title}} (each optionally "{{date:FORMAT}}")
// with their values. date/time take a Moment.js-style format string after a
// colon; with none they default to YYYY-MM-DD and HH:mm. title ignores any
// format. Matching is case-insensitive ({{Date}} works too).
QString expandTemplateTokens(const QString &content, const QString &title) {
    static const QRegularExpression re(
        QStringLiteral("\\{\\{\\s*(date|time|title)\\s*(?::([^}]*))?\\s*\\}\\}"),
        QRegularExpression::CaseInsensitiveOption);
    const QDateTime now = QDateTime::currentDateTime();
    QString out;
    int last = 0;
    auto it = re.globalMatch(content);
    while (it.hasNext()) {
        const auto m = it.next();
        out += content.mid(last, m.capturedStart(0) - last);
        const QString name = m.captured(1).toLower();
        const QString fmt = m.captured(2).trimmed();
        if (name == QStringLiteral("title"))
            out += title;
        else if (name == QStringLiteral("date"))
            out += now.toString(fmt.isEmpty() ? QStringLiteral("yyyy-MM-dd")
                                              : momentToQtDateFormat(fmt));
        else // time
            out += now.toString(fmt.isEmpty() ? QStringLiteral("HH:mm")
                                              : momentToQtDateFormat(fmt));
        last = m.capturedEnd(0);
    }
    out += content.mid(last);
    return out;
}

QString manualText() {
    return QStringLiteral(
        "Welcome to **Emerald** — a tiny, fast, Obsidian-style note app. This "
        "note shows everything the editor can do; edit or delete it freely.\n"
        "\n"
        "Emerald is a *live* Markdown editor — there is no separate preview "
        "pane. The markup on the line your cursor sits on stays visible so you "
        "can edit it, and melts into formatted text on every other line.\n"
        "\n"
        "## Text formatting\n"
        "- **bold** — wrap text in `**double asterisks**`\n"
        "- *italic* — wrap text in `*single asterisks*` or `_underscores_`\n"
        "- ***bold italic*** — combine them: `***three asterisks***`\n"
        "- ~~strikethrough~~ — wrap text in `~~tildes~~`\n"
        "- ==highlight== — wrap text in `==double equals==`\n"
        "- `inline code` — wrap text in single backticks\n"
        "\n"
        "These styles stack: nest them to layer more on, e.g. "
        "`==dog ~~cat *horse **elephant***~~==` highlights everything, strikes "
        "“cat horse elephant”, italicises “horse elephant”, and bolds "
        "“elephant”.\n"
        "\n"
        "## Headings\n"
        "Start a line with one to six `#` marks; the more marks, the smaller the "
        "heading. Hover the left margin beside a heading and click the ▾ arrow to "
        "fold its whole section away — click the ▸ to unfold it.\n"
        "\n"
        "### This is a third-level heading\n"
        "\n"
        "## Lists\n"
        "- bullets start with `-`, `*` or `+`\n"
        "  - press Tab to indent, Shift+Tab to outdent\n"
        "    - the bullet glyph changes with the nesting depth\n"
        "- Enter keeps the list going; Enter on an empty item ends it\n"
        "\n"
        "1. ordered lists start with `1.` or `1)`\n"
        "2. the next number is filled in for you on Enter\n"
        "\n"
        "## Tasks\n"
        "- [ ] an open task — type `- [ ] ` before the text\n"
        "- [x] a finished task — `- [x] ` (the text is struck through)\n"
        "\n"
        "Click a checkbox on any line other than the one you're editing to "
        "toggle it.\n"
        "\n"
        "## Quotes\n"
        "> Blockquotes start with `>`.\n"
        "> Enter keeps quoting; Enter on an empty quote line stops.\n"
        "\n"
        "## Code blocks\n"
        "Fence a block between lines of three backticks (or three tildes) and "
        "add a language name for a labelled header bar — click the copy icon on "
        "the right of that bar to copy the whole block:\n"
        "\n"
        "```cpp\n"
        "int answer = 42;  // the header bar shows the language\n"
        "return answer;\n"
        "```\n"
        "\n"
        "## Tables\n"
        "Type a pipe table and Emerald lines the columns up as you go — on every "
        "Tab and when you click away. Colons in the separator row set the "
        "alignment — `:--` left, `:-:` centre, `--:` right.\n"
        "\n"
        "Press **Enter** on a header row (the first line, before there's a "
        "`---` separator) and Emerald adds the separator and a first data row for "
        "you, dropping the caret in the first cell.\n"
        "\n"
        "Press **Tab** inside a table to jump to the next cell (**Shift+Tab** "
        "goes back). Tab on the last header cell adds a column; on the separator "
        "row it starts a new data row; on the last cell of the last row it adds a "
        "row — so you can build a whole table without leaving the keyboard. "
        "**Enter** on the last row leaves the table, on a fresh line below.\n"
        "\n"
        "| Feature      | Shortcut     |\n"
        "| :----------- | -----------: |\n"
        "| Find in note | Ctrl+F       |\n"
        "| Search vault | Ctrl+Shift+F |\n"
        "| Go to note   | Ctrl+P       |\n"
        "\n"
        "## Templates\n"
        "Pick a **Templates folder** inside your vault under **Settings**, then "
        "press **Ctrl+T** (or **Insert Template…** in the gear menu) to choose a "
        "template — every note under that folder, sub-folders included, is "
        "offered in a quick picker like *Go to note*. The template's text is "
        "dropped in at your cursor.\n"
        "\n"
        "Templates can carry placeholders that fill themselves in on insert:\n"
        "\n"
        "- `{{title}}` — the current note's title\n"
        "- `{{date}}` — today's date (default `YYYY-MM-DD`)\n"
        "- `{{time}}` — the current time (default `HH:mm`)\n"
        "\n"
        "Give `{{date}}` or `{{time}}` a format after a colon to change it, e.g. "
        "`{{date:YYYY/MM/DD}}` or `{{time:HH:mm:ss}}`.\n"
        "\n"
        "## Horizontal rule\n"
        "Three or more dashes on a line of their own draw a divider:\n"
        "\n"
        "---\n"
        "\n"
        "## Editing shortcuts\n"
        "Handy keys while writing (on macOS, Ctrl is ⌘):\n"
        "\n"
        "- **Ctrl+B** / **Ctrl+I** — bold / italic the selection\n"
        "- **Ctrl+K** — wrap the selection as a link `[text](…)`\n"
        "- **Ctrl+1** … **Ctrl+6** — set the line's heading level (press the "
        "same level again to clear it)\n"
        "- **Ctrl+L** — select the whole line\n"
        "- **Alt+↑** / **Alt+↓** — move the line (or selection) up / down\n"
        "- **Tab** / **Shift+Tab** — indent / outdent the selected lines (or the "
        "current list item)\n"
        "- **Ctrl+Enter** — start a new line below without splitting the current "
        "one (keeps continuing a list)\n"
        "- Select text, then press **(** **[** **\\*** **_** **=** **'** **\"** "
        "**`** or **~** to wrap the selection in it (brackets close with their "
        "match)\n"
        "\n"
        "## Linking notes\n"
        "Type `[[` to autocomplete a link to another note. `[[Emerald Manual]]` "
        "jumps to a note — click it once rendered, or Ctrl+click while editing — "
        "and a note that doesn't exist yet is created on the spot. Use "
        "`[[Note|label]]` to show a different label. Renaming a note's title "
        "rewrites every link that points to it.\n"
        "\n"
        "## Getting around\n"
        "- **Title** — the first line above the body is the file name (without "
        "`.md`); edit it to rename the note.\n"
        "- **Sidebar** — notes live in a folder tree. Right-click to create or "
        "delete notes and folders, drag to move them, Shift/Ctrl-click to select "
        "several at once, and single-click a folder to fold it.\n"
        "- **History** — the back / forward arrows (Alt+Left / Alt+Right "
        "or the mouse side buttons) walk back and forward through the notes "
        "you've opened.\n"
        "- **Find in note** — Ctrl+F opens a find bar; Enter and Shift+Enter step "
        "through the matches.\n"
        "- **Search vault** — Ctrl+Shift+F searches the text of every note; "
        "Ctrl+P jumps to a note by title.\n"
        "\n"
        "## Settings\n"
        "Open the gear in the bottom-left for **Settings**: the editor font, its "
        "size and width, the line spacing between rows, the folder new notes are "
        "created in, and a Home note to open at launch. The same menu has **New "
        "Vault…** to start a fresh vault, **Delete Note** to remove the open one "
        "(it asks first), and **Check for Updates…** to fetch and install the "
        "latest release. **Switch Vault…** jumps between vaults in the same "
        "folder, and **Insert Template…** drops in a template — both live in the "
        "menu. Edits save themselves a moment after you stop typing — Ctrl+S "
        "forces a save.\n"
        "\n"
        "## Other shortcuts\n"
        "More keys to control the app workflow (on macOS, Ctrl is ⌘):\n"
        "- **Ctrl+O** — Open a Vault\n"
        "- **Ctrl+Shift+O** — Quick-switch to another vault in the same folder\n"
        "- **Ctrl+N** — Create a new file\n"
        "- **Ctrl+T** — Insert a template at the cursor\n"
        "- **F2** — Rename the current note\n"
        "- **Ctrl+Shift+Backspace** — Delete the current note (asks first)\n"
        "- **Ctrl+S** — Save the current file (Emerald has auto-save)\n"
        "- **Ctrl+F** — Perform a file search\n"
        "- **Ctrl+Shift+F** — Perform a Vault search\n"
        "- **Ctrl+P** — Open the file picker\n"
        "- **Ctrl+,** — Open Settings\n"
        "- **Ctrl+Q** — Close Emerald\n"
        "- **Ctrl++** / **Ctrl+-** — Increase / decrease the font size "
        "(**Ctrl+0** resets it)\n"
        "- **Alt+←** — Back in the history\n"
        "- **Alt+→** — Next in the history\n");
}

// A tree that draws a faint vertical guide for each nesting level, so notes
// inside a folder read clearly as sub-items.
class NoteTree : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    // Called with (source note/folder paths, destination folder path; empty
    // dest = vault root) when a selection is dropped.
    std::function<void(const QStringList &, const QString &)> onMove;

protected:
    void dropEvent(QDropEvent *event) override {
        // The whole current selection moves together.
        QStringList srcPaths;
        for (QTreeWidgetItem *it : selectedItems()) {
            const QString dirRole = it->data(0, kDirRole).toString();
            const QString p =
                dirRole.isEmpty() ? it->data(0, kPathRole).toString() : dirRole;
            if (!p.isEmpty())
                srcPaths << p;
        }
        if (srcPaths.isEmpty()) {
            event->ignore();
            return;
        }
        const QStringList roots = topLevelPaths(srcPaths);
        QString destDir; // empty => root
        if (QTreeWidgetItem *target = itemAt(event->position().toPoint())) {
            const QString d = target->data(0, kDirRole).toString();
            destDir = d.isEmpty()
                          ? QFileInfo(target->data(0, kPathRole).toString())
                                .absolutePath()
                          : d;
        }
        // Accept the drop, but run the actual move *after* the view's own
        // drag-and-drop machinery has finished. In InternalMove mode
        // QAbstractItemView removes the dragged rows once startDrag()'s nested
        // exec() returns; if we move the files and rebuild the tree synchronously
        // here (still inside that exec), that post-drag removal deletes the
        // freshly rebuilt rows instead — so the notes vanish until the next
        // refresh. Deferring to the event loop makes the rebuild-from-disk the
        // last thing to run, so it always wins. CopyAction is a belt-and-braces
        // hint that we handled the move and the view shouldn't also remove it.
        event->setDropAction(Qt::CopyAction);
        event->accept();
        if (onMove) {
            const QString d = destDir;
            QTimer::singleShot(0, this, [this, roots, d] {
                if (onMove)
                    onMove(roots, d); // MainWindow moves on disk + rebuilds
            });
        }
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
        painter->setPen(QColor(0x1f, 0x47, 0x33));
        for (int level = 1; level <= depth; ++level) {
            const int x = rect.right() - ind * (depth - level) - ind / 2;
            painter->drawLine(x, rect.top(), x, rect.bottom());
        }
        painter->restore();
    }
};

// A small chevron drawn in place of a folder glyph: it points up when the
// folder is expanded and down when it's collapsed. Drawn once and reused.
QIcon makeChevron(bool up) {
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor("#52b58a"));
    pen.setWidthF(1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    const QPointF upPts[] = {{4.5, 10.0}, {8.0, 5.5}, {11.5, 10.0}};
    const QPointF downPts[] = {{4.5, 6.0}, {8.0, 10.5}, {11.5, 6.0}};
    p.drawPolyline(up ? upPts : downPts, 3);
    return QIcon(pm);
}
const QIcon &chevronIcon(bool expanded) {
    static const QIcon up = makeChevron(true);
    static const QIcon down = makeChevron(false);
    return expanded ? up : down;
}

// Chrome-style back/forward arrow: a horizontal shaft tipped with an arrowhead,
// drawn thin with rounded ends to mirror the browser toolbar glyphs.
QIcon makeNavArrow(bool back) {
    QPixmap pm(22, 22);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor("#a9c8b8"));
    pen.setWidthF(2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(5, 11), QPointF(17, 11)); // shaft
    const QPointF backHead[] = {{10.5, 5.5}, {5, 11}, {10.5, 16.5}};
    const QPointF fwdHead[] = {{11.5, 5.5}, {17, 11}, {11.5, 16.5}};
    p.drawPolyline(back ? backHead : fwdHead, 3); // arrowhead
    return QIcon(pm);
}

// Where the vault file dialogs should start: alongside the last opened vault,
// or the home folder when there isn't one (or it has since been removed).
QString vaultStartDir() {
    const QString last = QSettings().value(QStringLiteral("lastVault")).toString();
    if (!last.isEmpty()) {
        const QString parent = QFileInfo(last).absolutePath();
        if (QDir(parent).exists())
            return parent;
    }
    return QDir::homePath();
}

// The mascot seed encoded in a note file's first line, or 0. Reads only the
// first line so the gallery can scan a whole vault cheaply.
quint64 mascotSeedInFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray first = f.readLine(256); // the header line is short
    return MascotSeed::fromLine(QString::fromUtf8(first).trimmed());
}

// The user-creature kind on that same first line, or empty.
QString mascotKindInFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray first = f.readLine(256);
    return MascotSeed::kindFromLine(QString::fromUtf8(first).trimmed());
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    buildActions();
    buildUi();
    loadSettings();
    loadCursorPositions(); // remembered per-note caret positions, across restarts

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(700);
    connect(m_saveTimer, &QTimer::timeout, this, &MainWindow::saveCurrent);

    // Watch the open note's file so edits from another program are noticed
    // instead of being silently overwritten by our buffer; watch the vault's
    // folders too, so notes added/removed/renamed elsewhere reach the tree.
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
            &MainWindow::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &MainWindow::onVaultDirChanged);
    // Folder-change events arrive in bursts (a sync client touching many files,
    // an editor's replace-and-rename); coalesce them into one rescan.
    m_rescanTimer = new QTimer(this);
    m_rescanTimer->setSingleShot(true);
    m_rescanTimer->setInterval(250);
    connect(m_rescanTimer, &QTimer::timeout, this, [this] {
        if (!m_vault)
            return;
        m_vault->scan();
        m_searchIndex.rebuild(*m_vault);
        refreshTree(); // keeps the open note selected and folders expanded
        // A backup-rename save (e.g. Vim) swaps the note's inode, dropping the
        // file watch with no further fileChanged — so reconcile the open note
        // here too, or it would stay stale until reopened.
        syncOpenNoteFromDisk();
    });
    // A save fires a burst of file events (truncate, write, rename); coalesce
    // them so we read the file once it has settled, never mid-write.
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(150);
    connect(m_reloadTimer, &QTimer::timeout, this,
            &MainWindow::syncOpenNoteFromDisk);
    connect(m_editor, &MarkdownEditor::textChanged, this, [this] {
        if (!m_loading) {
            m_saveTimer->start();
            maybeAutoGenerateMascot();
        }
    });

    const QString last = QSettings().value(QStringLiteral("lastVault")).toString();
    if (!last.isEmpty() && QDir(last).exists())
        openVault(last);
    else
        // Deferred so the editor is laid out first; the toast positions itself
        // relative to the editor's size, which isn't known until then.
        QTimer::singleShot(0, this, [this] {
            notify(tr("Open a vault to begin  (Ctrl+O)"), 6000);
        });
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
    connect(m_editor, &MarkdownEditor::noticeRequested, this,
            [this](const QString &text) { notify(text, 2000); });
    // A right-click menu on the editor with the usual edit actions plus a
    // working "Delete Note" (the standard menu's Delete only acts on a text
    // selection, so it reads as permanently disabled).
    m_editor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_editor, &QWidget::customContextMenuRequested, this,
            &MainWindow::onEditorContextMenu);

    // The note's title is shown (and edited) as the first line above the body;
    // it maps to the filename, so the ".md" is never shown. Committing an edit
    // renames the file.
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setObjectName(QStringLiteral("noteTitle"));
    m_titleEdit->setPlaceholderText(tr("Untitled"));
    m_titleEdit->setFrame(false);
    connect(m_titleEdit, &QLineEdit::editingFinished, this,
            [this] { renameCurrent(m_titleEdit->text()); });
    // Enter on the title drops the caret onto the first body line, ready to type.
    connect(m_titleEdit, &QLineEdit::returnPressed, this, [this] {
        QTextCursor c = m_editor->textCursor();
        c.setPosition(m_editor->firstContentPosition()); // skip a hidden header line
        m_editor->setTextCursor(c);
        m_editor->setFocus();
    });

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
    m_centerPane = center;
    center->installEventFilter(this); // re-pin the mascot when the pane resizes
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
    // Shift-click for a range, Ctrl-click to toggle individual rows.
    m_noteTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->onMove = [this](const QStringList &srcs, const QString &dest) {
        moveItems(srcs, dest);
    };
    connect(m_noteTree, &QTreeWidget::itemClicked, this,
            &MainWindow::onTreeItemClicked);
    connect(m_noteTree, &QTreeWidget::customContextMenuRequested, this,
            &MainWindow::onTreeContextMenu);
    // Folders carry an up/down chevron instead of a folder icon; keep it in
    // sync with the fold state.
    connect(m_noteTree, &QTreeWidget::itemExpanded, this,
            [](QTreeWidgetItem *it) {
                if (!it->data(0, kDirRole).toString().isEmpty())
                    it->setIcon(0, chevronIcon(true));
            });
    connect(m_noteTree, &QTreeWidget::itemCollapsed, this,
            [](QTreeWidgetItem *it) {
                if (!it->data(0, kDirRole).toString().isEmpty())
                    it->setIcon(0, chevronIcon(false));
            });

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
    backBtn->setIconSize(QSize(22, 22));
    auto *fwdBtn = new QToolButton(header);
    fwdBtn->setObjectName(QStringLiteral("navButton"));
    fwdBtn->setDefaultAction(m_forwardAction);
    fwdBtn->setIconSize(QSize(22, 22));
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
    // Current version, right-aligned on the gear's row.
    auto *version = new QLabel(QStringLiteral("v%1").arg(QApplication::applicationVersion()),
                               footer);
    version->setObjectName(QStringLiteral("versionLabel"));
    version->setToolTip(tr("Emerald version"));
    frow->addWidget(version);

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
    // A wide handle for an easy drag/click target; the QSS paints only a thin
    // line inside it so the divider still looks slim.
    m_splitter->setHandleWidth(11);
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
    connect(m_searchPopup, &SearchPopup::openVaultRequested, this,
            &MainWindow::openVault);
    connect(m_searchPopup, &SearchPopup::templateRequested, this,
            &MainWindow::onTemplateChosen);

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

    // A transient toast for feedback (rename/delete/disk-change/errors). It
    // floats over the bottom of the editor and auto-hides, so there's no
    // permanent bar across the bottom of the window.
    m_toast = new QLabel(m_editor);
    m_toast->setObjectName(QStringLiteral("toast"));
    m_toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_toast->hide();
    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);

    // The per-note mascot sits in the app's bottom-right corner — a child of
    // the editor *pane* (not the editor), so it lives in the margin beside the
    // centered text column rather than over the text. Shown only when the open
    // note has one; hovering bobs/blinks it, clicking opens the gallery.
    m_mascot = new Mascot(m_centerPane);
    connect(m_mascot, &Mascot::clicked, this, &MainWindow::openMascotGallery);
    // The seed lives inline in the note; the editor reports it on load and
    // whenever the user edits or generates it, so the corner creature stays in
    // step with the file.
    connect(m_editor, &MarkdownEditor::mascotSeedChanged, this,
            &MainWindow::onMascotSeedChanged);
}

void MainWindow::notify(const QString &text, int ms) {
    if (!m_toast)
        return;
    m_toast->setText(text);
    positionToast();
    m_toast->show();
    m_toast->raise();
    m_toastTimer->start(ms);
}

void MainWindow::positionToast() {
    if (!m_toast)
        return;
    m_toast->adjustSize();
    const int x = (m_editor->width() - m_toast->width()) / 2;
    const int y = m_editor->height() - m_toast->height() - 18;
    m_toast->move(qMax(8, x), qMax(8, y));
}

void MainWindow::positionMascot() {
    if (!m_mascot)
        return;
    // Pin to the pane's bottom-right: the margin beside the centered text
    // column, so it sits in the app's corner rather than over the text.
    QWidget *pane = m_mascot->parentWidget();
    if (!pane)
        return;
    const int x = pane->width() - m_mascot->width() - 12;
    const int y = pane->height() - m_mascot->height() - 12;
    m_mascot->move(qMax(0, x), qMax(0, y));
}

void MainWindow::refreshMascot() {
    // The editor is the source of truth (the seed lives in the note's first
    // line); mirror whatever it currently holds.
    onMascotSeedChanged(m_editor ? m_editor->mascotSeed() : 0);
}

void MainWindow::onMascotSeedChanged(quint64 seed) {
    if (!m_mascot)
        return;
    // The editor is the source of truth for the kind too (it lives on the same
    // header line); mirror both onto the corner creature.
    m_mascot->setMascot(seed, m_editor ? m_editor->mascotKind() : QString());
    if (seed) {
        positionMascot();
        m_mascot->raise();
        // On first launch the pane has no final size yet (notably on macOS), so
        // the immediate pin lands in the corner and sticks until a resize. Re-
        // pin once the event loop has run the first real layout.
        QTimer::singleShot(0, this, [this] {
            if (m_mascot && m_mascot->seed()) {
                positionMascot();
                m_mascot->raise();
            }
        });
    }
    updateMascotActions();
}

void MainWindow::generateMascot() {
    if (!m_vault || m_currentPath.isEmpty()) {
        notify(tr("Open a note to give it a mascot"));
        return;
    }
    // The seed is hashed from the note's content (sans any existing header line)
    // and written back into the file's first line, which drives the creature.
    // If the seed rolls one of the user's own creatures, record its kind on the
    // line too so the choice is reproducible and travels with the note.
    const quint64 seed = Mascot::seedFor(m_currentTitle, m_editor->bodyText());
    m_editor->setMascot(seed, Mascot::kindForSeed(seed)); // -> mascotSeedChanged
    notify(tr("Mascot generated"));
}

void MainWindow::deleteMascot() {
    if (!m_vault || m_currentPath.isEmpty() || m_editor->mascotSeed() == 0)
        return;
    m_editor->setMascot(0); // removes the header line; hides the creature
    notify(tr("Mascot removed"));
}

void MainWindow::maybeAutoGenerateMascot() {
    if (!m_vault || m_currentPath.isEmpty() || m_editor->mascotSeed() != 0)
        return; // no note, or it already has a mascot
    QSettings s;
    if (!s.value(QStringLiteral("mascotAuto"), false).toBool())
        return;
    const int threshold =
        s.value(QStringLiteral("mascotThreshold"), 100).toInt();
    if (m_editor->bodyText().size() < threshold)
        return;
    generateMascot(); // crosses the threshold once
}

void MainWindow::updateMascotActions() {
    if (m_genMascotAction)
        m_genMascotAction->setEnabled(m_vault && !m_currentPath.isEmpty());
    if (m_delMascotAction)
        m_delMascotAction->setEnabled(m_editor && m_editor->mascotSeed() != 0);
}

// A transient grid of every mascot in the vault (not persisted anywhere — it's
// rebuilt from the stored seeds each time). Clicking one opens that note.
void MainWindow::openMascotGallery() {
    if (!m_vault)
        return;
    saveCurrent(); // flush the open note so its first line reflects edits

    // Gather every note whose file starts with a mascot header line. No metadata
    // store — each seed lives in its own note, read straight off disk.
    struct Entry { QString path, title; quint64 seed; QString kind; };
    QVector<Entry> entries;
    for (const Note &n : m_vault->notes()) {
        const quint64 seed = mascotSeedInFile(n.path);
        if (seed)
            entries.push_back({n.path, n.title, seed, mascotKindInFile(n.path)});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Mascot Gallery"));
    dlg.resize(560, 540);
    auto *outer = new QVBoxLayout(&dlg);

    if (entries.isEmpty()) {
        auto *empty = new QLabel(
            tr("No mascots yet.\nOpen a note and generate one!"), &dlg);
        empty->setAlignment(Qt::AlignCenter);
        outer->addWidget(empty);
    } else {
        auto *scroll = new QScrollArea(&dlg);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *grid = new QWidget;
        auto *gl = new QGridLayout(grid);
        gl->setSpacing(6);
        const int cols = 4;
        for (int i = 0; i < entries.size(); ++i) {
            const Entry &e = entries.at(i);
            auto *cell = new QToolButton(grid);
            cell->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            cell->setAutoRaise(true);
            cell->setIconSize(QSize(120, 132));
            cell->setIcon(
                QIcon(Mascot::renderPixmap(e.seed, e.kind, QSize(120, 132))));
            cell->setText(e.title);
            cell->setToolTip(e.title);
            const QString abs = e.path;
            connect(cell, &QToolButton::clicked, &dlg, [this, &dlg, abs] {
                dlg.accept();
                if (QFileInfo::exists(abs))
                    openNoteByPath(abs);
            });
            gl->addWidget(cell, i / cols, i % cols);
        }
        scroll->setWidget(grid);
        outer->addWidget(scroll);
    }
    dlg.exec();
}

void MainWindow::buildActions() {
    m_gearMenu = new QMenu(this);

    // Build each action; addAction() on the window keeps its shortcut live
    // without a menubar. The menu itself is assembled in a fixed order below.
    auto make = [this](const QString &text, const QKeySequence &ks,
                       void (MainWindow::*slot)()) {
        auto *a = new QAction(text, this);
        if (!ks.isEmpty())
            a->setShortcut(ks);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
        return a;
    };
    auto *settings = make(tr("Settings…"), QKeySequence(Qt::CTRL | Qt::Key_Comma),
                          &MainWindow::openSettings);
    auto *manual = make(tr("Manual"), {}, &MainWindow::openManual);
    auto *update = make(tr("Check for Updates…"), {}, &MainWindow::checkForUpdates);
    auto *newVault = make(tr("New Vault…"), {}, &MainWindow::newVault);
    auto *openVault = make(tr("Open Vault…"), QKeySequence(QKeySequence::Open),
                           &MainWindow::chooseVault);
    auto *switchVault = make(tr("Switch Vault…"),
                             QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
                             &MainWindow::openVaultSwitcher);
    auto *newNote = make(tr("New Note"), QKeySequence(QKeySequence::New),
                         &MainWindow::newNote);
    auto *goTo = make(tr("Go to Note…"), QKeySequence(Qt::CTRL | Qt::Key_P),
                      &MainWindow::openQuickOpen);
    auto *insertTpl = make(tr("Insert Template…"),
                           QKeySequence(Qt::CTRL | Qt::Key_T),
                           &MainWindow::insertTemplate);
    // Rename focuses the title field for editing (F2 is the universal rename).
    auto *rename = new QAction(tr("Rename Note"), this);
    rename->setShortcut(QKeySequence(Qt::Key_F2));
    connect(rename, &QAction::triggered, this, [this] {
        if (m_currentPath.isEmpty())
            return;
        m_titleEdit->setFocus();
        m_titleEdit->selectAll();
    });
    addAction(rename);
    auto *save = make(tr("Save"), QKeySequence(QKeySequence::Save),
                      &MainWindow::saveCurrent);
    // Delete Note confirms first. Ctrl+Shift+Backspace keeps it clear of
    // Ctrl+Delete (the editor's delete-word-forward) while staying deliberate.
    m_deleteAction = make(tr("Delete Note"),
                          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backspace),
                          &MainWindow::deleteCurrentNote);
    // Context menus hide shortcut labels by default; show this one (the editor's
    // right-click menu offers Delete Note).
    m_deleteAction->setShortcutVisibleInContextMenu(true);
    auto *findHere = make(tr("Find in Note…"), QKeySequence(QKeySequence::Find),
                          &MainWindow::openFindInFile);
    auto *search = make(tr("Search Vault…"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F),
                        &MainWindow::openSearch);
    m_genMascotAction = make(tr("Generate Mascot"), {}, &MainWindow::generateMascot);
    m_delMascotAction = make(tr("Delete Mascot"), {}, &MainWindow::deleteMascot);
    auto *gallery = make(tr("Mascot Gallery…"), {}, &MainWindow::openMascotGallery);
    auto *quit = new QAction(tr("Quit"), this);
    quit->setShortcut(QKeySequence(QKeySequence::Quit));
    connect(quit, &QAction::triggered, this, &QWidget::close);
    addAction(quit);

    // Requested order: app → file ops → search → quit, grouped by separators.
    m_gearMenu->addAction(settings);
    m_gearMenu->addAction(manual);
    m_gearMenu->addAction(update);
    m_gearMenu->addSeparator();
    m_gearMenu->addAction(newVault);
    m_gearMenu->addAction(openVault);
    m_gearMenu->addAction(switchVault);
    m_gearMenu->addAction(newNote);
    m_gearMenu->addAction(goTo);
    m_gearMenu->addAction(insertTpl);
    m_gearMenu->addAction(rename);
    m_gearMenu->addAction(save);
    m_gearMenu->addAction(m_deleteAction);
    m_gearMenu->addSeparator();
    m_gearMenu->addAction(findHere);
    m_gearMenu->addAction(search);
    m_gearMenu->addSeparator();
    m_gearMenu->addAction(m_genMascotAction);
    m_gearMenu->addAction(m_delMascotAction);
    m_gearMenu->addAction(gallery);
    m_gearMenu->addSeparator();
    m_gearMenu->addAction(quit);

    // Font size, bound to the browser zoom keys (Ctrl +/=, Ctrl -, Ctrl 0).
    auto addFontAction = [this](const QList<QKeySequence> &keys, int delta) {
        auto *act = new QAction(this);
        act->setShortcuts(keys);
        connect(act, &QAction::triggered, this,
                [this, delta] { changeFontSize(delta); });
        addAction(act);
    };
    addFontAction({QKeySequence(Qt::CTRL | Qt::Key_Plus),
                   QKeySequence(Qt::CTRL | Qt::Key_Equal)}, 1);
    addFontAction({QKeySequence(Qt::CTRL | Qt::Key_Minus)}, -1);
    addFontAction({QKeySequence(Qt::CTRL | Qt::Key_0)}, 0);

    // Navigation actions drive both the header arrow buttons and the shortcuts.
    // Browser-style Alt+Arrow (Ctrl+[ / Ctrl+] were dropped — they're
    // indent/outdent in most editors).
    m_backAction = new QAction(this);
    m_backAction->setIcon(makeNavArrow(true));
    m_forwardAction = new QAction(this);
    m_forwardAction->setIcon(makeNavArrow(false));
#ifdef Q_OS_MACOS
    // On macOS ⌥+Arrow is a text-editing key the QAction shortcut never receives
    // (the editor handles that itself), so add the system-standard ⌘[ / ⌘] as a
    // reliable QAction-delivered back/forward as well.
    m_backAction->setToolTip(tr("Back  (⌘[ or ⌥←)"));
    m_backAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_BracketLeft),
                                QKeySequence(Qt::ALT | Qt::Key_Left)});
    m_forwardAction->setToolTip(tr("Forward  (⌘] or ⌥→)"));
    m_forwardAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_BracketRight),
                                   QKeySequence(Qt::ALT | Qt::Key_Right)});
#else
    m_backAction->setToolTip(tr("Back  (Alt+Left)"));
    m_backAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    m_forwardAction->setToolTip(tr("Forward  (Alt+Right)"));
    m_forwardAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
#endif
    connect(m_backAction, &QAction::triggered, this, &MainWindow::navigateBack);
    addAction(m_backAction);
    connect(m_forwardAction, &QAction::triggered, this,
            &MainWindow::navigateForward);
    addAction(m_forwardAction);
    updateNavActions();
}

void MainWindow::loadSettings() {
    QSettings s;
    m_centerColumn->setMaximumWidth(
        s.value(QStringLiteral("editorWidth"), 820).toInt());
    m_editor->setLineSpacing(s.value(QStringLiteral("lineSpacing"), 100).toInt());
    const QByteArray split = s.value(QStringLiteral("splitterState")).toByteArray();
    if (!split.isEmpty())
        m_splitter->restoreState(split);
    // With no custom font saved, keep the editor's built-in monospace fallback
    // chain (SF Mono / Menlo / Cascadia / Consolas / … -> monospace) untouched.
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
    auto *spacingBox = new QSpinBox(&dlg);
    spacingBox->setRange(100, 250);
    spacingBox->setSingleStep(10);
    spacingBox->setSuffix(tr(" %"));
    spacingBox->setValue(s.value(QStringLiteral("lineSpacing"), 100).toInt());

    // Mascots: auto-generate once a note crosses a character count.
    auto *mascotAutoBox = new QCheckBox(tr("Generate one automatically"), &dlg);
    mascotAutoBox->setChecked(s.value(QStringLiteral("mascotAuto"), false).toBool());
    auto *mascotThreshBox = new QSpinBox(&dlg);
    mascotThreshBox->setRange(0, 100000);
    mascotThreshBox->setSingleStep(50);
    mascotThreshBox->setSuffix(tr(" chars"));
    mascotThreshBox->setValue(
        s.value(QStringLiteral("mascotThreshold"), 100).toInt());
    mascotThreshBox->setEnabled(mascotAutoBox->isChecked());
    connect(mascotAutoBox, &QCheckBox::toggled, mascotThreshBox,
            &QWidget::setEnabled);

    // New-note folder + Home note pickers (need an open vault).
    auto *folderBox = new QComboBox(&dlg);
    auto *homeBox = new QComboBox(&dlg);
    auto *templatesBox = new QComboBox(&dlg);
    folderBox->addItem(tr("(Vault root)"), QString());
    homeBox->addItem(tr("(None)"), QString());
    templatesBox->addItem(tr("(None)"), QString());
    if (m_vault) {
        const QDir root(m_vault->root());
        for (const QString &rel : m_vault->folders()) {
            folderBox->addItem(rel, rel);
            templatesBox->addItem(rel, rel);
        }
        for (const Note &n : m_vault->notes())
            homeBox->addItem(n.title, root.relativeFilePath(n.path));
        const int fi = folderBox->findData(s.value(QStringLiteral("newNoteFolder")));
        if (fi >= 0)
            folderBox->setCurrentIndex(fi);
        const int hi = homeBox->findData(s.value(QStringLiteral("homeNote")));
        if (hi >= 0)
            homeBox->setCurrentIndex(hi);
        const int ti =
            templatesBox->findData(s.value(QStringLiteral("templatesFolder")));
        if (ti >= 0)
            templatesBox->setCurrentIndex(ti);
    } else {
        folderBox->setEnabled(false);
        homeBox->setEnabled(false);
        templatesBox->setEnabled(false);
    }

    form->addRow(tr("Editor font"), fontBox);
    form->addRow(tr("Font size"), sizeBox);
    form->addRow(tr("Editor width"), widthBox);
    form->addRow(tr("Line spacing"), spacingBox);
    form->addRow(tr("New notes in"), folderBox);
    form->addRow(tr("Home note"), homeBox);
    form->addRow(tr("Templates folder"), templatesBox);
    form->addRow(tr("Mascot"), mascotAutoBox);
    form->addRow(tr("Mascot after"), mascotThreshBox);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // Live preview of font + width + line spacing as the user changes controls.
    auto preview = [this, fontBox, sizeBox, widthBox, spacingBox] {
        QFont f = fontBox->currentFont();
        f.setPointSize(sizeBox->value());
        m_editor->applyFont(f);
        m_centerColumn->setMaximumWidth(widthBox->value());
        m_editor->setLineSpacing(spacingBox->value());
    };
    connect(fontBox, &QFontComboBox::currentFontChanged, &dlg, preview);
    connect(sizeBox, qOverload<int>(&QSpinBox::valueChanged), &dlg, preview);
    connect(widthBox, qOverload<int>(&QSpinBox::valueChanged), &dlg, preview);
    connect(spacingBox, qOverload<int>(&QSpinBox::valueChanged), &dlg, preview);

    const QFont originalFont = m_editor->font();
    const int originalWidth = m_centerColumn->maximumWidth();
    const int originalSpacing = s.value(QStringLiteral("lineSpacing"), 100).toInt();
    if (dlg.exec() == QDialog::Accepted) {
        QFont f = fontBox->currentFont();
        f.setPointSize(sizeBox->value());
        m_editor->applyFont(f);
        m_centerColumn->setMaximumWidth(widthBox->value());
        m_editor->setLineSpacing(spacingBox->value());
        s.setValue(QStringLiteral("editorFontFamily"), f.family());
        s.setValue(QStringLiteral("editorFontSize"), f.pointSize());
        s.setValue(QStringLiteral("editorWidth"), widthBox->value());
        s.setValue(QStringLiteral("lineSpacing"), spacingBox->value());
        s.setValue(QStringLiteral("mascotAuto"), mascotAutoBox->isChecked());
        s.setValue(QStringLiteral("mascotThreshold"), mascotThreshBox->value());
        if (m_vault) {
            s.setValue(QStringLiteral("newNoteFolder"), folderBox->currentData());
            s.setValue(QStringLiteral("homeNote"), homeBox->currentData());
            s.setValue(QStringLiteral("templatesFolder"),
                       templatesBox->currentData());
        }
    } else {
        m_editor->applyFont(originalFont); // revert the live preview
        m_centerColumn->setMaximumWidth(originalWidth);
        m_editor->setLineSpacing(originalSpacing);
    }
}

void MainWindow::changeFontSize(int delta) {
    QFont f = m_editor->font();
    const int size = delta == 0 ? 12 : qBound(8, f.pointSize() + delta, 32);
    if (delta != 0 && size == f.pointSize())
        return; // already at the min/max
    f.setPointSize(size);
    m_editor->applyFont(f);
    QSettings s;
    s.setValue(QStringLiteral("editorFontFamily"), f.family());
    s.setValue(QStringLiteral("editorFontSize"), size);
    notify(tr("Font size: %1 pt").arg(size), 1200);
}

void MainWindow::newVault() {
    const QString parent = QFileDialog::getExistingDirectory(
        this, tr("Choose where to create the vault"), vaultStartDir());
    if (parent.isEmpty())
        return;
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("New Vault"), tr("Vault name:"),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return;
    QDir dir(parent);
    if (dir.exists(name)) {
        notify(tr("A folder named “%1” already exists here").arg(name), 3000);
        return;
    }
    if (!dir.mkdir(name)) {
        notify(tr("Couldn't create the vault"), 3000);
        return;
    }
    openVault(dir.filePath(name));
    notify(tr("Created vault “%1”").arg(name), 2500);
}

void MainWindow::deleteCurrentNote() {
    if (m_currentPath.isEmpty()) {
        notify(tr("No note is open to delete"), 2000);
        return;
    }
    deleteEntries({m_currentPath}); // shows the confirm dialog + reconciles
}

void MainWindow::onEditorContextMenu(const QPoint &pos) {
    QMenu *menu = m_editor->createStandardContextMenu();
    // Drop the standard "Delete" entry — it only removes a text selection, so it
    // reads as a broken delete-the-file. Offer the real "Delete Note" instead,
    // which carries its Ctrl+Shift+Backspace shortcut label.
    for (QAction *a : menu->actions())
        if (a->objectName() == QLatin1String("edit-delete"))
            menu->removeAction(a);
    if (m_deleteAction && !m_currentPath.isEmpty()) {
        menu->addSeparator();
        menu->addAction(m_deleteAction);
    }
    menu->exec(m_editor->mapToGlobal(pos));
    delete menu;
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

void MainWindow::checkForUpdates() {
    if (!m_updater)
        m_updater = new Updater(this);
    m_updater->check();
}

void MainWindow::chooseVault() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Open Vault"), vaultStartDir());
    if (!dir.isEmpty())
        openVault(dir);
}

void MainWindow::openVaultSwitcher() {
    // Candidate vaults are the sub-folders beside the current vault (i.e. in its
    // parent folder), or the home folder's sub-folders when none is open.
    QString base = QDir::homePath();
    if (m_vault) {
        const QString parent = QFileInfo(m_vault->root()).absolutePath();
        if (QDir(parent).exists())
            base = parent;
    }
    QDir dir(base);
    QStringList paths;
    const QStringList names =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &n : names)
        paths << dir.absoluteFilePath(n);
    if (paths.isEmpty()) {
        notify(tr("No vaults found in %1").arg(QDir::toNativeSeparators(base)),
               2500);
        return;
    }
    m_searchPopup->showVaults(paths);
}

void MainWindow::openVault(const QString &path) {
    saveCurrent();
    delete m_vault;
    m_vault = new Vault(path);
    migrateLegacyMascots(path); // fold any legacy .emerald/mascots.json into notes
    m_vault->scan();
    m_searchIndex.rebuild(*m_vault);

    m_currentPath.clear();
    m_currentTitle.clear();
    m_history.clear();
    m_histIndex = -1;
    updateNavActions();
    m_editor->clearFolds(); // drop the previous note's folds before clearing
    m_loading = true;
    m_editor->clear();
    m_loading = false;
    m_titleEdit->blockSignals(true);
    m_titleEdit->clear();
    m_titleEdit->blockSignals(false);

    refreshTree(false); // a freshly opened vault starts fully collapsed
    QSettings().setValue(QStringLiteral("lastVault"), path);
    setWindowTitle(QStringLiteral("Emerald — %1").arg(QFileInfo(path).fileName()));
    openInitialNote();
    refreshMascot(); // hide a stale mascot if the new vault opened no note
}

// One-time upgrade from the old per-vault store: read each saved seed and write
// it into the matching note's inline header line (if it doesn't already have
// one), then delete the JSON so no app metadata is left behind in the vault.
void MainWindow::migrateLegacyMascots(const QString &vaultRoot) {
    const QString jsonPath =
        QDir(vaultRoot).filePath(QStringLiteral(".emerald/mascots.json"));
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return; // nothing to migrate
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    const QJsonObject mascots = root.value(QStringLiteral("mascots")).toObject();
    const QDir rootDir(vaultRoot);
    for (auto it = mascots.begin(); it != mascots.end(); ++it) {
        const quint64 seed =
            it.value().toObject().value(QStringLiteral("seed")).toString().toULongLong();
        if (seed == 0)
            continue; // suppressed entries simply drop (no metadata to keep)
        const QString notePath = rootDir.filePath(it.key());
        if (!QFileInfo::exists(notePath))
            continue;
        const QString content = m_vault->read(notePath);
        const int nl = content.indexOf(QLatin1Char('\n'));
        const QString first = nl < 0 ? content : content.left(nl);
        if (MascotSeed::fromLine(first) != 0)
            continue; // already has an inline header line
        m_vault->write(notePath,
                       MascotSeed::line(seed) + QLatin1Char('\n') + content);
    }

    QFile::remove(jsonPath);
    QDir(vaultRoot).rmdir(QStringLiteral(".emerald")); // only if now empty
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

void MainWindow::refreshTree(bool preserveExpansion) {
    // Remember which folders are open so a rebuild (after a rename/move/new note)
    // doesn't collapse the tree the user expanded. A freshly opened vault passes
    // preserveExpansion=false, so it always starts fully collapsed.
    QSet<QString> expanded;
    if (preserveExpansion)
        for (QTreeWidgetItemIterator it(m_noteTree); *it; ++it) {
            const QString dir = (*it)->data(0, kDirRole).toString();
            if (!dir.isEmpty() && (*it)->isExpanded())
                expanded.insert(dir);
        }

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
        // Collapsed glyph by default; expandAll() below flips visible folders to
        // the expanded chevron via the itemExpanded signal.
        node->setIcon(0, chevronIcon(false));
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
    // Re-open the folders that were open before (none for a fresh vault, so it
    // stays fully collapsed). The chevron icons follow via the itemExpanded signal.
    for (auto it = folders.constBegin(); it != folders.constEnd(); ++it)
        if (expanded.contains(it.value()->data(0, kDirRole).toString()))
            it.value()->setExpanded(true);
    m_editor->setCompletions(titles);
    selectInTree(m_currentPath);
    watchVaultDirs(); // keep folder watches in sync with the rebuilt tree
}

void MainWindow::openNoteByPath(const QString &path, bool record) {
    if (!m_vault || path.isEmpty())
        return;
    if (!QFileInfo::exists(path)) {
        // A stale target (e.g. a note deleted outside the app but still in the
        // nav history): drop dangling entries and bail, rather than read an
        // empty buffer and silently re-create the file on the next save.
        pruneHistory();
        return;
    }
    // Remember where the caret sat in the note we're leaving, so returning to
    // it (e.g. via the backlink history) lands back at the same spot.
    if (!m_currentPath.isEmpty() && m_currentPath != path)
        m_cursorPositions[m_currentPath] = m_editor->textCursor().position();
    saveCurrent();

    m_editor->clearFolds(); // the previous note's folds would dangle once its
                            // content is replaced below — drop them first
    m_loading = true;
    const QString body = m_vault->read(path);
    m_editor->setPlainText(body);
    m_loading = false;

    m_currentPath = path;
    m_lastSavedContent = body;
    m_currentTitle = Vault::titleFromPath(path);
    watchCurrent();
    m_titleEdit->blockSignals(true);
    m_titleEdit->setText(m_currentTitle);
    m_titleEdit->blockSignals(false);
    setWindowTitle(QStringLiteral("Emerald — %1").arg(m_currentTitle));
    selectInTree(path);
    QSettings().setValue(QStringLiteral("lastNote"), path); // reopen on launch
    if (record)
        pushHistory(path);
    updateNavActions();

    // Always restore the caret to where it last sat in this note (remembered in
    // m_cursorPositions, persisted across restarts) — whether arriving via the
    // back/forward arrows, a tree click, a link, or launch. A note never visited
    // before falls back to its first line. The minimum position skips a hidden
    // mascot header line so the caret never starts on it. The editor is focused,
    // ready to type, either way.
    QTextCursor c = m_editor->textCursor();
    const int minPos = m_editor->firstContentPosition();
    const int last = qMax(minPos, m_cursorPositions.value(path, minPos));
    c.setPosition(qBound(
        minPos, last, qMax(minPos, m_editor->document()->characterCount() - 1)));
    m_editor->setTextCursor(c);
    m_editor->setFocus();
    // Bring the restored caret into view, centred. Deferred to the event loop:
    // centring inline runs against the just-loaded document before its layout
    // and viewport have settled, which leaves the view stuck at the top while
    // the caret sits offscreen lower down.
    MarkdownEditor *ed = m_editor;
    QTimer::singleShot(0, ed, [ed] { ed->centerCursor(); });
    refreshMascot(); // mirror this note's inline seed (the editor parsed it on load)
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
        notify(tr("Invalid note name"), 3000);
        return;
    }

    saveCurrent(); // flush the body before the file moves
    const QString oldTitle = m_currentTitle;
    const QString oldPath = m_currentPath;
    const QString newPath = m_vault->renameNote(oldPath, newTitle);
    if (newPath.isEmpty()) {
        revertField();
        notify(
            tr("A note named “%1” already exists").arg(newTitle), 3000);
        return;
    }

    m_vault->updateLinksTo(oldTitle, newTitle);
    m_currentPath = newPath;
    m_currentTitle = newTitle;
    watchCurrent(); // follow the file to its new name
    for (QString &p : m_history)
        if (p == oldPath)
            p = newPath;
    if (m_cursorPositions.contains(oldPath))
        m_cursorPositions[newPath] = m_cursorPositions.take(oldPath);
    m_searchIndex.rebuild(*m_vault); // paths and link text changed vault-wide
    refreshTree();
    setWindowTitle(QStringLiteral("Emerald — %1").arg(newTitle));
    notify(tr("Renamed to “%1”").arg(newTitle), 3000);
}

void MainWindow::saveCurrent() {
    if (!m_vault)
        return;
    if (m_currentPath.isEmpty()) {
        // Untitled buffer: a save creates the note, but only once it has a
        // valid, unused title — with no title we save nothing at all.
        const QString title = m_titleEdit->text().trimmed();
        if (!Vault::isValidTitle(title))
            return;
        if (!m_vault->pathForTitle(title).isEmpty()) {
            notify(tr("A note named “%1” already exists").arg(title), 3000);
            return;
        }
        // Honour the configured new-note folder (default: the vault root).
        QString dir = m_vault->root();
        const QString rel =
            QSettings().value(QStringLiteral("newNoteFolder")).toString();
        if (!rel.isEmpty()) {
            const QString candidate = QDir(m_vault->root()).filePath(rel);
            if (QDir(candidate).exists())
                dir = candidate;
        }
        const Note note = m_vault->createNoteIn(dir, title);
        if (note.path.isEmpty())
            return;
        m_currentPath = note.path;
        m_currentTitle = title;
        const QString content = m_editor->toPlainText();
        m_vault->write(note.path, content);
        m_lastSavedContent = content;
        m_vault->scan();
        m_searchIndex.rebuild(*m_vault);
        refreshTree();
        watchCurrent();
        selectInTree(note.path);
        setWindowTitle(QStringLiteral("Emerald — %1").arg(title));
        QSettings().setValue(QStringLiteral("lastNote"), note.path);
        pushHistory(note.path);
        updateNavActions();
        notify(tr("Created “%1”").arg(title), 2000);
        return;
    }
    const QString content = m_editor->toPlainText();
    if (content == m_lastSavedContent)
        return; // nothing new to flush; avoid a self-triggered watcher event
    m_vault->write(m_currentPath, content);
    m_lastSavedContent = content;
    m_searchIndex.updateNote(m_currentPath, m_currentTitle, content);
}

// Watch only the note that's currently open; drop whatever we watched before.
void MainWindow::watchCurrent() {
    if (!m_watcher)
        return;
    const QStringList watched = m_watcher->files();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);
    if (!m_currentPath.isEmpty())
        m_watcher->addPath(m_currentPath);
}

// Sync the watcher's directory list to the vault's folders (root + sub-folders)
// without disturbing the watched note file. Diff against what's already watched
// so a refresh doesn't churn (or warn about) unchanged paths.
void MainWindow::watchVaultDirs() {
    if (!m_watcher)
        return;
    QStringList desired;
    if (m_vault) {
        const QDir rootDir(m_vault->root());
        desired << m_vault->root();
        for (const QString &rel : m_vault->folders())
            desired << rootDir.filePath(rel);
    }
    const QStringList current = m_watcher->directories();
    QStringList toRemove;
    for (const QString &d : current)
        if (!desired.contains(d))
            toRemove << d;
    if (!toRemove.isEmpty())
        m_watcher->removePaths(toRemove);
    QStringList toAdd;
    for (const QString &d : desired)
        if (!current.contains(d))
            toAdd << d;
    if (!toAdd.isEmpty())
        m_watcher->addPaths(toAdd);
}

// A note was added, removed, or renamed in a vault folder by another program.
// Defer the rescan so a burst of events collapses into a single rebuild.
void MainWindow::onVaultDirChanged(const QString &) { m_rescanTimer->start(); }

// The open note's file changed on disk (another program saved it). Defer the
// reconcile so a save's burst of events collapses into one read of the settled
// file — and so a momentarily-missing file (mid backup-rename) isn't mistaken
// for a deletion.
void MainWindow::onFileChanged(const QString &path) {
    if (path == m_currentPath)
        m_reloadTimer->start();
}

// Reconcile the open note with disk. Adopt new contents only when we have no
// unsaved edits; never clobber the user's buffer.
void MainWindow::syncOpenNoteFromDisk() {
    if (!m_vault || m_currentPath.isEmpty())
        return;

    // A replace-and-rename or backup-rename save gives the file a new inode and
    // makes the watcher forget it; follow it back so later edits are noticed.
    const bool exists = QFileInfo::exists(m_currentPath);
    if (m_watcher && exists && !m_watcher->files().contains(m_currentPath))
        m_watcher->addPath(m_currentPath);

    if (!exists) {
        notify(tr("This note was removed on disk"), 4000);
        return;
    }

    const QString disk = m_vault->read(m_currentPath);
    if (disk == m_lastSavedContent)
        return; // our own write, or no real change

    if (m_editor->toPlainText() != m_lastSavedContent) {
        notify(tr("Changed on disk — saving will keep your version"), 5000);
        return;
    }

    // No local edits: reload, keeping the caret roughly where it was.
    const int caret = m_editor->textCursor().position();
    m_editor->clearFolds(); // reloading replaces the content; drop stale folds
    m_loading = true;
    m_editor->setPlainText(disk);
    m_loading = false;
    m_lastSavedContent = disk;

    QTextCursor c = m_editor->textCursor();
    c.setPosition(qMin(caret, int(disk.size())));
    m_editor->setTextCursor(c);
    notify(tr("Reloaded — changed on disk"), 3000);
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

void MainWindow::pruneHistory() {
    // The note the index currently points at; we keep the index on it if it
    // survives the prune (so deleting a *different* note doesn't move our spot).
    const QString current = (m_histIndex >= 0 && m_histIndex < m_history.size())
                                ? m_history.at(m_histIndex)
                                : QString();
    m_history.erase(
        std::remove_if(m_history.begin(), m_history.end(),
                       [](const QString &p) { return !QFileInfo::exists(p); }),
        m_history.end());
    m_histIndex = current.isEmpty() ? m_history.size() - 1
                                    : m_history.lastIndexOf(current);
    if (m_histIndex < 0)
        m_histIndex = m_history.size() - 1;
    updateNavActions();
}

void MainWindow::updateNavActions() {
    if (m_backAction)
        m_backAction->setEnabled(m_histIndex > 0);
    if (m_forwardAction)
        m_forwardAction->setEnabled(m_histIndex >= 0 &&
                                    m_histIndex < m_history.size() - 1);
}

// Persist the per-note caret positions (folding in the open note's current
// caret) so reopening a note — even after a restart — lands where you left off.
void MainWindow::saveCursorPositions() {
    if (m_editor && !m_currentPath.isEmpty())
        m_cursorPositions[m_currentPath] = m_editor->textCursor().position();
    QVariantMap map;
    for (auto it = m_cursorPositions.constBegin();
         it != m_cursorPositions.constEnd(); ++it)
        map.insert(it.key(), it.value());
    QSettings().setValue(QStringLiteral("cursorPositions"), map);
}

void MainWindow::loadCursorPositions() {
    const QVariantMap map =
        QSettings().value(QStringLiteral("cursorPositions")).toMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        m_cursorPositions.insert(it.key(), it.value().toInt());
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

void MainWindow::insertTemplate() {
    if (!m_vault)
        return;
    const QString rel =
        QSettings().value(QStringLiteral("templatesFolder")).toString();
    if (rel.isEmpty()) {
        notify(tr("Set a templates folder in Settings first."), 3000);
        return;
    }
    const QString dir = QDir(m_vault->root()).filePath(rel);
    if (!QDir(dir).exists()) {
        notify(tr("Templates folder not found: %1").arg(rel), 3000);
        return;
    }
    // Every .md under the folder (subfolders included) is a template.
    QStringList files;
    QDirIterator it(dir, {QStringLiteral("*.md")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        files << it.next();
    files.sort(Qt::CaseInsensitive);
    if (files.isEmpty()) {
        notify(tr("No templates in %1").arg(rel), 3000);
        return;
    }
    m_searchPopup->showTemplates(files);
}

void MainWindow::onTemplateChosen(const QString &path) {
    if (!m_vault || path.isEmpty())
        return;
    const QString body =
        expandTemplateTokens(m_vault->read(path), m_currentTitle);
    // Insert at the caret, but never inside the hidden mascot header line — drop
    // to the body's first position there (the spec's "beginning of file").
    QTextCursor c = m_editor->textCursor();
    if (c.position() < m_editor->firstContentPosition())
        c.setPosition(m_editor->firstContentPosition());
    c.insertText(body);
    m_editor->setTextCursor(c);
    m_editor->setFocus();
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
    } else if (watched == m_editor && event->type() == QEvent::Resize) {
        if (m_findBar && m_findBar->isVisible())
            positionFindBar();
        if (m_toast && m_toast->isVisible())
            positionToast();
    } else if (watched == m_centerPane && event->type() == QEvent::Resize) {
        if (m_mascot && m_mascot->isVisible())
            positionMascot();
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
    // Ctrl/Shift clicks are selection gestures (multi-select) — let them just
    // extend the selection without opening a note or folding a folder.
    if (QGuiApplication::keyboardModifiers() &
        (Qt::ControlModifier | Qt::ShiftModifier))
        return;
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

    // Gather the selected paths; a bulk delete applies when the right-clicked
    // row is part of a multi-row selection.
    QStringList selPaths;
    for (QTreeWidgetItem *it : m_noteTree->selectedItems()) {
        const QString d = it->data(0, kDirRole).toString();
        const QString p = d.isEmpty() ? it->data(0, kPathRole).toString() : d;
        if (!p.isEmpty())
            selPaths << p;
    }
    const bool bulk = item && item->isSelected() && selPaths.size() > 1;

    QMenu menu(this);
    menu.addAction(tr("New Note"), this, [this, dir] { newNoteIn(dir); });
    menu.addAction(tr("New Folder"), this, [this, dir] { newFolderIn(dir); });
    if (bulk) {
        menu.addSeparator();
        menu.addAction(tr("Delete %1 Items").arg(selPaths.size()), this,
                       [this, selPaths] { deleteEntries(selPaths); });
    } else if (!notePath.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Delete Note"), this,
                       [this, notePath] { deleteEntries({notePath}); });
    } else if (!folderPath.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Delete Folder"), this,
                       [this, folderPath] { deleteEntries({folderPath}); });
    }
    menu.exec(m_noteTree->viewport()->mapToGlobal(pos));
}

// Clear settings that pointed at a deleted note/folder so they don't go stale
// (Home note / new-note folder).
void MainWindow::clearStaleSettingsFor(const QString &path, bool isFolder) {
    QSettings s;
    const QString rel = QDir(m_vault->root()).relativeFilePath(path);
    const QString home = s.value(QStringLiteral("homeNote")).toString();
    const QString nf = s.value(QStringLiteral("newNoteFolder")).toString();
    if (home == rel || (isFolder && home.startsWith(rel + QLatin1Char('/'))))
        s.remove(QStringLiteral("homeNote"));
    if (isFolder && (nf == rel || nf.startsWith(rel + QLatin1Char('/'))))
        s.remove(QStringLiteral("newNoteFolder"));
}

// After a deletion: rescan, and if the open note was removed (on its own or
// inside a deleted folder), prune history and open a sensible fallback.
void MainWindow::reconcileAfterDeletion() {
    const bool currentGone =
        !m_currentPath.isEmpty() && !QFileInfo::exists(m_currentPath);
    m_vault->scan();
    m_searchIndex.rebuild(*m_vault);
    if (!currentGone) {
        // A *different* note (or a folder of them) was deleted — its path may
        // still sit in the nav history; drop it so Back/Forward can't reach a
        // now-deleted note (and re-create it on the next save).
        pruneHistory();
        refreshTree();
        return;
    }
    m_currentPath.clear();
    m_currentTitle.clear();
    // Empty the editor + title buffer now. Otherwise the pre-load save that
    // openNoteByPath() runs below would see an empty m_currentPath next to the
    // deleted note's still-present title/body and re-create it as a new note —
    // making the deletion appear to silently fail.
    m_editor->clearFolds(); // drop the deleted note's folds before clearing
    m_loading = true;
    m_editor->clear();
    m_loading = false;
    m_titleEdit->blockSignals(true);
    m_titleEdit->clear();
    m_titleEdit->blockSignals(false);

    pruneHistory();
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
    if (!fallback.isEmpty())
        openNoteByPath(fallback);
    else
        setWindowTitle(QStringLiteral("Emerald"));
}

void MainWindow::deleteEntries(const QStringList &pathsIn) {
    // A selected folder takes its contents with it, so ignore nested children.
    const QStringList paths = topLevelPaths(pathsIn);
    if (paths.isEmpty())
        return;

    QString question;
    if (paths.size() == 1) {
        const QString p = paths.first();
        const bool isFolder = QFileInfo(p).isDir();
        const QString name =
            isFolder ? QFileInfo(p).fileName() : Vault::titleFromPath(p);
        question =
            isFolder
                ? tr("Move the folder “%1” and everything inside it to the trash?")
                      .arg(name)
                : tr("Move the note “%1” to the trash?").arg(name);
    } else {
        question =
            tr("Move the %1 selected items to the trash?").arg(paths.size());
    }
    if (QMessageBox::question(this, tr("Move to Trash"), question) !=
        QMessageBox::Yes)
        return;

    int removed = 0;
    QString lastName;
    QStringList failed;
    for (const QString &path : paths) {
        const bool isFolder = QFileInfo(path).isDir();
        const QString name =
            isFolder ? QFileInfo(path).fileName() : Vault::titleFromPath(path);
        if (!m_vault->remove(path)) {
            failed << name;
            continue;
        }
        clearStaleSettingsFor(path, isFolder);
        lastName = name;
        ++removed;
    }
    if (removed == 0) {
        notify(tr("Couldn't move it to the trash"), 3000);
        return;
    }

    reconcileAfterDeletion();
    if (!failed.isEmpty())
        notify(tr("Couldn't move %1 to the trash")
                   .arg(failed.join(QStringLiteral(", "))),
               4000);
    else
        notify(removed == 1 ? tr("Moved “%1” to the trash").arg(lastName)
                            : tr("Moved %1 items to the trash").arg(removed),
               3000);
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

void MainWindow::moveItems(const QStringList &srcPaths, const QString &destDirIn) {
    if (!m_vault || srcPaths.isEmpty())
        return;
    const QString destDir = destDirIn.isEmpty() ? m_vault->root() : destDirIn;
    int moved = 0;
    for (const QString &srcPath : srcPaths) {
        const QString newPath = m_vault->movePath(srcPath, destDir);
        if (newPath.isEmpty())
            continue;
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
        if (m_cursorPositions.contains(srcPath))
            m_cursorPositions[newPath] = m_cursorPositions.take(srcPath);
        ++moved;
    }
    if (moved == 0) {
        notify(tr("Couldn't move it there"), 3000);
        return;
    }
    watchCurrent(); // the open note may have moved with it

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
        notify(tr("Couldn't create that folder"), 3000);
        return;
    }
    m_vault->scan();
    refreshTree();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveCurrent();
    saveCursorPositions(); // remember caret positions for the next launch
    QSettings().setValue(QStringLiteral("splitterState"), m_splitter->saveState());
    event->accept();
}
