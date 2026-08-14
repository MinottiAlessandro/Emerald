#include "MainWindow.h"

#include "AppTheme.h"
#include "GraphPage.h"
#include "MarkdownEditor.h"
#include "Mascot.h"
#include "MascotCatalog.h"
#include "SearchPopup.h"
#include "SpellLanguageDialog.h"
#include "ThemeEditorDialog.h"
#include "Updater.h"
#include "core/LegacyMascotMigration.h"
#include "core/MascotSeed.h"
#include "core/SpellChecker.h"
#include "core/Vault.h"
#include "core/VaultSettings.h"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractSpinBox>
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
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSplitter>
#include <QStyle>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QResizeEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace {
constexpr int kPathRole = Qt::UserRole;     // leaf: the note's file path
constexpr int kDirRole = Qt::UserRole + 1;  // folder: its absolute path
constexpr int kMobileBreakpoint = 760;
constexpr int kDesktopSplitterHandleWidth = 11;
constexpr int kGraphSplitterHandleWidth = 2;
constexpr int kDefaultSidebarWidth = 260;

enum class NoteTreeSort {
    NameAscending,
    NameDescending,
    ModifiedNewest,
    ModifiedOldest
};

NoteTreeSort noteTreeSortFromKey(const QString &key) {
    if (key == QStringLiteral("nameDesc"))
        return NoteTreeSort::NameDescending;
    if (key == QStringLiteral("modifiedNewest"))
        return NoteTreeSort::ModifiedNewest;
    if (key == QStringLiteral("modifiedOldest"))
        return NoteTreeSort::ModifiedOldest;
    return NoteTreeSort::NameAscending;
}

QString currentNoteTreeSortKey() {
    const QString key =
        QSettings().value(QStringLiteral("fileTreeSort"),
                          QStringLiteral("nameAsc")).toString();
    return key == QStringLiteral("nameDesc") ||
                   key == QStringLiteral("modifiedNewest") ||
                   key == QStringLiteral("modifiedOldest")
               ? key
               : QStringLiteral("nameAsc");
}

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
        "can edit it, and melts into formatted text on every other line. A "
        "selection reveals the exact source of every line it crosses, then "
        "renders those lines again when the selection leaves.\n"
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
        "    - click a parent item's arrow to fold or unfold its children\n"
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
        "Start a quote with `> [!tip]` to create an Obsidian-style callout. "
        "The whole quote becomes a continuous colored card and the marker "
        "becomes a type-specific emoji plus **Tip** when you leave the line; "
        "put text after it for a custom title, for example "
        "`> [!warning] Read this first`. Built-in types (each with its own "
        "emoji) are `note`, `abstract`, `summary`, `tldr`, `info`, `todo`, "
        "`tip`, `hint`, `important`, `success`, `check`, `done`, `question`, "
        "`help`, `faq`, `warning`, `caution`, `attention`, `failure`, `fail`, "
        "`missing`, `danger`, `error`, `bug`, `example`, `quote`, and `cite`; "
        "other names use a generic callout style.\n"
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
        "Move the caret into a code block to reveal both fences. Code stays "
        "verbatim inside, and the arrow beside its opening fence folds or "
        "unfolds the complete block. Typing the third opening backtick adds "
        "the closing fence automatically.\n"
        "\n"
        "## Math\n"
        "Write inline math between single dollar signs, such as `$x^2 + y^2$`, "
        "or display math between `$$` markers; display expressions may span "
        "several lines. Emerald's built-in renderer supports fractions and "
        "roots (`\\frac`, `\\sqrt`), super/subscripts, sums and integrals, "
        "accents, growing delimiters, text, matrices, Greek letters, operators, "
        "relations, and arrows. Bare currency dollars and math inside inline "
        "code stay literal.\n"
        "\n"
        "## Images\n"
        "Press **Ctrl+Shift+I**, choose **Insert Image…** from the gear or "
        "editor context menu, paste an image, or drop image files into a note. "
        "Files from outside the vault are copied into `_attachments`; files "
        "already inside it are linked in place. A standalone "
        "`![Alt](path)` line shows a responsive preview in both modes, reveals "
        "its compact Markdown when you edit the line, and shows a small "
        "fallback card if the local image is unavailable.\n"
        "\n"
        "## Tables\n"
        "Type a pipe table and Emerald lines the columns up as you go — on every "
        "Tab and when you click away. Colons in the separator row set the "
        "alignment — `:--` left, `:-:` centre, `--:` right.\n"
        "\n"
        "Press **Enter** anywhere on a header row (the first line, before "
        "there's a `---` separator) and Emerald adds the separator and a first data row for "
        "you, dropping the caret in its first cell. Enter from an existing "
        "header or separator does the same and lines up the table when it fits.\n"
        "\n"
        "Press **Tab** inside a table to jump to the next cell (**Shift+Tab** "
        "goes back). Tab on the last header cell adds a column; on the separator "
        "row it starts a new data row; on the last cell of the last row it adds a "
        "row — so you can build a whole table without leaving the keyboard. "
        "In body rows, **Enter** moves to the same cell below, adding that row "
        "at the bottom when needed. Tables stay as plain Markdown in Edit Mode.\n"
        "\n"
        "| Mascot          | Shortcut     |\n"
        "| :-------------- | -----------: |\n"
        "| Open gallery    | Ctrl+G       |\n"
        "| Generate mascot | Ctrl+M       |\n"
        "| Delete mascot   | Ctrl+Shift+M |\n"
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
        "## Mascots\n"
        "A note can have a small procedurally drawn mascot in its bottom-right "
        "corner. Press **Ctrl+M** to generate or re-roll one, "
        "**Ctrl+Shift+M** to remove it, or **Ctrl+G** (or click a mascot) to "
        "open the vault gallery. Settings can generate one automatically after "
        "a chosen character count; deleting it suppresses automatic generation "
        "for that note until you generate one manually.\n"
        "\n"
        "The reproducible seed is a hidden first-line HTML comment, so it "
        "travels with the Markdown file without separate vault metadata. Press "
        "Up at the start of the body to reveal and edit it. Emerald also "
        "discovers custom layered SVG creatures under "
        "`mascots/creatures/<name>` in its standard app-data folder. Put "
        "PNG/JPG/WEBP files in `mascots/images` and enable **Use Image "
        "Mascots** in the gear menu to use deterministic image tiles instead.\n"
        "\n"
        "## Comments\n"
        "Wrap author-only text in `<!-- comment -->`. Comments may sit inline "
        "or span several lines; they remain visible as subdued source in Edit "
        "Mode. Read Mode, search, spell "
        "checking, Graph View, Quick Jump, broken-link checks, and automatic "
        "link renaming all ignore them. Comment-looking text inside code or "
        "math remains literal.\n"
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
        "**`**, **~**, or **$** to wrap the selection in it (brackets close "
        "with their match)\n"
        "\n"
        "## Linking notes\n"
        "Type `[[` to autocomplete a link to another note. `[[Emerald Manual]]` "
        "jumps to a note — click it once rendered, or Ctrl+click while editing — "
        "and a note that doesn't exist yet is created on the spot. Use "
        "`[[Note|label]]` to show a different label. Renaming a note's title "
        "rewrites every link that points to it; new link targets honor the "
        "vault's **New notes in** folder. Standard `[label](https://…)` links "
        "open in the system browser. Hold **Alt** briefly to label "
        "the links currently on screen, then type a hint to open one without "
        "the mouse (X is reserved for the shortcuts panel). "
        "**Ctrl+Shift+B** opens Broken Links, a filterable report of links "
        "whose target is missing or empty.\n"
        "\n"
        "## Graph View\n"
        "Press **Ctrl+Shift+G** (or **Settings → Vault → Graph view → Open "
        "global**) to replace this note with a map of every wiki-linked note in "
        "the vault. **Open local** starts from the current note. Both open in "
        "the same workspace, never another window. Drag empty space to pan, use "
        "the wheel to zoom, drag nodes, click one to inspect it, and "
        "double-click or press Enter to open it. **F** fits the graph, **0** "
        "resets its camera, and **/** or **Ctrl+F** focuses title search.\n"
        "\n"
        "Filter the global view by folder and orphan or missing status, and "
        "optionally show direction arrows. Local mode can follow both, "
        "incoming-only, or outgoing-only links at depth 1–3. Note/link totals "
        "stay at the bottom-right. Back and Forward remember the graph's "
        "camera, search, filters, and selection alongside normal notes.\n"
        "\n"
        "## Read Mode\n"
        "Press **Ctrl+E** (or use **Settings → Vault → Read mode**) when you "
        "only want to read. Emerald removes the caret, fully renders every "
        "line, and prevents changes to the vault. Select text and press "
        "**Ctrl+Shift+H** to add or remove a saved `==highlight==`: if any of "
        "the selection is not highlighted Emerald fills the gaps, otherwise it "
        "removes the selected highlight. The plain "
        "**↑** and **↓** keys scroll the page; links, search, folding and "
        "selection/copy keep working, while checkboxes and saved highlights are "
        "the deliberate editable exceptions. Switching between Read and Edit "
        "Mode keeps the same source line at the same viewport position—even on "
        "long wrapped lists—so repeated toggles do not drift. Read Mode is "
        "remembered separately for each vault.\n"
        "\n"
        "Ordinary mouse-wheel steps ease smoothly between pixel positions; "
        "high-resolution trackpad movement stays native and direct.\n"
        "\n"
        "## Getting around\n"
        "- **Title** — the first line above the body is the file name (without "
        "`.md`); edit it to rename the note.\n"
        "- **Sidebar** — notes live in a folder tree. Right-click to create or "
        "delete notes and folders, drag to move them, Shift/Ctrl-click to select "
        "several at once, and single-click a folder to fold it. Choose name or "
        "modified-time ordering under **Settings → Vault → File order**. Collapse the whole "
        "sidebar with **Ctrl+\\** (or click the divider) and again to bring it "
        "back.\n"
        "- **Narrow windows** — at compact widths, Notes and the editor become "
        "separate full-width views. Use the Notes / Editor controls to switch; "
        "new-note, vault-search and gear controls remain in the editor bar.\n"
        "- **History** — the back / forward arrows (Alt+Left / Alt+Right "
        "or the mouse side buttons) walk back and forward through the notes "
        "you've opened.\n"
        "- **Find in note** — Ctrl+F opens a find bar; Enter and Shift+Enter step "
        "through the matches.\n"
        "- **Search vault** — Ctrl+Shift+F searches the text of every note; "
        "Ctrl+P jumps to a note by title.\n"
        "\n"
        "Emerald watches the vault for files changed, created, renamed, or "
        "deleted by other programs and refreshes the tree and search index. If "
        "an open note has unsaved local changes, Emerald keeps them and warns "
        "instead of silently overwriting them.\n"
        "\n"
        "## Settings\n"
        "Open the gear in the bottom-left for **Settings**: the editor font, its "
        "size and width, the line spacing between rows, local spell checking, "
        "the folder new notes are "
        "created in, a Home note to open at launch, a Templates folder, Read "
        "Mode, Broken Links, Graph View, and automatic mascot controls. Vault "
        "choices are stored separately for each vault without adding metadata "
        "files to it. The same menu has **New "
        "Vault…** to start a fresh vault, **Delete Note** to remove the open one "
        "(it asks first), and **Check for Updates…** to fetch and install the "
        "latest release. **Switch Vault…** jumps between vaults in the same "
        "folder, and **Insert Template…** drops in a template — both live in the "
        "menu. Edits save themselves a moment after you stop typing — Ctrl+S "
        "forces a save.\n"
        "\n"
        "### Spelling\n"
        "US English is included and works offline. Misspelled prose is "
        "underlined after the caret leaves the word; code, math, URLs, HTML, "
        "images, and wiki-link targets are ignored. Right-click an underlined "
        "word for suggestions, **Add to personal dictionary**, or **Ignore for "
        "this session**. Under **Settings → Spelling**, **Manage…** can download "
        "verified Italian, German, French, and Spanish dictionaries from "
        "versioned Emerald releases. Packs whose verified content changed are "
        "shown as **Update available**. Language "
        "packs and personal words are stored in Emerald's application-data "
        "folder, never in the vault.\n"
        "\n"
        "## Other shortcuts\n"
        "More keys to control the app workflow (on macOS, Ctrl is ⌘):\n"
        "- **Ctrl+O** — Open a Vault\n"
        "- **Ctrl+Shift+O** — Quick-switch to another vault in the same folder\n"
        "- **Ctrl+N** — Create a new file\n"
        "- **Ctrl+T** — Insert a template at the cursor\n"
        "- **Ctrl+Shift+I** — Insert one or more image attachments\n"
        "- **F2** — Rename the current note\n"
        "- **Ctrl+Shift+Backspace** — Delete the current note (asks first)\n"
        "- **Ctrl+S** — Save the current file (Emerald has auto-save)\n"
        "- **Ctrl+F** — Find text in the current note\n"
        "- **Ctrl+Shift+F** — Perform a Vault search\n"
        "- **Ctrl+Shift+B** — Review broken links\n"
        "- **Ctrl+Shift+G** — Open Graph View\n"
        "- **Ctrl+E** — Toggle Read Mode for this vault\n"
        "- **Ctrl+Shift+H** — Highlight or unhighlight selected Read Mode text\n"
        "- **Ctrl+P** — Open the file picker\n"
        "- **Ctrl+,** — Open Settings\n"
        "- **Ctrl+Q** — Close Emerald\n"
        "- **Ctrl++** / **Ctrl+-** — Increase / decrease the font size "
        "(**Ctrl+0** resets it)\n"
        "- **Ctrl+\\** — Toggle the sidebar (collapse / reopen the left pane)\n"
        "- **Ctrl+G** — Open the mascot gallery (every note's creature at a "
        "glance)\n"
        "- **Ctrl+M** — Generate (or re-roll) this note's mascot\n"
        "- **Ctrl+Shift+M** — Delete this note's mascot\n"
        "- Hold **Alt+X** — Show the keyboard shortcut cheatsheet; release "
        "either key to close it\n"
        "- **Alt+←** — Back in the history\n"
        "- **Alt+→** — Next in the history\n");
}

const QIcon &chevronIcon(bool expanded);

struct NoteTreeNode {
    QString text;
    QString path;
    QString dir;
    QDateTime modified;
    NoteTreeNode *parent = nullptr;
    std::vector<std::unique_ptr<NoteTreeNode>> children;
    int row = 0;

    bool isFolder() const { return !dir.isEmpty(); }
};

class NoteTreeModel : public QAbstractItemModel {
public:
    explicit NoteTreeModel(QObject *parent = nullptr) : QAbstractItemModel(parent) {}

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override {
        if (column != 0 || row < 0)
            return QModelIndex();
        const NoteTreeNode *parentNode = nodeFor(parent);
        if (row >= static_cast<int>(parentNode->children.size()))
            return QModelIndex();
        return createIndex(row, column, parentNode->children.at(row).get());
    }

    QModelIndex parent(const QModelIndex &child) const override {
        if (!child.isValid())
            return QModelIndex();
        const auto *node = static_cast<NoteTreeNode *>(child.internalPointer());
        if (!node || !node->parent || node->parent == &m_root)
            return QModelIndex();
        return createIndex(node->parent->row, 0, node->parent);
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return nodeFor(parent)->children.size();
    }

    int columnCount(const QModelIndex & = QModelIndex()) const override { return 1; }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid())
            return QVariant();
        const auto *node = static_cast<NoteTreeNode *>(index.internalPointer());
        if (!node)
            return QVariant();
        if (role == Qt::DisplayRole)
            return node->text;
        if (role == Qt::DecorationRole && node->isFolder())
            return chevronIcon(m_expandedDirs.contains(node->dir));
        if (role == kPathRole)
            return node->path;
        if (role == kDirRole)
            return node->dir;
        return QVariant();
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override {
        if (!index.isValid())
            return Qt::ItemIsDropEnabled;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled |
               Qt::ItemIsDropEnabled;
    }

    QStringList mimeTypes() const override {
        return {QStringLiteral("application/x-emerald-note-tree")};
    }

    QMimeData *mimeData(const QModelIndexList &indexes) const override {
        auto *mime = new QMimeData;
        QStringList paths;
        for (const QModelIndex &index : indexes) {
            if (index.column() != 0)
                continue;
            const QString dir = data(index, kDirRole).toString();
            const QString path = dir.isEmpty() ? data(index, kPathRole).toString() : dir;
            if (!path.isEmpty() && !paths.contains(path))
                paths << path;
        }
        mime->setData(QStringLiteral("application/x-emerald-note-tree"),
                      paths.join(QLatin1Char('\n')).toUtf8());
        return mime;
    }

    Qt::DropActions supportedDragActions() const override { return Qt::MoveAction; }
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }

    void rebuild(const QString &rootPath, const QStringList &folders,
                 const QVector<Note> &notes, const QSet<QString> &expandedDirs,
                 NoteTreeSort sort) {
        beginResetModel();
        m_root.children.clear();
        m_noteNodes.clear();
        m_dirNodes.clear();
        m_expandedDirs = expandedDirs;
        m_sort = sort;

        const QDir rootDir(rootPath);
        auto appendChild = [](NoteTreeNode *parent, const QString &text,
                              const QString &path, const QString &dir,
                              const QDateTime &modified = QDateTime()) {
            auto child = std::make_unique<NoteTreeNode>();
            child->text = text;
            child->path = path;
            child->dir = dir;
            child->modified = modified;
            child->parent = parent;
            child->row = parent->children.size();
            NoteTreeNode *raw = child.get();
            parent->children.push_back(std::move(child));
            return raw;
        };

        std::function<NoteTreeNode *(const QString &)> ensure =
            [&](const QString &rel) -> NoteTreeNode * {
            if (rel.isEmpty() || rel == QStringLiteral("."))
                return &m_root;
            if (auto it = m_relDirNodes.constFind(rel); it != m_relDirNodes.constEnd())
                return it.value();
            const int slash = rel.lastIndexOf(QLatin1Char('/'));
            NoteTreeNode *parent =
                ensure(slash < 0 ? QString() : rel.left(slash));
            const QString text = slash < 0 ? rel : rel.mid(slash + 1);
            NoteTreeNode *node =
                appendChild(parent, text, QString(), rootDir.filePath(rel));
            m_relDirNodes.insert(rel, node);
            m_dirNodes.insert(node->dir, node);
            return node;
        };

        m_relDirNodes.clear();
        for (const QString &rel : folders)
            ensure(rel);

        for (const Note &note : notes) {
            const QString dirRel =
                rootDir.relativeFilePath(QFileInfo(note.path).absolutePath());
            NoteTreeNode *leaf =
                appendChild(ensure(dirRel), note.title, note.path, QString(),
                            QFileInfo(note.path).lastModified());
            m_noteNodes.insert(note.path, leaf);
        }

        sortChildren(&m_root);
        endResetModel();
    }

    void updateModificationTime(const QString &path,
                                const QDateTime &modified) {
        NoteTreeNode *node = m_noteNodes.value(path, nullptr);
        if (!node)
            return;
        node->modified = modified;
        if (m_sort != NoteTreeSort::ModifiedNewest &&
            m_sort != NoteTreeSort::ModifiedOldest)
            return;

        // Only one sibling group can move after a save. A layout change keeps
        // selection and expansion intact and avoids rebuilding/stat-ing the
        // complete vault on every autosave.
        const QModelIndexList before = persistentIndexList();
        emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
        sortChildren(node->parent, false);
        QModelIndexList after;
        after.reserve(before.size());
        for (const QModelIndex &index : before) {
            auto *persistentNode =
                static_cast<NoteTreeNode *>(index.internalPointer());
            after << (persistentNode
                          ? createIndex(persistentNode->row, index.column(),
                                        persistentNode)
                          : QModelIndex());
        }
        changePersistentIndexList(before, after);
        emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
    }

    QModelIndex indexForPath(const QString &path) const {
        return indexForNode(m_noteNodes.value(path, nullptr));
    }

    QModelIndex indexForDir(const QString &dir) const {
        return indexForNode(m_dirNodes.value(dir, nullptr));
    }

    QSet<QString> expandedDirs() const { return m_expandedDirs; }

    void setDirExpanded(const QString &dir, bool expanded) {
        if (dir.isEmpty())
            return;
        if (expanded)
            m_expandedDirs.insert(dir);
        else
            m_expandedDirs.remove(dir);
        const QModelIndex idx = indexForDir(dir);
        if (idx.isValid())
            emit dataChanged(idx, idx, {Qt::DecorationRole});
    }

private:
    static int compareText(const QString &left, const QString &right) {
        const int folded = left.compare(right, Qt::CaseInsensitive);
        return folded != 0 ? folded : left.compare(right, Qt::CaseSensitive);
    }

    bool less(const std::unique_ptr<NoteTreeNode> &left,
              const std::unique_ptr<NoteTreeNode> &right) const {
        // Keep the hierarchy easy to scan: folders stay above notes and
        // alphabetic, while the selected order applies to notes.
        if (left->isFolder() != right->isFolder())
            return left->isFolder();
        if (left->isFolder())
            return compareText(left->text, right->text) < 0;

        if (m_sort == NoteTreeSort::ModifiedNewest &&
            left->modified != right->modified)
            return left->modified > right->modified;
        if (m_sort == NoteTreeSort::ModifiedOldest &&
            left->modified != right->modified)
            return left->modified < right->modified;

        const int textOrder = compareText(left->text, right->text);
        if (textOrder != 0)
            return m_sort == NoteTreeSort::NameDescending ? textOrder > 0
                                                          : textOrder < 0;
        return left->path < right->path;
    }

    void sortChildren(NoteTreeNode *parent, bool recursive = true) {
        std::sort(parent->children.begin(), parent->children.end(),
                  [this](const auto &left, const auto &right) {
                      return less(left, right);
                  });
        for (int row = 0;
             row < static_cast<int>(parent->children.size()); ++row) {
            NoteTreeNode *child = parent->children.at(row).get();
            child->row = row;
            if (recursive)
                sortChildren(child);
        }
    }

    const NoteTreeNode *nodeFor(const QModelIndex &index) const {
        if (!index.isValid())
            return &m_root;
        return static_cast<NoteTreeNode *>(index.internalPointer());
    }

    QModelIndex indexForNode(NoteTreeNode *node) const {
        if (!node || node == &m_root)
            return QModelIndex();
        return createIndex(node->row, 0, node);
    }

    NoteTreeNode m_root;
    QHash<QString, NoteTreeNode *> m_noteNodes;
    QHash<QString, NoteTreeNode *> m_dirNodes;
    QHash<QString, NoteTreeNode *> m_relDirNodes;
    QSet<QString> m_expandedDirs;
    NoteTreeSort m_sort = NoteTreeSort::NameAscending;
};

// A tree that draws a faint vertical guide for each nesting level, so notes
// inside a folder read clearly as sub-items.
class NoteTreeView : public QTreeView {
public:
    using QTreeView::QTreeView;
    // Called with (source note/folder paths, destination folder path; empty
    // dest = vault root) when a selection is dropped.
    std::function<void(const QStringList &, const QString &)> onMove;

protected:
    void dropEvent(QDropEvent *event) override {
        // The whole current selection moves together.
        QStringList srcPaths;
        if (selectionModel()) {
            const QModelIndexList rows = selectionModel()->selectedRows();
            for (const QModelIndex &idx : rows) {
                const QString dirRole = idx.data(kDirRole).toString();
                const QString p =
                    dirRole.isEmpty() ? idx.data(kPathRole).toString() : dirRole;
                if (!p.isEmpty())
                    srcPaths << p;
            }
        }
        if (srcPaths.isEmpty()) {
            const QByteArray encoded =
                event->mimeData()->data(QStringLiteral("application/x-emerald-note-tree"));
            for (const QString &p :
                 QString::fromUtf8(encoded).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                if (!p.isEmpty())
                    srcPaths << p;
            }
        }
        if (srcPaths.isEmpty()) {
            event->ignore();
            return;
        }
        const QStringList roots = topLevelPaths(srcPaths);
        QString destDir; // empty => root
        const QModelIndex target = indexAt(event->position().toPoint());
        if (target.isValid()) {
            const QString d = target.data(kDirRole).toString();
            destDir = d.isEmpty()
                          ? QFileInfo(target.data(kPathRole).toString()).absolutePath()
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
        painter->fillRect(
            rect, AppTheme::color(QColor(0x10, 0x11, 0x13)));
        QTreeView::drawBranches(painter, rect, index);
        int depth = 0;
        for (QModelIndex a = index.parent(); a.isValid(); a = a.parent())
            ++depth;
        if (depth == 0)
            return;
        const int ind = indentation();
        painter->save();
        painter->setPen(AppTheme::color(QColor(0x1f, 0x47, 0x33)));
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
    QPen pen(AppTheme::color(QColor("#52b58a")));
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
    static AppTheme::Id cachedTheme = AppTheme::current();
    static QIcon up = makeChevron(true);
    static QIcon down = makeChevron(false);
    if (cachedTheme != AppTheme::current()) {
        cachedTheme = AppTheme::current();
        up = makeChevron(true);
        down = makeChevron(false);
    }
    return expanded ? up : down;
}

// Chrome-style back/forward arrow: a horizontal shaft tipped with an arrowhead,
// drawn thin with rounded ends to mirror the browser toolbar glyphs.
QIcon makeNavArrow(bool back) {
    QPixmap pm(22, 22);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(AppTheme::color(QColor("#a9c8b8")));
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

QString readNoteForIndex(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QString text = QString::fromUtf8(f.readAll());
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

quint64 contentFingerprint(const QString &content) {
    const quint64 hi = qHash(content, 0x9e3779b97f4a7c15ULL);
    const quint64 lo = qHash(content, 0x2545f4914f6cdd1ULL);
    return (hi << 32) ^ lo ^ (hi >> 16);
}

QString attachmentBaseName(QString name) {
    name = name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("image");
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || QStringLiteral("/\\:*?\"<>|").contains(c))
            out += QLatin1Char('-');
        else if (c.isSpace())
            out += QLatin1Char('-');
        else
            out += c;
    }
    while (out.contains(QStringLiteral("--")))
        out.replace(QStringLiteral("--"), QStringLiteral("-"));
    while (out.startsWith(QLatin1Char('-')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out.isEmpty() ? QStringLiteral("image") : out;
}

QString markdownImageTarget(QString relPath) {
    relPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QString::fromLatin1(QUrl::toPercentEncoding(
        relPath, QByteArrayLiteral("/._-~")));
}

QString markdownAltText(QString text) {
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('['), QLatin1Char('('));
    text.replace(QLatin1Char(']'), QLatin1Char(')'));
    text = text.trimmed();
    return text.isEmpty() ? QStringLiteral("Image") : text;
}

QString elideRightAscii(const QString &text, const QFontMetrics &fm, int width) {
    if (text.isEmpty() || fm.horizontalAdvance(text) <= width)
        return text;
    const QString dots = QStringLiteral("...");
    const int dotsWidth = fm.horizontalAdvance(dots);
    if (dotsWidth >= width)
        return dots;

    int lo = 0;
    int hi = text.size();
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (fm.horizontalAdvance(text.left(mid)) + dotsWidth <= width)
            lo = mid;
        else
            hi = mid - 1;
    }
    return text.left(lo) + dots;
}

class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(const QString &text, QWidget *parent = nullptr)
        : QLabel(parent), m_fullText(text) {
        updateText();
    }

    void setFullText(const QString &text) {
        if (m_fullText == text)
            return;
        m_fullText = text;
        updateText();
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        updateText();
    }

private:
    void updateText() {
        QLabel::setText(elideRightAscii(m_fullText, fontMetrics(), width()));
    }

    QString m_fullText;
};

void prepareDialogButtons(QDialogButtonBox *buttons) {
    for (auto *button : buttons->findChildren<QPushButton *>()) {
        button->setIcon(QIcon());
        button->setAutoDefault(false);
        button->setDefault(false);
        const QDialogButtonBox::ButtonRole role = buttons->buttonRole(button);
        const bool primary = role == QDialogButtonBox::AcceptRole ||
                             role == QDialogButtonBox::YesRole ||
                             role == QDialogButtonBox::ApplyRole;
        button->setProperty("dialogRole",
                            primary ? QStringLiteral("primary")
                                    : QStringLiteral("secondary"));
    }
}

QVBoxLayout *createEmeraldDialogRoot(QDialog *dlg, const QString &title,
                                     const QString &subtitle = QString(),
                                     const QString &objectName =
                                         QStringLiteral("compactDialog")) {
    dlg->setObjectName(objectName);
    dlg->setProperty("emeraldDialog", true);
    dlg->setWindowTitle(title);
    dlg->setSizeGripEnabled(false);
    QWidget *parent = dlg->parentWidget();
    const bool compact = parent && parent->width() <= kMobileBreakpoint;
    const int labelWidth =
        compact ? qMax(240, parent->width() - 64) : 460;

    auto *root = new QVBoxLayout(dlg);
    root->setContentsMargins(compact ? 16 : 24, compact ? 18 : 22,
                             compact ? 16 : 24, compact ? 16 : 20);
    root->setSpacing(compact ? 10 : 12);
    root->setAlignment(Qt::AlignCenter);

    auto *heading = new QLabel(title, dlg);
    heading->setObjectName(QStringLiteral("settingsTitle"));
    heading->setAlignment(Qt::AlignCenter);
    heading->setMaximumWidth(labelWidth);
    heading->setWordWrap(true);
    root->addWidget(heading);

    if (!subtitle.isEmpty()) {
        auto *sub = new QLabel(subtitle, dlg);
        sub->setObjectName(QStringLiteral("settingsSubtitle"));
        sub->setAlignment(Qt::AlignCenter);
        sub->setMaximumWidth(labelWidth);
        sub->setWordWrap(true);
        root->addWidget(sub);
    }
    return root;
}

bool runFolderNameDialog(QWidget *parent, QString *out) {
    QDialog dlg(parent);
    auto *root = createEmeraldDialogRoot(
        &dlg, QObject::tr("New Folder"),
        QObject::tr("Create a folder in the selected location."),
        QStringLiteral("newFolderDialog"));

    auto *input = new QLineEdit(&dlg);
    input->setObjectName(QStringLiteral("dialogInput"));
    input->setPlaceholderText(QObject::tr("Folder name"));
    const int inputWidth =
        qBound(220, parent ? parent->width() - 64 : 300, 300);
    input->setFixedWidth(inputWidth);
    root->addWidget(input, 0, Qt::AlignCenter);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    if (auto *ok = buttons->button(QDialogButtonBox::Ok)) {
        ok->setText(QObject::tr("Create"));
        ok->setEnabled(false);
    }
    buttons->setCenterButtons(true);
    prepareDialogButtons(buttons);
    root->addWidget(buttons, 0, Qt::AlignCenter);

    QObject::connect(input, &QLineEdit::textChanged, &dlg, [buttons](const QString &text) {
        if (auto *ok = buttons->button(QDialogButtonBox::Ok))
            ok->setEnabled(!text.trimmed().isEmpty());
    });
    QObject::connect(input, &QLineEdit::returnPressed, &dlg, [&dlg, buttons] {
        if (auto *ok = buttons->button(QDialogButtonBox::Ok); ok && ok->isEnabled())
            dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.adjustSize();
    dlg.setFixedSize(dlg.sizeHint());
    input->setFocus();
    if (dlg.exec() != QDialog::Accepted)
        return false;
    *out = input->text().trimmed();
    return true;
}

bool runTrashDialog(QWidget *parent, const QString &question) {
    QDialog dlg(parent);
    auto *root = createEmeraldDialogRoot(
        &dlg, QObject::tr("Move to Trash"), question,
        QStringLiteral("trashDialog"));

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    if (auto *ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(QObject::tr("Move to Trash"));
    buttons->setCenterButtons(true);
    prepareDialogButtons(buttons);
    if (auto *ok = buttons->button(QDialogButtonBox::Ok))
        ok->setProperty("dialogRole", QStringLiteral("destructive"));
    root->addWidget(buttons, 0, Qt::AlignCenter);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.adjustSize();
    dlg.setFixedSize(dlg.sizeHint());
    return dlg.exec() == QDialog::Accepted;
}

bool runVaultNameDialog(QWidget *parent, QString *out) {
    QDialog dlg(parent);
    auto *root = createEmeraldDialogRoot(
        &dlg, QObject::tr("New Vault"),
        QObject::tr("Choose a concise name for the new vault."),
        QStringLiteral("newVaultDialog"));

    auto *input = new QLineEdit(&dlg);
    input->setObjectName(QStringLiteral("dialogInput"));
    input->setPlaceholderText(QObject::tr("Vault name"));
    input->setFixedWidth(qBound(220, parent ? parent->width() - 64 : 300, 300));
    root->addWidget(input, 0, Qt::AlignCenter);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    if (auto *create = buttons->button(QDialogButtonBox::Ok)) {
        create->setText(QObject::tr("Create"));
        create->setEnabled(false);
    }
    buttons->setCenterButtons(true);
    prepareDialogButtons(buttons);
    root->addWidget(buttons, 0, Qt::AlignCenter);

    QObject::connect(input, &QLineEdit::textChanged, &dlg,
                     [buttons](const QString &text) {
        if (auto *create = buttons->button(QDialogButtonBox::Ok))
            create->setEnabled(!text.trimmed().isEmpty());
    });
    QObject::connect(input, &QLineEdit::returnPressed, &dlg, [&dlg, buttons] {
        if (auto *create = buttons->button(QDialogButtonBox::Ok);
            create && create->isEnabled())
            dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg,
                     &QDialog::reject);

    dlg.adjustSize();
    dlg.setFixedSize(dlg.sizeHint());
    input->setFocus();
    if (dlg.exec() != QDialog::Accepted)
        return false;
    *out = input->text().trimmed();
    return true;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    VaultSettings::migrateLegacyForLastVault();
    buildActions();
    buildUi();
    // A QApplication-level filter sees the hold chord before whichever child
    // currently owns focus (editor, title, sidebar, or graph). The same filter
    // continues to handle the handful of child-specific events below.
    qApp->installEventFilter(this);
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
        rescanVaultIncremental(); // keeps the open note selected and folders expanded
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
    connect(m_editor, &MarkdownEditor::sourceChanged, this, [this] {
        if (!m_loading)
            m_saveTimer->start();
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

MainWindow::~MainWindow() {
    if (qApp)
        qApp->removeEventFilter(this);
    if (m_indexThread) {
        m_indexThread->requestInterruption();
        m_indexThread->wait();
        m_indexThread = nullptr;
    }
    delete m_vault;
}

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
    connect(m_editor, &MarkdownEditor::imageFilesInserted, this,
            &MainWindow::insertImagesFromFiles);
    connect(m_editor, &MarkdownEditor::imagePasted, this,
            &MainWindow::insertPastedImage);
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
    m_titleEdit->setAlignment(Qt::AlignCenter);
    connect(m_titleEdit, &QLineEdit::editingFinished, this,
            [this] { renameCurrent(m_titleEdit->text()); });
    // Enter on the title drops the caret onto the first body line, ready to type.
    connect(m_titleEdit, &QLineEdit::returnPressed, this, [this] {
        if (m_currentPath.isEmpty()) {
            const QString title = m_titleEdit->text().trimmed();
            if (title.isEmpty()) {
                QTextCursor c = m_editor->textCursor();
                c.setPosition(m_editor->firstContentPosition());
                m_editor->setTextCursor(c);
                m_editor->setFocus();
                return;
            }
            if (!Vault::isValidTitle(title)) {
                notify(tr("Enter a valid note title"), 2500);
                m_titleEdit->setFocus();
                return;
            }
            saveCurrent();
            if (m_currentPath.isEmpty()) {
                m_titleEdit->setFocus();
                m_titleEdit->selectAll();
                return;
            }
        }
        QTextCursor c = m_editor->textCursor();
        c.setPosition(m_editor->firstContentPosition()); // skip a hidden header line
        m_editor->setTextCursor(c);
        m_editor->setFocus();
    });

    // The editor and graph are first-class pages in the same central pane. The
    // note page keeps its comfortable width-capped text column; the graph page
    // uses the complete pane and never opens an overlapping window.
    auto *center = new QWidget(this);
    m_centerPane = center;
    center->setMinimumWidth(0);
    center->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    m_mobileEditorBar = new QWidget(center);
    m_mobileEditorBar->setObjectName(QStringLiteral("mobileEditorBar"));
    auto *mobileBarLayout = new QHBoxLayout(m_mobileEditorBar);
    mobileBarLayout->setContentsMargins(8, 4, 8, 4);
    mobileBarLayout->setSpacing(4);
    auto makeMobileBarButton = [this](QWidget *parent, const QString &text,
                                      const QString &tooltip) {
        auto *button = new QToolButton(parent);
        button->setObjectName(QStringLiteral("mobileBarButton"));
        button->setText(text);
        button->setToolTip(tooltip);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        return button;
    };
    m_mobileNotesButton =
        makeMobileBarButton(m_mobileEditorBar, tr("Notes"), tr("Show notes"));
    connect(m_mobileNotesButton, &QToolButton::clicked, this,
            &MainWindow::showMobileNotes);
    auto *mobileNewButton =
        makeMobileBarButton(m_mobileEditorBar, QStringLiteral("+"), tr("New Note"));
    connect(mobileNewButton, &QToolButton::clicked, this, &MainWindow::newNote);
    auto *mobileSearchButton =
        makeMobileBarButton(m_mobileEditorBar, tr("Search"), tr("Search Vault"));
    connect(mobileSearchButton, &QToolButton::clicked, this,
            &MainWindow::openSearch);
    auto *mobileMenuButton =
        makeMobileBarButton(m_mobileEditorBar, QStringLiteral("⚙"), tr("Menu"));
    mobileMenuButton->setPopupMode(QToolButton::InstantPopup);
    mobileMenuButton->setMenu(m_gearMenu);
    mobileBarLayout->addWidget(m_mobileNotesButton);
    mobileBarLayout->addStretch();
    mobileBarLayout->addWidget(mobileNewButton);
    mobileBarLayout->addWidget(mobileSearchButton);
    mobileBarLayout->addWidget(mobileMenuButton);
    m_mobileEditorBar->hide();
    centerLayout->addWidget(m_mobileEditorBar);

    m_centerColumn = new QWidget(this);
    m_centerColumn->setObjectName(QStringLiteral("editorColumn"));
    auto *colLayout = new QVBoxLayout(m_centerColumn);
    colLayout->setContentsMargins(0, 0, 0, 0);
    colLayout->setSpacing(0);
    colLayout->addWidget(m_titleEdit);
    colLayout->addWidget(m_editor, 1);
    m_centerColumn->setMaximumWidth(m_editorColumnWidth);
    m_centerColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_notePage = new QWidget(this);
    m_notePage->setObjectName(QStringLiteral("notePage"));
    auto *row = new QHBoxLayout(m_notePage);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addStretch(0);
    row->addWidget(m_centerColumn, 1);
    row->addStretch(0);

    m_graphPage = new GraphPage(this);
    connect(m_graphPage, &GraphPage::noteActivated, this,
            [this](const QString &path) { openNoteByPath(path); });
    connect(m_graphPage, &GraphPage::stateChanged, this,
            [this](const QString &state) {
                if (m_vault)
                    VaultSettings::setValue(m_vault->root(),
                                            QStringLiteral("graphState"), state);
            });
    m_pageStack = new QStackedWidget(center);
    m_pageStack->setObjectName(QStringLiteral("workspacePages"));
    m_pageStack->setFrameShape(QFrame::NoFrame);
    m_pageStack->addWidget(m_notePage);
    m_pageStack->addWidget(m_graphPage);
    m_pageStack->setCurrentWidget(m_notePage);
    centerLayout->addWidget(m_pageStack, 1);

    m_noteTreeModel = new NoteTreeModel(this);
    auto *tree = new NoteTreeView(this);
    m_noteTree = tree;
    m_noteTree->setObjectName(QStringLiteral("noteTree"));
    m_noteTree->setModel(m_noteTreeModel);
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
    connect(m_noteTree, &QTreeView::clicked, this,
            &MainWindow::onTreeIndexClicked);
    connect(m_noteTree, &QTreeView::customContextMenuRequested, this,
            &MainWindow::onTreeContextMenu);
    // Folders carry an up/down chevron instead of a folder icon; keep it in
    // sync with the fold state.
    connect(m_noteTree, &QTreeView::expanded, this,
            [this](const QModelIndex &idx) {
                auto *model = static_cast<NoteTreeModel *>(m_noteTreeModel);
                model->setDirExpanded(idx.data(kDirRole).toString(), true);
            });
    connect(m_noteTree, &QTreeView::collapsed, this,
            [this](const QModelIndex &idx) {
                auto *model = static_cast<NoteTreeModel *>(m_noteTreeModel);
                model->setDirExpanded(idx.data(kDirRole).toString(), false);
            });

    // Sidebar header: the current vault name with the back/forward arrows on
    // the right (replaces the old top toolbar).
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("sideHeader"));
    auto *hrow = new QHBoxLayout(header);
    hrow->setContentsMargins(10, 4, 4, 4);
    hrow->setSpacing(2);
    m_sideTitle = new ElidedLabel(tr("Notes"), header);
    m_sideTitle->setObjectName(QStringLiteral("sideTitle"));
    m_sideTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_mobileEditorButton =
        makeMobileBarButton(header, tr("Editor"), tr("Show editor"));
    m_mobileEditorButton->hide();
    connect(m_mobileEditorButton, &QToolButton::clicked, this,
            &MainWindow::showMobileEditor);
    auto *backBtn = new QToolButton(header);
    backBtn->setObjectName(QStringLiteral("navButton"));
    backBtn->setDefaultAction(m_backAction);
    backBtn->setIconSize(QSize(22, 22));
    auto *fwdBtn = new QToolButton(header);
    fwdBtn->setObjectName(QStringLiteral("navButton"));
    fwdBtn->setDefaultAction(m_forwardAction);
    fwdBtn->setIconSize(QSize(22, 22));
    hrow->addWidget(m_sideTitle, 1);
    hrow->addWidget(m_mobileEditorButton);
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
    m_splitter->setProperty("graphActive", false);
    m_splitter->addWidget(side);
    m_splitter->addWidget(center);
    m_splitter->setCollapsible(0, true);
    m_splitter->setCollapsible(1, false);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    // A wide handle for an easy drag/click target; the QSS paints only a thin
    // line inside it so the divider still looks slim.
    m_splitter->setHandleWidth(kDesktopSplitterHandleWidth);
    m_splitter->setSizes({kDefaultSidebarWidth, 900});
    setCentralWidget(m_splitter);
    // Clicking (not dragging) the handle collapses / reopens the sidebar.
    m_splitHandle = m_splitter->handle(1);

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
    connect(m_searchPopup, &SearchPopup::brokenLinkRequested, this,
            &MainWindow::openBrokenLinkSource);

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
    m_toast = new QLabel(m_centerPane);
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

    buildShortcutCheatsheet();
}

void MainWindow::buildShortcutCheatsheet() {
    struct ShortcutRow {
        QString action;
        QString keys;
    };
    using ShortcutRows = QList<ShortcutRow>;

    const auto key = [](const QKeySequence &sequence) {
        return sequence.toString(QKeySequence::NativeText);
    };
    const auto chord = [&key](Qt::KeyboardModifiers modifiers, Qt::Key value) {
        return key(QKeySequence(modifiers | value));
    };
    const auto joined = [](const QString &left, const QString &right) {
        return left + QStringLiteral("  /  ") + right;
    };

    const ShortcutRows workspace = {
        {tr("New note"), key(QKeySequence(QKeySequence::New))},
        {tr("Open vault"), key(QKeySequence(QKeySequence::Open))},
        {tr("Switch vault"),
         chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_O)},
        {tr("Save now"), key(QKeySequence(QKeySequence::Save))},
        {tr("Rename note"), key(QKeySequence(Qt::Key_F2))},
        {tr("Delete note"),
         chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Backspace)},
        {tr("Insert template"), chord(Qt::ControlModifier, Qt::Key_T)},
        {tr("Insert image"),
         chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_I)},
        {tr("Settings"), chord(Qt::ControlModifier, Qt::Key_Comma)},
        {tr("Toggle sidebar"),
         chord(Qt::ControlModifier, Qt::Key_Backslash)},
        {tr("Quit Emerald"), chord(Qt::ControlModifier, Qt::Key_Q)},
    };
    const ShortcutRows navigation = {
        {tr("Go to note"), chord(Qt::ControlModifier, Qt::Key_P)},
        {tr("Find in note"), key(QKeySequence(QKeySequence::Find))},
        {tr("Search vault"),
         chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_F)},
        {tr("Review broken links"),
         chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_B)},
        {tr("Open Graph View"),
         chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_G)},
        {tr("Toggle Read Mode"), chord(Qt::ControlModifier, Qt::Key_E)},
        {tr("Back / forward"),
         joined(chord(Qt::AltModifier, Qt::Key_Left),
                chord(Qt::AltModifier, Qt::Key_Right))},
        {tr("Quick Jump to link"), tr("Hold Alt, then hint")},
        {tr("Shortcut cheatsheet"),
         tr("Hold %1").arg(chord(Qt::AltModifier, Qt::Key_X))},
    };
    const ShortcutRows editing = {
        {tr("Undo / redo"),
         joined(key(QKeySequence(QKeySequence::Undo)),
                key(QKeySequence(QKeySequence::Redo)))},
        {tr("Bold / italic"),
         joined(chord(Qt::ControlModifier, Qt::Key_B),
                chord(Qt::ControlModifier, Qt::Key_I))},
        {tr("Insert link"), chord(Qt::ControlModifier, Qt::Key_K)},
        {tr("Heading level 1–6"),
         QStringLiteral("%1 … %2")
             .arg(chord(Qt::ControlModifier, Qt::Key_1),
                  chord(Qt::ControlModifier, Qt::Key_6))},
        {tr("Select current line"), chord(Qt::ControlModifier, Qt::Key_L)},
        {tr("New line below"), chord(Qt::ControlModifier, Qt::Key_Return)},
        {tr("Move line up / down"),
         joined(chord(Qt::AltModifier, Qt::Key_Up),
                chord(Qt::AltModifier, Qt::Key_Down))},
        {tr("Indent / outdent list"),
         joined(key(QKeySequence(Qt::Key_Tab)),
                key(QKeySequence(Qt::ShiftModifier | Qt::Key_Tab)))},
        {tr("Next / previous table cell"),
         joined(key(QKeySequence(Qt::Key_Tab)),
                key(QKeySequence(Qt::ShiftModifier | Qt::Key_Tab)))},
    };
    const ShortcutRows viewAndGraph = {
        {tr("Highlight Read Mode selection"),
         chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_H)},
        {tr("Font size up / down"),
         joined(chord(Qt::ControlModifier, Qt::Key_Plus),
                chord(Qt::ControlModifier, Qt::Key_Minus))},
        {tr("Reset font size"), chord(Qt::ControlModifier, Qt::Key_0)},
        {tr("Fit / reset graph camera"),
         joined(key(QKeySequence(Qt::Key_F)),
                key(QKeySequence(Qt::Key_0)))},
        {tr("Focus graph search"),
         joined(key(QKeySequence(Qt::Key_Slash)),
                key(QKeySequence(QKeySequence::Find)))},
        {tr("Open selected graph note"), key(QKeySequence(Qt::Key_Return))},
        {tr("Mascot gallery"), chord(Qt::ControlModifier, Qt::Key_G)},
        {tr("Generate mascot"), chord(Qt::ControlModifier, Qt::Key_M)},
        {tr("Delete mascot"),
         chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_M)},
    };

    m_shortcutCheatsheet = new QFrame(this);
    m_shortcutCheatsheet->setObjectName(QStringLiteral("shortcutCheatsheet"));
    m_shortcutCheatsheet->setFrameShape(QFrame::StyledPanel);
    m_shortcutCheatsheet->setFocusPolicy(Qt::NoFocus);

    // X11-style repeat may arrive as release/press pairs. Delay a real X-up by
    // one short frame; the immediately following repeat press cancels it,
    // while a physical release still dismisses the hold overlay promptly.
    m_shortcutReleaseTimer = new QTimer(this);
    m_shortcutReleaseTimer->setSingleShot(true);
    m_shortcutReleaseTimer->setInterval(35);
    connect(m_shortcutReleaseTimer, &QTimer::timeout, this,
            &MainWindow::hideShortcutCheatsheet);

    auto *root = new QVBoxLayout(m_shortcutCheatsheet);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);

    auto *title = new QLabel(tr("Keyboard shortcuts"), m_shortcutCheatsheet);
    title->setObjectName(QStringLiteral("shortcutCheatsheetTitle"));
    title->setAlignment(Qt::AlignCenter);
    root->addWidget(title);

    auto *hint = new QLabel(
        tr("Hold %1 · release either key to close")
            .arg(chord(Qt::AltModifier, Qt::Key_X)),
        m_shortcutCheatsheet);
    hint->setObjectName(QStringLiteral("shortcutCheatsheetHint"));
    hint->setAlignment(Qt::AlignCenter);
    root->addWidget(hint);

    auto *scroll = new QScrollArea(m_shortcutCheatsheet);
    scroll->setObjectName(QStringLiteral("shortcutCheatsheetScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(scroll, 1);

    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("shortcutCheatsheetContent"));
    auto *columns = new QHBoxLayout(content);
    m_shortcutColumnsLayout = columns;
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(12);

    const auto makeSection = [this](QWidget *parent, const QString &heading,
                                    const ShortcutRows &rows) {
        auto *section = new QFrame(parent);
        section->setObjectName(QStringLiteral("shortcutSection"));
        auto *layout = new QVBoxLayout(section);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(1);

        auto *sectionTitle = new QLabel(heading, section);
        sectionTitle->setObjectName(QStringLiteral("shortcutSectionTitle"));
        layout->addWidget(sectionTitle);

        for (const ShortcutRow &entry : rows) {
            auto *row = new QWidget(section);
            row->setObjectName(QStringLiteral("shortcutRow"));
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);

            auto *action = new QLabel(entry.action, row);
            action->setObjectName(QStringLiteral("shortcutAction"));
            action->setWordWrap(true);
            action->setSizePolicy(QSizePolicy::Expanding,
                                  QSizePolicy::Preferred);
            auto *keys = new QLabel(entry.keys, row);
            keys->setObjectName(QStringLiteral("shortcutKey"));
            keys->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            keys->setTextInteractionFlags(Qt::NoTextInteraction);
            rowLayout->addWidget(action, 1);
            rowLayout->addWidget(keys);
            layout->addWidget(row);
        }
        layout->addStretch();
        return section;
    };

    auto *left = new QWidget(content);
    left->setObjectName(QStringLiteral("shortcutColumn"));
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);
    leftLayout->addWidget(makeSection(left, tr("Workspace"), workspace));
    leftLayout->addWidget(makeSection(left, tr("Editing"), editing));

    auto *right = new QWidget(content);
    right->setObjectName(QStringLiteral("shortcutColumn"));
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);
    rightLayout->addWidget(makeSection(right, tr("Navigate & find"), navigation));
    rightLayout->addWidget(
        makeSection(right, tr("View, graph & mascot"), viewAndGraph));

    columns->addWidget(left, 1);
    columns->addWidget(right, 1);
    scroll->setWidget(content);

    m_shortcutCheatsheet->hide();
    positionShortcutCheatsheet();
}

void MainWindow::positionShortcutCheatsheet() {
    if (!m_shortcutCheatsheet)
        return;
    const int availableWidth = qMax(280, width() - 32);
    const int availableHeight = qMax(260, height() - 32);
    const QSize panel(qMin(920, availableWidth),
                      qMin(680, availableHeight));
    m_shortcutCheatsheet->setGeometry(
        (width() - panel.width()) / 2,
        (height() - panel.height()) / 2,
        panel.width(), panel.height());
    if (m_shortcutColumnsLayout) {
        const QBoxLayout::Direction direction =
            panel.width() < 680 ? QBoxLayout::TopToBottom
                                : QBoxLayout::LeftToRight;
        if (m_shortcutColumnsLayout->direction() != direction) {
            m_shortcutColumnsLayout->setDirection(direction);
            m_shortcutColumnsLayout->invalidate();
        }
    }
}

void MainWindow::showShortcutCheatsheet() {
    if (!m_shortcutCheatsheet)
        return;
    if (m_shortcutReleaseTimer)
        m_shortcutReleaseTimer->stop();
    if (m_editor)
        m_editor->suppressQuickJump();
    m_shortcutCheatsheetHeld = true;
    positionShortcutCheatsheet();
    m_shortcutCheatsheet->show();
    m_shortcutCheatsheet->raise();
}

void MainWindow::hideShortcutCheatsheet() {
    if (m_shortcutReleaseTimer)
        m_shortcutReleaseTimer->stop();
    m_shortcutCheatsheetHeld = false;
    if (m_shortcutCheatsheet)
        m_shortcutCheatsheet->hide();
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
    QWidget *target = m_activePage == PageLocation::Kind::Note
                          ? static_cast<QWidget *>(m_editor)
                          : static_cast<QWidget *>(m_pageStack);
    if (!target || !m_centerPane)
        return;
    const QPoint origin = target->mapTo(m_centerPane, QPoint());
    const int x = origin.x() + (target->width() - m_toast->width()) / 2;
    const int y = origin.y() + target->height() - m_toast->height() - 18;
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
    if (!ensureVaultWritable())
        return;
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
    setAutoMascotOff(m_currentPath, false); // an explicit Generate re-enables auto
    notify(tr("Mascot generated"));
}

void MainWindow::deleteMascot() {
    if (!ensureVaultWritable())
        return;
    if (!m_vault || m_currentPath.isEmpty() || m_editor->mascotSeed() == 0)
        return;
    // Suppress *before* clearing: setMascot(0) edits the document, which emits
    // textChanged -> maybeAutoGenerateMascot synchronously; the flag must already
    // be set so that pass bails instead of instantly recreating the mascot.
    setAutoMascotOff(m_currentPath, true);
    m_editor->setMascot(0); // removes the header line; hides the creature
    notify(tr("Mascot removed"));
}

bool MainWindow::autoMascotOff(const QString &path) const {
    return !path.isEmpty() && QSettings()
        .value(QStringLiteral("mascotAutoOff")).toStringList().contains(path);
}

void MainWindow::setAutoMascotOff(const QString &path, bool off) {
    if (path.isEmpty())
        return;
    QSettings s;
    QStringList paths = s.value(QStringLiteral("mascotAutoOff")).toStringList();
    if (off == paths.contains(path))
        return; // already in the desired state
    if (off)
        paths.append(path);
    else
        paths.removeAll(path);
    s.setValue(QStringLiteral("mascotAutoOff"), paths);
}

void MainWindow::maybeAutoGenerateMascot() {
    if (m_readMode || !m_vault || m_currentPath.isEmpty() ||
        m_editor->mascotSeed() != 0)
        return; // no note, or it already has a mascot
    QSettings s;
    if (!s.value(QStringLiteral("mascotAuto"), false).toBool())
        return;
    if (autoMascotOff(m_currentPath))
        return; // user deleted this note's mascot — leave it manual-only
    const int threshold =
        s.value(QStringLiteral("mascotThreshold"), 100).toInt();
    const int bodyChars =
        qMax(0, m_editor->sourceDocument()->characterCount() - 1 -
                    m_editor->firstContentPosition());
    if (bodyChars < threshold)
        return;
    generateMascot(); // crosses the threshold once
}

void MainWindow::updateMascotActions() {
    const bool notePage = m_activePage == PageLocation::Kind::Note;
    if (m_genMascotAction)
        m_genMascotAction->setEnabled(notePage && !m_readMode && m_vault &&
                                      !m_currentPath.isEmpty());
    if (m_delMascotAction)
        m_delMascotAction->setEnabled(notePage && !m_readMode && m_editor &&
                                      m_editor->mascotSeed() != 0);
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
    dlg.setObjectName(QStringLiteral("mascotGalleryDialog"));
    dlg.setProperty("emeraldDialog", true);
    dlg.setWindowTitle(tr("Mascot Gallery"));
    const bool compactGallery = m_mobileLayout || width() <= kMobileBreakpoint;
    dlg.resize(compactGallery
                   ? QSize(qMax(320, width() - 24), qMax(420, height() - 48))
                   : QSize(640, 620));
    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(compactGallery ? 14 : 20,
                              compactGallery ? 14 : 18,
                              compactGallery ? 14 : 20,
                              compactGallery ? 14 : 16);
    outer->setSpacing(compactGallery ? 10 : 12);

    auto *galleryTitle = new QLabel(tr("Mascot Gallery"), &dlg);
    galleryTitle->setObjectName(QStringLiteral("settingsTitle"));
    auto *gallerySubtitle = new QLabel(
        entries.isEmpty()
            ? tr("Every mascot in this vault will appear here.")
            : tr("%n mascot(s) in this vault", nullptr, entries.size()),
        &dlg);
    gallerySubtitle->setObjectName(QStringLiteral("settingsSubtitle"));
    outer->addWidget(galleryTitle);
    outer->addWidget(gallerySubtitle);

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
        gl->setSpacing(10);
        const int cols = compactGallery ? 2 : 3;
        const QSize iconSize =
            compactGallery ? QSize(132, 148) : QSize(176, 196);
        for (int i = 0; i < entries.size(); ++i) {
            const Entry &e = entries.at(i);
            auto *cell = new QToolButton(grid);
            cell->setObjectName(QStringLiteral("mascotGalleryItem"));
            cell->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            cell->setAutoRaise(true);
            cell->setIconSize(iconSize);
            cell->setIcon(QIcon(Mascot::renderPixmap(e.seed, e.kind, iconSize)));
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
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    prepareDialogButtons(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outer->addWidget(buttons);
    dlg.exec();
}

void MainWindow::buildActions() {
    m_gearMenu = new QMenu(this);
    m_gearMenu->setObjectName(QStringLiteral("gearMenu"));

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
    settings->setObjectName(QStringLiteral("settingsAction"));
    auto *manual = make(tr("Manual"), {}, &MainWindow::openManual);
    auto *update = make(tr("Check for Updates…"), {}, &MainWindow::checkForUpdates);
    auto *toggleSide = make(tr("Toggle Sidebar"),
                            QKeySequence(Qt::CTRL | Qt::Key_Backslash),
                            &MainWindow::toggleSidebar);
    auto *newVault = make(tr("New Vault…"), {}, &MainWindow::newVault);
    auto *openVault = make(tr("Open Vault…"), QKeySequence(QKeySequence::Open),
                           &MainWindow::chooseVault);
    auto *switchVault = make(tr("Switch Vault…"),
                             QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
                             &MainWindow::openVaultSwitcher);
    m_newNoteAction = make(tr("New Note"), QKeySequence(QKeySequence::New),
                           &MainWindow::newNote);
    auto *goTo = make(tr("Go to Note…"), QKeySequence(Qt::CTRL | Qt::Key_P),
                      &MainWindow::openQuickOpen);
    m_insertTemplateAction = make(tr("Insert Template…"),
                                  QKeySequence(Qt::CTRL | Qt::Key_T),
                                  &MainWindow::insertTemplate);
    m_insertImageAction = make(tr("Insert Image…"),
                               QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I),
                               &MainWindow::insertImage);
    // Rename focuses the title field for editing (F2 is the universal rename).
    m_renameAction = new QAction(tr("Rename Note"), this);
    m_renameAction->setShortcut(QKeySequence(Qt::Key_F2));
    connect(m_renameAction, &QAction::triggered, this, [this] {
        if (!m_vault || m_readMode)
            return;
        if (m_currentPath.isEmpty() && m_pendingNoteDir.isEmpty()) {
            this->newNote();
            return;
        }
        if (m_mobileLayout)
            showMobileEditor();
        m_titleEdit->setFocus();
        m_titleEdit->selectAll();
    });
    addAction(m_renameAction);
    m_saveAction = make(tr("Save"), QKeySequence(QKeySequence::Save),
                        &MainWindow::saveCurrent);
    // Delete Note confirms first. Ctrl+Shift+Backspace keeps it clear of
    // Ctrl+Delete (the editor's delete-word-forward) while staying deliberate.
    m_deleteAction = make(tr("Delete Note"),
                          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backspace),
                          &MainWindow::deleteCurrentNote);
    // Context menus hide shortcut labels by default; show this one (the editor's
    // right-click menu offers Delete Note).
    m_deleteAction->setShortcutVisibleInContextMenu(true);
    m_findAction = make(tr("Find in Note…"), QKeySequence(QKeySequence::Find),
                        &MainWindow::openFindInFile);
    auto *search = make(tr("Search Vault…"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F),
                        &MainWindow::openSearch);
    auto *brokenLinks = make(tr("Broken Links…"),
                             QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B),
                             &MainWindow::openBrokenLinks);
    m_graphAction = make(tr("Graph View"),
                         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G),
                         &MainWindow::openGraphView);
    m_graphAction->setObjectName(QStringLiteral("graphViewAction"));
    m_graphAction->setToolTip(tr("Graph View  (Ctrl+Shift+G)"));
    m_graphAction->setEnabled(false);
    m_localGraphAction =
        make(tr("Local Graph"), {}, &MainWindow::openLocalGraphView);
    m_localGraphAction->setObjectName(QStringLiteral("localGraphAction"));
    m_localGraphAction->setEnabled(false);
    m_readModeAction = new QAction(tr("Read Mode"), this);
    m_readModeAction->setCheckable(true);
    m_readModeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_readModeAction->setToolTip(tr("Read Mode  (Ctrl+E)"));
    m_readModeAction->setEnabled(false);
    connect(m_readModeAction, &QAction::triggered, this,
            [this](bool enabled) { setReadMode(enabled); });
    addAction(m_readModeAction);
    m_genMascotAction = make(tr("Generate Mascot"),
                             QKeySequence(Qt::CTRL | Qt::Key_M),
                             &MainWindow::generateMascot);
    m_delMascotAction = make(tr("Delete Mascot"),
                             QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M),
                             &MainWindow::deleteMascot);
    auto *gallery = make(tr("Mascot Gallery…"),
                         QKeySequence(Qt::CTRL | Qt::Key_G),
                         &MainWindow::openMascotGallery);
    // Image mode: draw each note's mascot as one of the user's own images
    // (dropped in the mascots/images folder, picked by the note's seed) instead
    // of the procedural creature. Off by default; the seed line is untouched, so
    // it only changes how an existing mascot is drawn.
    auto *imageMode = new QAction(tr("Use Image Mascots"), this);
    imageMode->setCheckable(true);
    imageMode->setChecked(
        QSettings().value(QStringLiteral("mascotImageMode"), false).toBool());
    connect(imageMode, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("mascotImageMode"), on);
        MascotCatalog::shared().refresh(); // re-scan for images added meanwhile
        if (m_mascot)
            m_mascot->update(); // repaint the corner in the chosen style
    });
    addAction(imageMode);
    auto *quit = new QAction(tr("Quit"), this);
    quit->setShortcut(QKeySequence(QKeySequence::Quit));
    connect(quit, &QAction::triggered, this, &QWidget::close);
    addAction(quit);

    auto *mascotMenu = new QMenu(tr("Mascot"), m_gearMenu);
    mascotMenu->setObjectName(QStringLiteral("mascotMenu"));
    mascotMenu->addAction(m_genMascotAction);
    mascotMenu->addAction(m_delMascotAction);
    mascotMenu->addAction(gallery);
    mascotMenu->addAction(imageMode);

    // Requested order: app → file ops → search → mascot → quit, grouped by
    // separators.
    m_gearMenu->addAction(settings);
    m_gearMenu->addAction(manual);
    m_gearMenu->addAction(update);
    m_gearMenu->addAction(toggleSide);
    m_gearMenu->addAction(m_readModeAction);
    m_gearMenu->addSeparator();
    m_gearMenu->addAction(newVault);
    m_gearMenu->addAction(openVault);
    m_gearMenu->addAction(switchVault);
    m_gearMenu->addAction(m_newNoteAction);
    m_gearMenu->addAction(goTo);
    m_gearMenu->addAction(m_insertTemplateAction);
    m_gearMenu->addAction(m_insertImageAction);
    m_gearMenu->addAction(m_renameAction);
    m_gearMenu->addAction(m_saveAction);
    m_gearMenu->addAction(m_deleteAction);
    m_gearMenu->addSeparator();
    m_gearMenu->addAction(m_findAction);
    m_gearMenu->addAction(search);
    m_gearMenu->addAction(brokenLinks);
    m_gearMenu->addSeparator();
    m_gearMenu->addMenu(mascotMenu);
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
    m_backAction->setObjectName(QStringLiteral("backAction"));
    m_backAction->setIcon(makeNavArrow(true));
    m_forwardAction = new QAction(this);
    m_forwardAction->setObjectName(QStringLiteral("forwardAction"));
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
    QString savedTheme =
        s.value(QStringLiteral("theme"), QStringLiteral("dark")).toString();
    if (!AppTheme::isAvailable(savedTheme)) {
        savedTheme = QStringLiteral("dark");
        s.setValue(QStringLiteral("theme"), savedTheme);
    }
    if (AppTheme::currentKey() != savedTheme) {
        AppTheme::apply(*qApp, savedTheme);
        refreshThemeUi();
    }
    m_editorColumnWidth =
        s.value(QStringLiteral("editorWidth"), m_editorColumnWidth).toInt();
    m_editorFullWidth =
        s.value(QStringLiteral("editorFullWidth"), false).toBool();
    applyEditorColumnWidth();
    m_editor->setLineSpacing(s.value(QStringLiteral("lineSpacing"), 100).toInt());
    const QByteArray split = s.value(QStringLiteral("splitterState")).toByteArray();
    if (!split.isEmpty())
        m_splitter->restoreState(split);
    updateResponsiveLayout();

    const bool ignoreNumbers =
        s.value(QStringLiteral("spellIgnoreNumbers"), true).toBool();
    const bool ignoreAllCaps =
        s.value(QStringLiteral("spellIgnoreAllCaps"), true).toBool();
    m_editor->setSpellCheckingOptions(ignoreNumbers, ignoreAllCaps);
    QString language =
        s.value(QStringLiteral("spellLanguage"), QStringLiteral("en_US"))
            .toString();
    QString spellError;
    if (!m_editor->setSpellCheckingLanguage(language, &spellError) &&
        language != QLatin1String("en_US")) {
        language = QStringLiteral("en_US");
        s.setValue(QStringLiteral("spellLanguage"), language);
        m_editor->setSpellCheckingLanguage(language, &spellError);
    }
    const bool spellEnabled =
        s.value(QStringLiteral("spellCheckEnabled"), true).toBool() &&
        !m_editor->spellCheckingLanguage().isEmpty();
    m_editor->setSpellCheckingEnabled(spellEnabled);
    if (!spellError.isEmpty() && m_editor->spellCheckingLanguage().isEmpty())
        QTimer::singleShot(0, this, [this, spellError] {
            notify(tr("Spell checker unavailable: %1").arg(spellError), 4000);
        });

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

void MainWindow::refreshThemeUi() {
    if (m_editor)
        m_editor->applyTheme();
    if (m_backAction)
        m_backAction->setIcon(makeNavArrow(true));
    if (m_forwardAction)
        m_forwardAction->setIcon(makeNavArrow(false));
    if (m_noteTree)
        m_noteTree->viewport()->update();
    if (m_graphPage)
        m_graphPage->update();
    update();
}

void MainWindow::setReadMode(bool enabled, bool persist) {
    if (!m_vault)
        enabled = false;
    const bool changed = m_readMode != enabled;

    // Flush a writable buffer before locking it. Once m_readMode flips, every
    // path that can mutate a vault is intentionally blocked.
    if (changed && enabled) {
        // Ctrl+E can be pressed while the title field still owns focus, before
        // editingFinished has committed a pending filename change.
        if (m_titleEdit && !m_currentPath.isEmpty() &&
            m_titleEdit->text().trimmed() != m_currentTitle)
            renameCurrent(m_titleEdit->text());
        saveCurrent();
    }
    m_readMode = enabled;

    if (persist && m_vault)
        VaultSettings::setValue(m_vault->root(), QStringLiteral("readMode"),
                                enabled ? QStringLiteral("true")
                                        : QStringLiteral("false"));
    updateReadModeUi();

    if (persist && changed)
        notify(enabled ? tr("Read Mode on") : tr("Read Mode off"), 1800);
}

bool MainWindow::ensureVaultWritable() {
    if (!m_readMode)
        return true;
    notify(tr("Read Mode is on"));
    return false;
}

void MainWindow::updateReadModeUi() {
    const bool hasVault = m_vault != nullptr;
    const bool notePage = m_activePage == PageLocation::Kind::Note;
    const bool writable = hasVault && !m_readMode && notePage;

    if (m_editor)
        m_editor->setReadMode(m_readMode);
    if (m_titleEdit)
        m_titleEdit->setReadOnly(m_readMode);
    if (m_readModeAction) {
        m_readModeAction->blockSignals(true);
        m_readModeAction->setChecked(m_readMode);
        m_readModeAction->setEnabled(hasVault);
        m_readModeAction->blockSignals(false);
    }
    if (m_graphAction)
        m_graphAction->setEnabled(hasVault);
    if (m_localGraphAction)
        m_localGraphAction->setEnabled(hasVault && !m_currentPath.isEmpty());
    if (m_findAction)
        m_findAction->setEnabled(hasVault && notePage);
    if (m_newNoteAction)
        m_newNoteAction->setEnabled(writable);
    if (m_renameAction)
        m_renameAction->setEnabled(writable);
    if (m_saveAction)
        m_saveAction->setEnabled(writable);
    if (m_insertTemplateAction)
        m_insertTemplateAction->setEnabled(writable);
    if (m_insertImageAction)
        m_insertImageAction->setEnabled(writable);
    if (m_deleteAction)
        m_deleteAction->setEnabled(writable);
    if (m_noteTree) {
        m_noteTree->setDragEnabled(writable);
        m_noteTree->setAcceptDrops(writable);
        m_noteTree->setDropIndicatorShown(writable);
    }
    updateMascotActions();
}

void MainWindow::applyEditorColumnWidth() {
    if (!m_centerColumn)
        return;
    m_centerColumn->setMaximumWidth(
        m_mobileLayout || m_editorFullWidth ? QWIDGETSIZE_MAX
                                            : m_editorColumnWidth);
}

void MainWindow::applyMobileSplit() {
    if (!m_mobileLayout || !m_splitter)
        return;
    const QList<int> sizes = m_splitter->sizes();
    int total = sizes.value(0) + sizes.value(1);
    if (total <= 0)
        total = qMax(1, width());
    if (m_mobileShowingNotes)
        m_splitter->setSizes({total, 0});
    else
        m_splitter->setSizes({0, total});
}

void MainWindow::updateMobileNavigationControls() {
    if (m_mobileEditorBar)
        m_mobileEditorBar->setVisible(m_mobileLayout);
    if (m_mobileEditorButton) {
        m_mobileEditorButton->setVisible(m_mobileLayout);
        m_mobileEditorButton->setEnabled(
            m_activePage != PageLocation::Kind::Note ||
            !m_currentPath.isEmpty() || !m_pendingNoteDir.isEmpty());
    }
}

void MainWindow::updateSplitterHandleWidth() {
    if (!m_splitter)
        return;
    const bool graphActive = m_activePage != PageLocation::Kind::Note;
    if (m_splitter->property("graphActive").toBool() != graphActive) {
        m_splitter->setProperty("graphActive", graphActive);
        // Dynamic properties do not automatically repolish existing widgets.
        // Refresh only this splitter so the gap-free graph handle and the
        // wider note-page drag target switch immediately during navigation.
        m_splitter->style()->unpolish(m_splitter);
        m_splitter->style()->polish(m_splitter);
    }
    if (m_mobileLayout)
        m_splitter->setHandleWidth(0);
    else if (m_activePage == PageLocation::Kind::Note)
        m_splitter->setHandleWidth(kDesktopSplitterHandleWidth);
    else
        // Graph View paints edge-to-edge. Its handle is exactly as wide as the
        // divider, so no transparent gutter remains beside its canvas even
        // while the sidebar is fully collapsed.
        m_splitter->setHandleWidth(kGraphSplitterHandleWidth);
}

void MainWindow::showMobileNotes() {
    if (!m_mobileLayout)
        return;
    saveCurrent();
    m_mobileShowingNotes = true;
    updateMobileNavigationControls();
    applyMobileSplit();
    if (m_noteTree)
        m_noteTree->setFocus();
}

void MainWindow::showMobileEditor() {
    if (!m_mobileLayout)
        return;
    if (m_activePage == PageLocation::Kind::Note && m_currentPath.isEmpty() &&
        m_pendingNoteDir.isEmpty()) {
        showMobileNotes();
        return;
    }
    m_mobileShowingNotes = false;
    updateMobileNavigationControls();
    applyMobileSplit();
    if (m_activePage != PageLocation::Kind::Note && m_graphPage)
        m_graphPage->focusGraph();
    else if (m_currentPath.isEmpty())
        m_titleEdit->setFocus();
    else
        m_editor->setFocus();
}

void MainWindow::updateResponsiveLayout() {
    if (!m_splitter || !m_centerColumn || !m_mobileEditorBar)
        return;

    const bool mobile = width() > 0 && width() <= kMobileBreakpoint;
    if (mobile == m_mobileLayout) {
        updateSplitterHandleWidth();
        updateMobileNavigationControls();
        applyEditorColumnWidth();
        if (mobile)
            applyMobileSplit();
        return;
    }

    if (mobile) {
        m_desktopSplitterSizes = m_splitter->sizes();
        m_mobileLayout = true;
        updateSplitterHandleWidth();
        m_splitter->setCollapsible(1, true);
        m_mobileShowingNotes =
            m_currentPath.isEmpty() && m_pendingNoteDir.isEmpty();
        updateMobileNavigationControls();
        applyEditorColumnWidth();
        applyMobileSplit();
        return;
    }

    m_mobileLayout = false;
    updateMobileNavigationControls();
    updateSplitterHandleWidth();
    m_splitter->setCollapsible(1, false);
    applyEditorColumnWidth();
    if (!m_desktopSplitterSizes.isEmpty())
        m_splitter->setSizes(m_desktopSplitterSizes);
    else
        m_splitter->setSizes({kDefaultSidebarWidth,
                              qMax(1, width() - kDefaultSidebarWidth)});
}

void MainWindow::openSettings() {
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("settingsDialog"));
    dlg.setProperty("emeraldDialog", true);
    dlg.setWindowTitle(tr("Settings"));
    dlg.setFocusPolicy(Qt::StrongFocus);
    const bool compactSettings = m_mobileLayout || width() <= kMobileBreakpoint;
    dlg.setMinimumSize(compactSettings ? QSize(320, 420) : QSize(620, 560));
    dlg.resize(compactSettings
                   ? QSize(qMax(320, width() - 24), qMax(420, height() - 48))
                   : QSize(680, qMax(560, qMin(700, height() - 40))));
    dlg.setSizeGripEnabled(!compactSettings);

    auto *root = new QVBoxLayout(&dlg);
    root->setContentsMargins(compactSettings ? 12 : 20,
                             compactSettings ? 12 : 18,
                             compactSettings ? 12 : 20,
                             compactSettings ? 12 : 16);
    root->setSpacing(compactSettings ? 10 : 12);

    auto *scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(compactSettings ? 10 : 12);

    auto *heading = new QLabel(tr("Settings"), content);
    heading->setObjectName(QStringLiteral("settingsTitle"));
    auto *subtitle = new QLabel(
        tr("Tune appearance, editor, spelling, vault defaults, and mascot "
           "behavior."),
        content);
    subtitle->setObjectName(QStringLiteral("settingsSubtitle"));
    subtitle->setWordWrap(true);
    contentLayout->addWidget(heading);
    contentLayout->addWidget(subtitle);

    auto *sections = new QVBoxLayout();
    sections->setContentsMargins(0, 0, 0, 0);
    sections->setSpacing(compactSettings ? 10 : 12);
    contentLayout->addLayout(sections);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto addSection = [content, compactSettings, sections](
                          const QString &title, const QString &description) {
        auto *section = new QFrame(content);
        section->setObjectName(QStringLiteral("settingsSection"));
        auto *layout = new QVBoxLayout(section);
        layout->setContentsMargins(compactSettings ? 12 : 15,
                                   compactSettings ? 12 : 13,
                                   compactSettings ? 12 : 15,
                                   compactSettings ? 12 : 14);
        layout->setSpacing(compactSettings ? 8 : 10);

        auto *titleLabel = new QLabel(title, section);
        titleLabel->setObjectName(QStringLiteral("settingsSectionTitle"));
        auto *descriptionLabel = new QLabel(description, section);
        descriptionLabel->setObjectName(
            QStringLiteral("settingsSectionDescription"));
        descriptionLabel->setWordWrap(true);
        layout->addWidget(titleLabel);
        layout->addWidget(descriptionLabel);

        auto *form = new QFormLayout();
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setRowWrapPolicy(compactSettings ? QFormLayout::WrapAllRows
                                               : QFormLayout::DontWrapRows);
        form->setFormAlignment(Qt::AlignTop);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setHorizontalSpacing(compactSettings ? 8 : 22);
        form->setVerticalSpacing(compactSettings ? 10 : 12);
        layout->addLayout(form);

        sections->addWidget(section);
        return form;
    };
    auto addSettingRow = [&dlg, compactSettings](QFormLayout *form,
                                                 const QString &labelText,
                                                 QWidget *field) {
        auto *label = new QLabel(labelText, &dlg);
        label->setObjectName(QStringLiteral("settingsFieldLabel"));
        label->setMinimumWidth(compactSettings ? 0 : 130);
        label->setSizePolicy(compactSettings ? QSizePolicy::Preferred
                                             : QSizePolicy::Fixed,
                             QSizePolicy::Preferred);
        form->addRow(label, field);
    };

    QSettings s;

    auto *themeWidget = new QWidget(&dlg);
    auto *themeLayout = new QVBoxLayout(themeWidget);
    themeLayout->setContentsMargins(0, 0, 0, 0);
    themeLayout->setSpacing(8);
    auto *themeBox = new QComboBox(themeWidget);
    themeBox->setObjectName(QStringLiteral("appTheme"));
    auto *themeActions = new QWidget(themeWidget);
    auto *themeActionsLayout = new QHBoxLayout(themeActions);
    themeActionsLayout->setContentsMargins(0, 0, 0, 0);
    themeActionsLayout->setSpacing(8);
    auto *createThemeButton =
        new QPushButton(tr("Create your theme…"), themeActions);
    createThemeButton->setObjectName(QStringLiteral("createCustomTheme"));
    auto *deleteThemeButton = new QPushButton(tr("Delete"), themeActions);
    deleteThemeButton->setObjectName(QStringLiteral("deleteCustomTheme"));
    deleteThemeButton->setProperty("dialogRole", QStringLiteral("destructive"));
    themeActionsLayout->addWidget(createThemeButton);
    themeActionsLayout->addWidget(deleteThemeButton);
    themeActionsLayout->addStretch();
    themeLayout->addWidget(themeBox);
    themeLayout->addWidget(themeActions);

    auto populateThemes = [themeBox, deleteThemeButton](
                              const QString &selectedKey) {
        const QSignalBlocker blocker(themeBox);
        themeBox->clear();
        themeBox->addItem(QCoreApplication::translate("MainWindow",
                                                       "Emerald Dark"),
                          QStringLiteral("dark"));
        themeBox->addItem(QCoreApplication::translate("MainWindow",
                                                       "Emerald Light"),
                          QStringLiteral("light"));
        for (const AppTheme::CustomTheme &theme : AppTheme::customThemes())
            themeBox->addItem(theme.name, theme.key);
        int index = themeBox->findData(selectedKey);
        if (index < 0)
            index = 0;
        themeBox->setCurrentIndex(index);
        deleteThemeButton->setEnabled(AppTheme::isCustom(
            themeBox->currentData().toString()));
    };
    populateThemes(AppTheme::currentKey());
    deleteThemeButton->setEnabled(
        AppTheme::isCustom(themeBox->currentData().toString()));

    auto *fontBox = new QFontComboBox(&dlg);
    fontBox->view()->setObjectName(QStringLiteral("fontFamilyPopup"));
    fontBox->setFontFilters(QFontComboBox::AllFonts);
    fontBox->setEditable(false);
    fontBox->setCurrentFont(m_editor->font());
    auto *sizeBox = new QSpinBox(&dlg);
    sizeBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    sizeBox->setRange(8, 32);
    sizeBox->setValue(m_editor->font().pointSize());
    auto *widthBox = new QSpinBox(&dlg);
    widthBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    widthBox->setRange(500, 1600);
    widthBox->setSingleStep(20);
    widthBox->setSuffix(tr(" px"));
    widthBox->setValue(m_editorColumnWidth);
    widthBox->setObjectName(QStringLiteral("editorColumnWidth"));
    auto *fullWidthBox = new QCheckBox(tr("Use all available space"), &dlg);
    fullWidthBox->setObjectName(QStringLiteral("editorFullWidth"));
    fullWidthBox->setChecked(m_editorFullWidth);
    widthBox->setEnabled(!m_editorFullWidth);
    auto *spacingBox = new QSpinBox(&dlg);
    spacingBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spacingBox->setRange(100, 250);
    spacingBox->setSingleStep(10);
    spacingBox->setSuffix(tr(" %"));
    spacingBox->setValue(s.value(QStringLiteral("lineSpacing"), 100).toInt());

    // Spelling is checked incrementally in the Markdown highlighter. English
    // ships with Emerald; Manage languages installs/removes verified optional
    // packs outside the vault.
    auto *spellEnabledBox = new QCheckBox(tr("Underline misspelled words"), &dlg);
    spellEnabledBox->setObjectName(QStringLiteral("spellCheckEnabled"));
    spellEnabledBox->setChecked(
        s.value(QStringLiteral("spellCheckEnabled"), true).toBool());
    auto *spellLanguageWidget = new QWidget(&dlg);
    auto *spellLanguageLayout = new QHBoxLayout(spellLanguageWidget);
    spellLanguageLayout->setContentsMargins(0, 0, 0, 0);
    spellLanguageLayout->setSpacing(8);
    auto *spellLanguageBox = new QComboBox(spellLanguageWidget);
    spellLanguageBox->setObjectName(QStringLiteral("spellLanguage"));
    auto *manageSpellLanguages =
        new QPushButton(tr("Manage…"), spellLanguageWidget);
    manageSpellLanguages->setObjectName(QStringLiteral("manageSpellLanguages"));
    spellLanguageLayout->addWidget(spellLanguageBox, 1);
    spellLanguageLayout->addWidget(manageSpellLanguages);
    auto *ignoreNumbersBox =
        new QCheckBox(tr("Ignore words containing numbers"), &dlg);
    ignoreNumbersBox->setObjectName(QStringLiteral("spellIgnoreNumbers"));
    ignoreNumbersBox->setChecked(
        s.value(QStringLiteral("spellIgnoreNumbers"), true).toBool());
    auto *ignoreCapsBox = new QCheckBox(tr("Ignore ALL-CAPS words"), &dlg);
    ignoreCapsBox->setObjectName(QStringLiteral("spellIgnoreAllCaps"));
    ignoreCapsBox->setChecked(
        s.value(QStringLiteral("spellIgnoreAllCaps"), true).toBool());

    auto refreshSpellLanguages = [spellLanguageBox] {
        QString selected = spellLanguageBox->currentData().toString();
        if (selected.isEmpty())
            selected = QStringLiteral("en_US");
        spellLanguageBox->clear();
        const QStringList installed = SpellChecker::installedLanguages();
        for (const SpellLanguage &language :
             SpellChecker::availableLanguages())
            if (installed.contains(language.locale))
                spellLanguageBox->addItem(language.name, language.locale);
        int index = spellLanguageBox->findData(selected);
        if (index < 0)
            index = spellLanguageBox->findData(QStringLiteral("en_US"));
        spellLanguageBox->setCurrentIndex(qMax(0, index));
    };
    refreshSpellLanguages();
    const QString savedSpellLanguage =
        s.value(QStringLiteral("spellLanguage"), QStringLiteral("en_US"))
            .toString();
    if (const int index = spellLanguageBox->findData(savedSpellLanguage);
        index >= 0)
        spellLanguageBox->setCurrentIndex(index);
    auto updateSpellControlState = [spellEnabledBox, spellLanguageBox,
                                    ignoreNumbersBox, ignoreCapsBox] {
        const bool enabled = spellEnabledBox->isChecked();
        spellLanguageBox->setEnabled(enabled);
        ignoreNumbersBox->setEnabled(enabled);
        ignoreCapsBox->setEnabled(enabled);
    };
    connect(spellEnabledBox, &QCheckBox::toggled, &dlg,
            updateSpellControlState);
    connect(manageSpellLanguages, &QPushButton::clicked, &dlg,
            [this, &dlg, refreshSpellLanguages] {
                SpellLanguageDialog manager(m_editor->spellCheckingLanguage(),
                                            &dlg);
                connect(&manager, &SpellLanguageDialog::languagesChanged, &dlg,
                        refreshSpellLanguages);
                manager.exec();
                refreshSpellLanguages();
            });
    updateSpellControlState();

    // Mascots: auto-generate once a note crosses a character count.
    auto *mascotAutoBox = new QCheckBox(tr("Generate one automatically"), &dlg);
    mascotAutoBox->setChecked(s.value(QStringLiteral("mascotAuto"), false).toBool());
    auto *mascotThreshBox = new QSpinBox(&dlg);
    mascotThreshBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
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
    auto *fileTreeSortBox = new QComboBox(&dlg);
    fileTreeSortBox->setObjectName(QStringLiteral("fileTreeSort"));
    fileTreeSortBox->addItem(tr("Name (A–Z)"), QStringLiteral("nameAsc"));
    fileTreeSortBox->addItem(tr("Name (Z–A)"), QStringLiteral("nameDesc"));
    fileTreeSortBox->addItem(tr("Modified (newest first)"),
                             QStringLiteral("modifiedNewest"));
    fileTreeSortBox->addItem(tr("Modified (oldest first)"),
                             QStringLiteral("modifiedOldest"));
    fileTreeSortBox->setCurrentIndex(
        fileTreeSortBox->findData(currentNoteTreeSortKey()));
    auto *readModeBox = new QCheckBox(tr("Prevent changes to this vault"), &dlg);
    readModeBox->setChecked(m_readMode);
    readModeBox->setToolTip(tr("Shortcut: Ctrl+E"));
    auto *brokenLinksButton = new QPushButton(tr("Review…"), &dlg);
    brokenLinksButton->setToolTip(tr("Shortcut: Ctrl+Shift+B"));
    auto *graphButtons = new QWidget(&dlg);
    auto *graphButtonsLayout = new QHBoxLayout(graphButtons);
    graphButtonsLayout->setContentsMargins(0, 0, 0, 0);
    graphButtonsLayout->setSpacing(8);
    auto *openGraphButton = new QPushButton(tr("Open global"), graphButtons);
    openGraphButton->setObjectName(QStringLiteral("settingsOpenGraph"));
    openGraphButton->setToolTip(tr("Shortcut: Ctrl+Shift+G"));
    auto *openLocalGraphButton =
        new QPushButton(tr("Open local"), graphButtons);
    openLocalGraphButton->setObjectName(
        QStringLiteral("settingsOpenLocalGraph"));
    graphButtonsLayout->addWidget(openGraphButton);
    graphButtonsLayout->addWidget(openLocalGraphButton);
    graphButtonsLayout->addStretch();
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
        const int fi = folderBox->findData(VaultSettings::value(
            m_vault->root(), QStringLiteral("newNoteFolder")));
        if (fi >= 0)
            folderBox->setCurrentIndex(fi);
        const int hi = homeBox->findData(VaultSettings::value(
            m_vault->root(), QStringLiteral("homeNote")));
        if (hi >= 0)
            homeBox->setCurrentIndex(hi);
        const int ti = templatesBox->findData(VaultSettings::value(
            m_vault->root(), QStringLiteral("templatesFolder")));
        if (ti >= 0)
            templatesBox->setCurrentIndex(ti);
    } else {
        folderBox->setEnabled(false);
        homeBox->setEnabled(false);
        templatesBox->setEnabled(false);
        readModeBox->setEnabled(false);
        brokenLinksButton->setEnabled(false);
    }
    openGraphButton->setEnabled(m_vault != nullptr);
    openLocalGraphButton->setEnabled(m_vault && !m_currentPath.isEmpty());

    auto *appearanceForm = addSection(
        tr("Appearance"), tr("Choose the palette used throughout Emerald."));
    addSettingRow(appearanceForm, tr("Theme"), themeWidget);

    auto *editorForm =
        addSection(tr("Editor"), tr("Reading comfort and writing column size."));
    addSettingRow(editorForm, tr("Font"), fontBox);
    addSettingRow(editorForm, tr("Font size"), sizeBox);
    addSettingRow(editorForm, tr("Full width"), fullWidthBox);
    addSettingRow(editorForm, tr("Column width"), widthBox);
    addSettingRow(editorForm, tr("Line spacing"), spacingBox);

    auto *spellingForm = addSection(
        tr("Spelling"),
        tr("Fast local checking with English included and optional languages."));
    addSettingRow(spellingForm, tr("Spell check"), spellEnabledBox);
    addSettingRow(spellingForm, tr("Language"), spellLanguageWidget);
    addSettingRow(spellingForm, tr("Numbers"), ignoreNumbersBox);
    addSettingRow(spellingForm, tr("Capitals"), ignoreCapsBox);

    auto *vaultForm = addSection(
        tr("Vault"), tr("Defaults and maintenance tools for this vault."));
    addSettingRow(vaultForm, tr("File order"), fileTreeSortBox);
    addSettingRow(vaultForm, tr("New notes in"), folderBox);
    addSettingRow(vaultForm, tr("Home note"), homeBox);
    addSettingRow(vaultForm, tr("Templates folder"), templatesBox);
    addSettingRow(vaultForm, tr("Read mode"), readModeBox);
    addSettingRow(vaultForm, tr("Broken links"), brokenLinksButton);
    addSettingRow(vaultForm, tr("Graph view"), graphButtons);

    auto *mascotForm = addSection(
        tr("Mascot"), tr("Optional automatic mascot generation for notes."));
    addSettingRow(mascotForm, tr("Generate"), mascotAutoBox);
    addSettingRow(mascotForm, tr("After"), mascotThreshBox);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->setObjectName(QStringLiteral("settingsButtons"));
    if (auto *ok = buttons->button(QDialogButtonBox::Ok))
    {
        ok->setIcon(QIcon());
        ok->setAutoDefault(false);
        ok->setDefault(false);
        ok->setProperty("dialogRole", QStringLiteral("primary"));
    }
    if (auto *cancel = buttons->button(QDialogButtonBox::Cancel))
    {
        cancel->setIcon(QIcon());
        cancel->setAutoDefault(false);
        cancel->setDefault(false);
        cancel->setProperty("dialogRole", QStringLiteral("secondary"));
    }
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto applyEditorPreview = [this, widthBox](const QFont &font,
                                               bool fullWidth,
                                               int columnWidth,
                                               int lineSpacing) {
        m_editor->applyFont(font);
        m_editorFullWidth = fullWidth;
        widthBox->setEnabled(!m_editorFullWidth);
        m_editorColumnWidth = columnWidth;
        applyEditorColumnWidth();
        m_editor->setLineSpacing(lineSpacing);
    };

    // Live preview of font + width + line spacing as the user changes controls.
    auto previewEditor = [fontBox, sizeBox, fullWidthBox, widthBox, spacingBox,
                          applyEditorPreview] {
        QFont font = fontBox->currentFont();
        font.setPointSize(sizeBox->value());
        applyEditorPreview(font, fullWidthBox->isChecked(), widthBox->value(),
                           spacingBox->value());
    };
    connect(fontBox, &QFontComboBox::currentFontChanged, &dlg, previewEditor);
    connect(sizeBox, qOverload<int>(&QSpinBox::valueChanged), &dlg,
            previewEditor);
    connect(fullWidthBox, &QCheckBox::toggled, &dlg, previewEditor);
    connect(widthBox, qOverload<int>(&QSpinBox::valueChanged), &dlg,
            previewEditor);
    connect(spacingBox, qOverload<int>(&QSpinBox::valueChanged), &dlg,
            previewEditor);

    auto previewTheme = [this, themeBox, applyEditorPreview] {
        const QString selected = themeBox->currentData().toString();
        if (selected == AppTheme::currentKey())
            return;

        // QApplication::setStyleSheet() repolishes every widget and can reset
        // an explicitly assigned editor font. Capture the complete preview
        // state before applying the theme, then restore it as one unit. This
        // also preserves unsaved font/layout choices made earlier in this
        // Settings session, rather than falling back to persisted defaults.
        const QFont previewFont = m_editor->font();
        const bool previewFullWidth = m_editorFullWidth;
        const int previewColumnWidth = m_editorColumnWidth;
        const int previewLineSpacing = m_editor->lineSpacing();
        AppTheme::apply(*qApp, selected);
        refreshThemeUi();
        applyEditorPreview(previewFont, previewFullWidth, previewColumnWidth,
                           previewLineSpacing);
    };
    connect(themeBox, qOverload<int>(&QComboBox::currentIndexChanged), &dlg,
            previewTheme);
    connect(themeBox, qOverload<int>(&QComboBox::currentIndexChanged), &dlg,
            [themeBox, deleteThemeButton] {
                deleteThemeButton->setEnabled(
                    AppTheme::isCustom(themeBox->currentData().toString()));
            });

    const QFont originalFont = m_editor->font();
    const QString originalTheme = AppTheme::currentKey();
    const int originalWidth = m_editorColumnWidth;
    const bool originalFullWidth = m_editorFullWidth;
    const int originalSpacing = s.value(QStringLiteral("lineSpacing"), 100).toInt();
    auto restoreEditorPreview = [this, fontBox, sizeBox, fullWidthBox, widthBox,
                                 spacingBox, applyEditorPreview] {
        const QFont current = [&] {
            QFont font = fontBox->currentFont();
            font.setPointSize(sizeBox->value());
            return font;
        }();
        refreshThemeUi();
        applyEditorPreview(current, fullWidthBox->isChecked(), widthBox->value(),
                           spacingBox->value());
    };
    connect(createThemeButton, &QPushButton::clicked, &dlg,
            [this, &dlg, themeBox, populateThemes, restoreEditorPreview] {
                const QString basedOn = themeBox->currentData().toString();
                ThemeEditorDialog editor(basedOn, &dlg);
                if (editor.exec() == QDialog::Accepted) {
                    const AppTheme::CustomTheme theme = editor.theme();
                    AppTheme::saveCustomTheme(theme);
                    populateThemes(theme.key);
                    AppTheme::apply(*qApp, theme.key);
                    restoreEditorPreview();
                }
            });
    connect(deleteThemeButton, &QPushButton::clicked, &dlg,
            [this, themeBox, populateThemes, restoreEditorPreview] {
                const QString selected = themeBox->currentData().toString();
                if (!AppTheme::isCustom(selected))
                    return;
                const QString name = AppTheme::displayName(selected);
                const QMessageBox::StandardButton answer = QMessageBox::question(
                    themeBox, tr("Delete custom theme"),
                    tr("Delete the custom theme “%1”? This cannot be undone.")
                        .arg(name),
                    QMessageBox::Yes | QMessageBox::Cancel,
                    QMessageBox::Cancel);
                if (answer != QMessageBox::Yes)
                    return;
                AppTheme::deleteCustomTheme(selected);
                populateThemes(QStringLiteral("dark"));
                AppTheme::apply(*qApp, AppTheme::Id::Dark);
                restoreEditorPreview();
            });
    bool openBrokenLinksAfterSettings = false;
    enum class GraphToOpen { None, Global, Local };
    GraphToOpen graphToOpen = GraphToOpen::None;
    connect(brokenLinksButton, &QPushButton::clicked, &dlg, [&] {
        openBrokenLinksAfterSettings = true;
        dlg.reject();
    });
    connect(openGraphButton, &QPushButton::clicked, &dlg, [&] {
        graphToOpen = GraphToOpen::Global;
        dlg.reject();
    });
    connect(openLocalGraphButton, &QPushButton::clicked, &dlg, [&] {
        graphToOpen = GraphToOpen::Local;
        dlg.reject();
    });
    dlg.setFocus(Qt::OtherFocusReason);
    QTimer::singleShot(0, &dlg,
                       [&dlg] { dlg.setFocus(Qt::OtherFocusReason); });
    if (dlg.exec() == QDialog::Accepted) {
        QFont f = fontBox->currentFont();
        f.setPointSize(sizeBox->value());
        m_editor->applyFont(f);
        m_editorFullWidth = fullWidthBox->isChecked();
        m_editorColumnWidth = widthBox->value();
        applyEditorColumnWidth();
        m_editor->setLineSpacing(spacingBox->value());
        const QString selectedTheme = themeBox->currentData().toString();
        if (selectedTheme != AppTheme::currentKey()) {
            AppTheme::apply(*qApp, selectedTheme);
            refreshThemeUi();
        }
        s.setValue(QStringLiteral("theme"), selectedTheme);
        s.setValue(QStringLiteral("editorFontFamily"), f.family());
        s.setValue(QStringLiteral("editorFontSize"), f.pointSize());
        s.setValue(QStringLiteral("editorWidth"), widthBox->value());
        s.setValue(QStringLiteral("editorFullWidth"), m_editorFullWidth);
        s.setValue(QStringLiteral("lineSpacing"), spacingBox->value());
        const QString selectedFileTreeSort =
            fileTreeSortBox->currentData().toString();
        const bool fileTreeSortChanged =
            selectedFileTreeSort != currentNoteTreeSortKey();
        s.setValue(QStringLiteral("fileTreeSort"), selectedFileTreeSort);
        QString spellLanguage =
            spellLanguageBox->currentData().toString();
        QString spellError;
        bool languageReady =
            m_editor->setSpellCheckingLanguage(spellLanguage, &spellError);
        if (!languageReady && spellLanguage != QLatin1String("en_US")) {
            const QString selectedError = spellError;
            spellLanguage = QStringLiteral("en_US");
            languageReady =
                m_editor->setSpellCheckingLanguage(spellLanguage, &spellError);
            spellError = selectedError;
        }
        m_editor->setSpellCheckingOptions(ignoreNumbersBox->isChecked(),
                                          ignoreCapsBox->isChecked());
        m_editor->setSpellCheckingEnabled(spellEnabledBox->isChecked() &&
                                          languageReady);
        s.setValue(QStringLiteral("spellCheckEnabled"),
                   spellEnabledBox->isChecked() && languageReady);
        s.setValue(QStringLiteral("spellLanguage"), spellLanguage);
        s.setValue(QStringLiteral("spellIgnoreNumbers"),
                   ignoreNumbersBox->isChecked());
        s.setValue(QStringLiteral("spellIgnoreAllCaps"),
                   ignoreCapsBox->isChecked());
        if (!languageReady)
            QTimer::singleShot(0, this, [this, spellError] {
                notify(tr("Could not load spelling language: %1").arg(spellError),
                       4000);
            });
        s.setValue(QStringLiteral("mascotAuto"), mascotAutoBox->isChecked());
        s.setValue(QStringLiteral("mascotThreshold"), mascotThreshBox->value());
        if (m_vault) {
            const QString root = m_vault->root();
            VaultSettings::setValue(root, QStringLiteral("newNoteFolder"),
                                    folderBox->currentData().toString());
            VaultSettings::setValue(root, QStringLiteral("homeNote"),
                                    homeBox->currentData().toString());
            VaultSettings::setValue(root, QStringLiteral("templatesFolder"),
                                    templatesBox->currentData().toString());
            setReadMode(readModeBox->isChecked());
            if (fileTreeSortChanged)
                refreshTree();
        }
    } else {
        const QString restoredTheme = AppTheme::isAvailable(originalTheme)
                                          ? originalTheme
                                          : QStringLiteral("dark");
        if (AppTheme::currentKey() != restoredTheme) {
            AppTheme::apply(*qApp, restoredTheme);
            refreshThemeUi();
        }
        m_editor->applyFont(originalFont); // revert the live preview
        m_editorFullWidth = originalFullWidth;
        m_editorColumnWidth = originalWidth;
        applyEditorColumnWidth();
        m_editor->setLineSpacing(originalSpacing);
    }
    if (openBrokenLinksAfterSettings)
        QTimer::singleShot(0, this, &MainWindow::openBrokenLinks);
    if (graphToOpen == GraphToOpen::Global)
        QTimer::singleShot(0, this, &MainWindow::openGraphView);
    else if (graphToOpen == GraphToOpen::Local)
        QTimer::singleShot(0, this, &MainWindow::openLocalGraphView);
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

void MainWindow::toggleSidebar() {
    if (m_mobileLayout) {
        if (m_mobileShowingNotes)
            showMobileEditor();
        else
            showMobileNotes();
        return;
    }

    const QList<int> sizes = m_splitter->sizes();
    const int total = sizes.value(0) + sizes.value(1);
    if (sizes.value(0) > 0)
        m_splitter->setSizes({0, total}); // collapse
    else {
        const int sidebarWidth = qMin(kDefaultSidebarWidth, qMax(1, total - 1));
        m_splitter->setSizes({sidebarWidth, total - sidebarWidth}); // reopen
    }
}

void MainWindow::newVault() {
    const QString parent = QFileDialog::getExistingDirectory(
        this, tr("Choose where to create the vault"), vaultStartDir(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (parent.isEmpty())
        return;
    QString name;
    if (!runVaultNameDialog(this, &name))
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
    if (!ensureVaultWritable())
        return;
    if (m_currentPath.isEmpty()) {
        notify(tr("No note is open to delete"), 2000);
        return;
    }
    deleteEntries({m_currentPath}); // shows the confirm dialog + reconciles
}

void MainWindow::onEditorContextMenu(const QPoint &pos) {
    QMenu *menu = m_editor->createStandardContextMenu();
    menu->setObjectName(QStringLiteral("editorContextMenu"));
    const QString misspelled = m_editor->misspelledWordAt(pos);
    if (!misspelled.isEmpty()) {
        auto *spelling = new QMenu(tr("Spelling: “%1”").arg(misspelled), menu);
        spelling->setObjectName(QStringLiteral("spellingSuggestions"));
        const QStringList suggestions =
            m_editor->spellingSuggestions(misspelled);
        if (suggestions.isEmpty()) {
            QAction *none = spelling->addAction(tr("No suggestions"));
            none->setEnabled(false);
        } else {
            for (const QString &suggestion : suggestions) {
                QAction *replace = spelling->addAction(suggestion);
                connect(replace, &QAction::triggered, menu,
                        [this, pos, misspelled, suggestion] {
                            m_editor->replaceMisspelledWordAt(
                                pos, misspelled, suggestion);
                        });
            }
        }
        spelling->addSeparator();
        QAction *add = spelling->addAction(
            tr("Add “%1” to personal dictionary").arg(misspelled));
        connect(add, &QAction::triggered, menu, [this, misspelled] {
            QString error;
            if (m_editor->addToPersonalDictionary(misspelled, &error))
                notify(tr("Added “%1” to your dictionary").arg(misspelled));
            else
                notify(error.isEmpty() ? tr("Could not add that word") : error,
                       3000);
        });
        QAction *ignore = spelling->addAction(tr("Ignore for this session"));
        connect(ignore, &QAction::triggered, menu, [this, misspelled] {
            m_editor->ignoreSpellingForSession(misspelled);
        });

        QAction *before = menu->actions().isEmpty() ? nullptr
                                                     : menu->actions().first();
        menu->insertMenu(before, spelling);
        menu->insertSeparator(before);
    }
    // Drop the standard "Delete" entry — it only removes a text selection, so it
    // reads as a broken delete-the-file. Offer the real "Delete Note" instead,
    // which carries its Ctrl+Shift+Backspace shortcut label.
    for (QAction *a : menu->actions())
        if (a->objectName() == QLatin1String("edit-delete"))
            menu->removeAction(a);
    if (!m_currentPath.isEmpty() && (m_insertImageAction || m_deleteAction)) {
        menu->addSeparator();
        if (m_insertImageAction)
            menu->addAction(m_insertImageAction);
        if (m_deleteAction)
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
        if (m_readMode) {
            notify(tr("The manual is not in this vault — Read Mode is on"),
                   3000);
            return;
        }
        const Note note = m_vault->createNote(title);
        m_vault->write(note.path, manualText());
        path = note.path;
        m_vault->scan();
        m_searchIndex.updateNote(note.path, note.title, manualText());
        m_linkGraphIndex.setNotes(m_vault->root(), m_vault->notes());
        m_linkGraphIndex.updateNote(note.path, note.title, manualText());
        markNoteMetaCurrent(note.path, note.title);
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
        this, tr("Open Vault"), vaultStartDir(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
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
    if (m_vault && m_graphPage)
        VaultSettings::setValue(m_vault->root(), QStringLiteral("graphState"),
                                m_graphPage->savedState());
    delete m_vault;
    m_vault = new Vault(path);
    updateVaultTitle();
    // Fold any legacy seed store into note headers. The migration rejects every
    // metadata or note path that does not resolve canonically inside the vault.
    LegacyMascotMigration::run(*m_vault);
    m_vault->scan();
    m_noteMeta = scannedNoteMeta();
    m_linkGraphIndex.clear();
    m_linkGraphIndex.setNotes(m_vault->root(), m_vault->notes());
    if (m_graphPage) {
        m_graphPage->clearGraph();
        m_graphPage->restoreState(VaultSettings::value(
            m_vault->root(), QStringLiteral("graphState")));
    }
    startIndexRebuild();

    m_currentPath.clear();
    m_currentTitle.clear();
    m_pendingNoteDir.clear();
    m_lastSavedFingerprint = 0;
    m_history.clear();
    m_histIndex = -1;
    m_activePage = PageLocation::Kind::Note;
    showNotePage();
    updateNavActions();
    m_editor->clearFolds(); // drop the previous note's folds before clearing
    m_loading = true;
    m_editor->clear();
    m_editor->setImagePaths(QString(), QString());
    m_editor->sourceDocument()->setModified(false);
    m_loading = false;
    m_titleEdit->blockSignals(true);
    m_titleEdit->clear();
    m_titleEdit->blockSignals(false);

    m_readMode = VaultSettings::value(
                     m_vault->root(), QStringLiteral("readMode"),
                     QStringLiteral("false")) == QStringLiteral("true");
    updateReadModeUi();

    refreshTree(false); // a freshly opened vault starts fully collapsed
    QSettings().setValue(QStringLiteral("lastVault"), path);
    setWindowTitle(QStringLiteral("Emerald — %1").arg(QFileInfo(path).fileName()));
    openInitialNote();
    refreshMascot(); // hide a stale mascot if the new vault opened no note
    if (m_mobileLayout) {
        if (m_currentPath.isEmpty())
            showMobileNotes();
        else
            showMobileEditor();
    }
}

void MainWindow::updateVaultTitle() {
    if (!m_sideTitle)
        return;
    QString title;
    if (m_vault)
        title = QFileInfo(m_vault->root()).fileName();
    if (title.isEmpty())
        title = tr("Notes");
    if (auto *label = dynamic_cast<ElidedLabel *>(m_sideTitle))
        label->setFullText(title);
    else
        m_sideTitle->setText(title);
    const QString minText =
        title.size() > 10 ? title.left(10) + QStringLiteral("...")
                          : title;
    m_sideTitle->setMinimumWidth(
        qMax(80, m_sideTitle->fontMetrics().horizontalAdvance(minText)));
    m_sideTitle->setToolTip(m_vault ? QDir::toNativeSeparators(m_vault->root())
                                    : QString());
}

void MainWindow::startIndexRebuild() {
    ++m_indexGeneration;
    const int generation = m_indexGeneration;

    if (m_indexThread) {
        m_indexThread->requestInterruption();
        m_indexThread = nullptr;
    }

    m_searchIndex.clear();
    m_linkGraphIndex.clear();
    const QVector<Note> notes = m_vault ? m_vault->notes() : QVector<Note>();
    const QString root = m_vault ? m_vault->root() : QString();
    m_linkGraphIndex.setNotes(root, notes);
    if (notes.isEmpty()) {
        refreshGraphPage();
        return;
    }

    const QPointer<MainWindow> guard(this);
    QThread *thread = QThread::create([guard, notes, root, generation] {
        auto *builtSearch = new SearchIndex;
        auto *builtGraph = new LinkGraphIndex;
        builtGraph->setNotes(root, notes);
        for (const Note &note : notes) {
            if (QThread::currentThread()->isInterruptionRequested()) {
                delete builtSearch;
                delete builtGraph;
                return;
            }
            if (QFileInfo::exists(note.path)) {
                const QString content = readNoteForIndex(note.path);
                builtSearch->updateNote(note.path, note.title, content);
                builtGraph->updateNote(note.path, note.title, content);
            }
        }
        if (!guard) {
            delete builtSearch;
            delete builtGraph;
            return;
        }
        QMetaObject::invokeMethod(
            guard,
            [guard, builtSearch, builtGraph, generation] {
                if (guard && guard->m_indexGeneration == generation) {
                    guard->m_searchIndex = std::move(*builtSearch);
                    guard->m_linkGraphIndex = std::move(*builtGraph);
                    if (!guard->m_currentPath.isEmpty()) {
                        const QString content = guard->m_editor->toPlainText();
                        guard->m_searchIndex.updateNote(
                            guard->m_currentPath, guard->m_currentTitle,
                            content);
                        guard->m_linkGraphIndex.updateNote(
                            guard->m_currentPath, guard->m_currentTitle,
                            content);
                    }
                    guard->refreshGraphPage();
                }
                delete builtSearch;
                delete builtGraph;
            },
            Qt::QueuedConnection);
    });
    m_indexThread = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_indexThread == thread)
            m_indexThread = nullptr;
        thread->deleteLater();
    });
    thread->start();
}

void MainWindow::openInitialNote() {
    if (!m_vault)
        return;
    QSettings s;
    // A configured Home note wins; otherwise reopen the last-edited note.
    const QString home = VaultSettings::value(
        m_vault->root(), QStringLiteral("homeNote"));
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
    auto *model = static_cast<NoteTreeModel *>(m_noteTreeModel);
    const QSet<QString> expanded =
        preserveExpansion && model ? model->expandedDirs() : QSet<QString>();
    if (!m_vault) {
        if (model)
            model->rebuild(QString(), QStringList(), QVector<Note>(),
                           QSet<QString>(), NoteTreeSort::NameAscending);
        return;
    }

    QStringList titles;
    for (const Note &n : m_vault->notes())
        titles << n.title;

    model->rebuild(m_vault->root(), m_vault->folders(), m_vault->notes(),
                   expanded,
                   noteTreeSortFromKey(currentNoteTreeSortKey()));

    // Re-open the folders that were open before (none for a fresh vault, so it
    // stays fully collapsed). The chevron icons follow via the expanded signal.
    for (const QString &dir : expanded) {
        const QModelIndex idx = model->indexForDir(dir);
        if (idx.isValid())
            m_noteTree->setExpanded(idx, true);
    }
    m_editor->setCompletions(titles);
    selectInTree(m_currentPath);
    watchVaultDirs(); // keep folder watches in sync with the rebuilt tree
}

void MainWindow::rescanVaultIncremental(bool preserveExpansion) {
    if (!m_vault)
        return;

    const QHash<QString, NoteFileMeta> previous = m_noteMeta;
    m_vault->scan();
    updateIndexForScannedVault(previous);
    refreshTree(preserveExpansion);
}

MainWindow::NoteFileMeta MainWindow::noteFileMeta(const Note &note) const {
    const QFileInfo info(note.path);
    NoteFileMeta meta;
    meta.title = note.title;
    if (info.exists()) {
        meta.size = info.size();
        meta.modified = info.lastModified();
    }
    return meta;
}

QHash<QString, MainWindow::NoteFileMeta> MainWindow::scannedNoteMeta() const {
    QHash<QString, NoteFileMeta> meta;
    if (!m_vault)
        return meta;
    const QVector<Note> notes = m_vault->notes();
    meta.reserve(notes.size());
    for (const Note &note : notes)
        meta.insert(note.path, noteFileMeta(note));
    return meta;
}

void MainWindow::updateIndexForScannedVault(
    const QHash<QString, NoteFileMeta> &previous) {
    const QHash<QString, NoteFileMeta> current = scannedNoteMeta();
    m_linkGraphIndex.setNotes(m_vault->root(), m_vault->notes());
    for (const Note &note : m_vault->notes()) {
        const NoteFileMeta meta = current.value(note.path);
        const auto old = previous.constFind(note.path);
        if (old == previous.constEnd() || old.value().title != meta.title ||
            old.value().size != meta.size ||
            old.value().modified != meta.modified) {
            const QString content = readNoteForIndex(note.path);
            m_searchIndex.updateNote(note.path, note.title, content);
            m_linkGraphIndex.updateNote(note.path, note.title, content);
        }
    }
    for (auto it = previous.constBegin(); it != previous.constEnd(); ++it)
        if (!current.contains(it.key())) {
            m_searchIndex.removeNote(it.key());
            m_linkGraphIndex.removeNote(it.key());
        }
    m_noteMeta = current;
    refreshGraphPage();
}

void MainWindow::refreshGraphPage() {
    if (!m_graphPage || m_activePage == PageLocation::Kind::Note)
        return;
    m_graphPage->setSnapshot(m_linkGraphIndex.snapshot());
    m_graphPage->setCurrentPath(m_currentPath);
}

void MainWindow::markNoteMetaCurrent(const QString &path, const QString &title) {
    if (path.isEmpty())
        return;
    Note note{path, title};
    m_noteMeta.insert(path, noteFileMeta(note));
}

void MainWindow::openNoteByPath(const QString &path, bool record,
                                bool saveBeforeOpen) {
    if (!m_vault || path.isEmpty())
        return;
    if (record)
        captureCurrentPageState();
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
        m_cursorPositions[m_currentPath] =
            m_editor->sourceTextCursor().position();
    if (saveBeforeOpen)
        saveCurrent();

    m_editor->clearFolds(); // the previous note's folds would dangle once its
                            // content is replaced below — drop them first
    m_loading = true;
    const QString body = m_vault->read(path);
    m_editor->setImagePaths(QFileInfo(path).absolutePath(), m_vault->root());
    m_editor->setPlainText(body);
    m_editor->sourceDocument()->setModified(false);
    m_loading = false;

    m_currentPath = path;
    m_pendingNoteDir.clear();
    m_lastSavedFingerprint = contentFingerprint(body);
    m_currentTitle = Vault::titleFromPath(path);
    watchCurrent();
    m_titleEdit->blockSignals(true);
    m_titleEdit->setText(m_currentTitle);
    m_titleEdit->blockSignals(false);
    setWindowTitle(QStringLiteral("Emerald — %1").arg(m_currentTitle));
    selectInTree(path);
    QSettings().setValue(QStringLiteral("lastNote"), path); // reopen on launch
    if (record)
        pushHistory({PageLocation::Kind::Note, path});
    showNotePage();
    updateNavActions();

    // Always restore the caret to where it last sat in this note (remembered in
    // m_cursorPositions, persisted across restarts) — whether arriving via the
    // back/forward arrows, a tree click, a link, or launch. A note never visited
    // before falls back to its first line. The minimum position skips a hidden
    // mascot header line so the caret never starts on it. The editor is focused,
    // ready to type, either way.
    QTextCursor c = m_editor->sourceTextCursor();
    const int minPos = m_editor->firstContentPosition();
    const int last = qMax(minPos, m_cursorPositions.value(path, minPos));
    c.setPosition(qBound(
        minPos, last,
        qMax(minPos, m_editor->sourceDocument()->characterCount() - 1)));
    m_editor->setSourceTextCursor(c);
    m_editor->setFocus();
    // Bring the restored caret into view, centred. Deferred to the event loop:
    // centring inline runs against the just-loaded document before its layout
    // and viewport have settled, which leaves the view stuck at the top while
    // the caret sits offscreen lower down.
    MarkdownEditor *ed = m_editor;
    QTimer::singleShot(0, ed, [ed] { ed->centerCursor(); });
    refreshMascot(); // mirror this note's inline seed (the editor parsed it on load)
    if (m_mobileLayout)
        showMobileEditor();
}

void MainWindow::showNotePage() {
    m_activePage = PageLocation::Kind::Note;
    updateSplitterHandleWidth();
    if (m_pageStack && m_notePage)
        m_pageStack->setCurrentWidget(m_notePage);
    if (m_findBar)
        m_findBar->setVisible(false);
    refreshMascot();
    updateReadModeUi();
}

void MainWindow::openGraphView() {
    showGraphView(false, QString(), true, true);
}

void MainWindow::openLocalGraphView() {
    if (m_currentPath.isEmpty()) {
        notify(tr("Open a note to view its local graph"), 2200);
        return;
    }
    showGraphView(true, m_currentPath, true, true);
}

void MainWindow::showGraphView(bool local, const QString &rootPath, bool record,
                               bool saveBeforeOpen) {
    if (!m_vault || !m_graphPage)
        return;
    if (record)
        captureCurrentPageState();
    if (saveBeforeOpen)
        saveCurrent();

    const QString root = local ? (rootPath.isEmpty() ? m_currentPath : rootPath)
                               : QString();
    if (local && root.isEmpty()) {
        notify(tr("Open a note to view its local graph"), 2200);
        return;
    }

    m_activePage = local ? PageLocation::Kind::LocalGraph
                         : PageLocation::Kind::GlobalGraph;
    updateSplitterHandleWidth();
    m_graphPage->setSnapshot(m_linkGraphIndex.snapshot());
    if (local)
        m_graphPage->openLocal(root);
    else
        m_graphPage->openGlobal(m_currentPath);
    if (m_pageStack)
        m_pageStack->setCurrentWidget(m_graphPage);
    if (m_findBar)
        m_findBar->hide();
    if (m_mascot)
        m_mascot->hide();
    if (record)
        pushHistory({m_activePage, root});
    selectInTree(local ? root : QString());
    setWindowTitle(local
                       ? QStringLiteral("Emerald — Local Graph")
                       : QStringLiteral("Emerald — Graph"));
    updateReadModeUi();
    updateNavActions();
    if (m_mobileLayout)
        showMobileEditor();
}

void MainWindow::renameCurrent(const QString &rawTitle) {
    if (m_readMode || !m_vault || m_currentPath.isEmpty())
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

    const QStringList changedLinks = m_vault->updateLinksToPaths(oldTitle, newTitle);
    m_currentPath = newPath;
    m_currentTitle = newTitle;
    watchCurrent(); // follow the file to its new name
    for (PageLocation &location : m_history)
        if (location.path == oldPath)
            location.path = newPath;
    if (m_cursorPositions.contains(oldPath))
        m_cursorPositions[newPath] = m_cursorPositions.take(oldPath);
    m_searchIndex.renamePath(oldPath, newPath);
    m_searchIndex.updateNote(newPath, newTitle, m_editor->toPlainText());
    m_linkGraphIndex.renamePath(oldPath, newPath, newTitle);
    m_linkGraphIndex.updateNote(newPath, newTitle, m_editor->toPlainText());
    m_noteMeta.remove(oldPath);
    markNoteMetaCurrent(newPath, newTitle);
    for (const QString &p : changedLinks) {
        const QString content = m_vault->read(p);
        m_searchIndex.updateNote(p, Vault::titleFromPath(p), content);
        m_linkGraphIndex.updateNote(p, Vault::titleFromPath(p), content);
        markNoteMetaCurrent(p, Vault::titleFromPath(p));
    }
    m_linkGraphIndex.setNotes(m_vault->root(), m_vault->notes());
    refreshGraphPage();
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
        QString dir = m_pendingNoteDir;
        if (dir.isEmpty() || !QDir(dir).exists())
            dir = defaultNoteDirectory();
        const Note note = m_vault->createNoteIn(dir, title);
        if (note.path.isEmpty())
            return;
        m_currentPath = note.path;
        m_currentTitle = title;
        m_pendingNoteDir.clear();
        const QString content = m_editor->toPlainText();
        m_vault->write(note.path, content);
        m_lastSavedFingerprint = contentFingerprint(content);
        m_editor->sourceDocument()->setModified(false);
        m_vault->scan();
        m_searchIndex.updateNote(note.path, note.title, content);
        m_linkGraphIndex.setNotes(m_vault->root(), m_vault->notes());
        m_linkGraphIndex.updateNote(note.path, note.title, content);
        markNoteMetaCurrent(note.path, note.title);
        refreshTree();
        watchCurrent();
        selectInTree(note.path);
        setWindowTitle(QStringLiteral("Emerald — %1").arg(title));
        QSettings().setValue(QStringLiteral("lastNote"), note.path);
        pushHistory({PageLocation::Kind::Note, note.path});
        updateNavActions();
        notify(tr("Created “%1”").arg(title), 2000);
        return;
    }
    if (!m_editor->sourceDocument()->isModified())
        return;
    const QString content = m_editor->toPlainText();
    const quint64 fingerprint = contentFingerprint(content);
    if (fingerprint == m_lastSavedFingerprint) {
        m_editor->sourceDocument()->setModified(false);
        return; // nothing new to flush; avoid a self-triggered watcher event
    }
    m_vault->write(m_currentPath, content);
    m_lastSavedFingerprint = fingerprint;
    m_editor->sourceDocument()->setModified(false);
    m_searchIndex.updateNote(m_currentPath, m_currentTitle, content);
    m_linkGraphIndex.updateNote(m_currentPath, m_currentTitle, content);
    refreshGraphPage();
    markNoteMetaCurrent(m_currentPath, m_currentTitle);
    if (auto *model = static_cast<NoteTreeModel *>(m_noteTreeModel))
        model->updateModificationTime(
            m_currentPath, QFileInfo(m_currentPath).lastModified());
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
    const quint64 diskFingerprint = contentFingerprint(disk);
    if (diskFingerprint == m_lastSavedFingerprint)
        return; // our own write, or no real change

    if (m_editor->sourceDocument()->isModified()) {
        if (contentFingerprint(m_editor->toPlainText()) == m_lastSavedFingerprint) {
            m_editor->sourceDocument()->setModified(false);
        } else {
            notify(tr("Changed on disk — saving will keep your version"), 5000);
            return;
        }
    }

    // No local edits: reload, keeping the caret roughly where it was.
    const int caret = m_editor->sourceTextCursor().position();
    m_editor->clearFolds(); // reloading replaces the content; drop stale folds
    m_loading = true;
    m_editor->setPlainText(disk);
    m_editor->sourceDocument()->setModified(false);
    m_loading = false;
    m_lastSavedFingerprint = diskFingerprint;
    m_searchIndex.updateNote(m_currentPath, m_currentTitle, disk);
    m_linkGraphIndex.updateNote(m_currentPath, m_currentTitle, disk);
    refreshGraphPage();
    markNoteMetaCurrent(m_currentPath, m_currentTitle);
    if (auto *model = static_cast<NoteTreeModel *>(m_noteTreeModel))
        model->updateModificationTime(
            m_currentPath, QFileInfo(m_currentPath).lastModified());

    QTextCursor c = m_editor->sourceTextCursor();
    c.setPosition(qMin(caret, int(disk.size())));
    m_editor->setSourceTextCursor(c);
    notify(tr("Reloaded — changed on disk"), 3000);
}

QString MainWindow::defaultNoteDirectory() const {
    if (!m_vault)
        return {};

    // Create in the configured folder (default: vault root), falling back to
    // the root if the saved folder no longer exists.
    QString dir = m_vault->root();
    const QString rel = VaultSettings::value(
        m_vault->root(), QStringLiteral("newNoteFolder"));
    if (!rel.isEmpty()) {
        const QString candidate = QDir(m_vault->root()).filePath(rel);
        if (QDir(candidate).exists())
            dir = candidate;
    }
    return dir;
}

void MainWindow::newNote() {
    if (!ensureVaultWritable())
        return;
    if (!m_vault) {
        chooseVault();
        if (!m_vault)
            return;
    }
    newNoteIn(defaultNoteDirectory());
}

void MainWindow::onLinkClicked(const QString &target) {
    if (!m_vault)
        return;
    QString path = m_vault->pathForTitle(target);
    if (path.isEmpty()) {
        if (m_readMode) {
            notify(tr("“%1” does not exist — Read Mode is on").arg(target),
                   3000);
            return;
        }
        if (!Vault::isValidTitle(target)) {
            notify(tr("“%1” is not a valid note name").arg(target), 3000);
            return;
        }
        const Note note =
            m_vault->createNoteIn(defaultNoteDirectory(), target);
        if (note.path.isEmpty()) {
            notify(tr("Could not create “%1”").arg(target), 3000);
            return;
        }
        const QString content = m_vault->read(note.path);
        // createNoteIn() does not mutate the cached vault listing. Rescan so a
        // link-created note in a nested default folder immediately reaches the
        // tree and wiki-link completion list.
        m_vault->scan();
        m_searchIndex.updateNote(note.path, note.title, content);
        m_linkGraphIndex.setNotes(m_vault->root(), m_vault->notes());
        m_linkGraphIndex.updateNote(note.path, note.title, content);
        markNoteMetaCurrent(note.path, note.title);
        refreshTree();
        path = note.path;
    }
    openNoteByPath(path);
}

void MainWindow::navigateBack() {
    captureCurrentPageState();
    if (hasUnsavedDraft()) {
        if (m_histIndex >= 0 && m_histIndex < m_history.size())
            openHistoryLocation(m_history.at(m_histIndex), false);
        return;
    }
    if (m_histIndex > 0) {
        --m_histIndex;
        openHistoryLocation(m_history.at(m_histIndex));
    }
}

void MainWindow::navigateForward() {
    captureCurrentPageState();
    if (hasUnsavedDraft()) {
        if (m_histIndex >= 0 && m_histIndex < m_history.size() - 1) {
            ++m_histIndex;
            openHistoryLocation(m_history.at(m_histIndex), false);
        }
        return;
    }
    if (m_histIndex >= 0 && m_histIndex < m_history.size() - 1) {
        ++m_histIndex;
        openHistoryLocation(m_history.at(m_histIndex));
    }
}

void MainWindow::openHistoryLocation(const PageLocation &location,
                                     bool saveBeforeOpen) {
    if (location.kind == PageLocation::Kind::Note)
        openNoteByPath(location.path, false, saveBeforeOpen);
    else {
        showGraphView(location.kind == PageLocation::Kind::LocalGraph,
                      location.path, false, saveBeforeOpen);
        if (m_graphPage && !location.viewState.isEmpty())
            m_graphPage->restoreSessionState(location.viewState);
    }
}

void MainWindow::captureCurrentPageState() {
    if (!m_graphPage || !m_pageStack ||
        m_pageStack->currentWidget() != m_graphPage || m_histIndex < 0 ||
        m_histIndex >= m_history.size())
        return;
    PageLocation &location = m_history[m_histIndex];
    location.kind = m_graphPage->isLocal()
                        ? PageLocation::Kind::LocalGraph
                        : PageLocation::Kind::GlobalGraph;
    location.path = m_graphPage->isLocal() ? m_graphPage->localRoot()
                                           : QString();
    location.viewState = m_graphPage->sessionState();
    m_activePage = location.kind;
}

bool MainWindow::hasUnsavedDraft() const {
    return m_currentPath.isEmpty() && !m_pendingNoteDir.isEmpty();
}

void MainWindow::pushHistory(const PageLocation &location) {
    if (m_histIndex >= 0 && m_history.at(m_histIndex) == location)
        return; // re-opening the current note shouldn't add an entry
    while (m_history.size() > m_histIndex + 1)
        m_history.removeLast(); // opening a note drops the forward branch
    m_history.append(location);
    m_histIndex = m_history.size() - 1;
}

void MainWindow::pruneHistory() {
    // The note the index currently points at; we keep the index on it if it
    // survives the prune (so deleting a *different* note doesn't move our spot).
    const PageLocation current =
        (m_histIndex >= 0 && m_histIndex < m_history.size())
            ? m_history.at(m_histIndex)
            : PageLocation{};
    m_history.erase(
        std::remove_if(m_history.begin(), m_history.end(),
                       [](const PageLocation &location) {
                           return location.kind !=
                                      PageLocation::Kind::GlobalGraph &&
                                  !QFileInfo::exists(location.path);
                       }),
        m_history.end());
    m_histIndex = m_history.lastIndexOf(current);
    if (m_histIndex < 0)
        m_histIndex = m_history.size() - 1;
    updateNavActions();
}

void MainWindow::updateNavActions() {
    const bool canLeaveDraft =
        hasUnsavedDraft() && m_histIndex >= 0 && m_histIndex < m_history.size();
    if (m_backAction)
        m_backAction->setEnabled(canLeaveDraft || m_histIndex > 0);
    if (m_forwardAction)
        m_forwardAction->setEnabled(m_histIndex >= 0 &&
                                    m_histIndex < m_history.size() - 1);
}

// Persist the per-note caret positions (folding in the open note's current
// caret) so reopening a note — even after a restart — lands where you left off.
void MainWindow::saveCursorPositions() {
    if (m_editor && !m_currentPath.isEmpty())
        m_cursorPositions[m_currentPath] =
            m_editor->sourceTextCursor().position();
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
    auto *model = static_cast<NoteTreeModel *>(m_noteTreeModel);
    const QModelIndex idx = model ? model->indexForPath(path) : QModelIndex();
    if (idx.isValid() && m_noteTree->selectionModel()) {
        m_noteTree->selectionModel()->setCurrentIndex(
            idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_noteTree->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        return;
    }
    if (m_noteTree->selectionModel())
        m_noteTree->selectionModel()->clearSelection();
}

void MainWindow::openSearch() {
    if (m_vault)
        m_searchPopup->showCentered(false);
}

void MainWindow::openQuickOpen() {
    if (m_vault)
        m_searchPopup->showCentered(true); // titles only
}

void MainWindow::openBrokenLinks() {
    if (!m_vault)
        return;

    // Flush the active buffer and refresh the vault listing so the report is a
    // snapshot of what the user currently sees, including outside file edits.
    saveCurrent();
    rescanVaultIncremental();

    const QVector<Vault::BrokenLink> issues = m_vault->brokenLinks();
    QList<SearchPopup::BrokenLinkItem> items;
    items.reserve(issues.size());
    const QDir root(m_vault->root());
    for (const Vault::BrokenLink &issue : issues) {
        const QString status =
            issue.state == Vault::BrokenLink::State::MissingNote
                ? tr("Missing note")
                : tr("Empty note");
        const QString source = root.relativeFilePath(issue.sourcePath);
        const QString label =
            tr("[[%1]]  —  %2\nFrom %3  ·  Line %4")
                .arg(issue.target, status, source)
                .arg(issue.line);
        items.append({label, issue.sourcePath, issue.sourcePosition,
                      issue.sourceLength});
    }
    m_searchPopup->showBrokenLinks(items);
}

void MainWindow::openBrokenLinkSource(const QString &path, int position,
                                      int length) {
    openNoteByPath(path);
    const int documentEnd =
        qMax(0, m_editor->sourceDocument()->characterCount() - 1);
    const int start = qBound(0, position, documentEnd);
    const int end = qBound(start, position + length, documentEnd);
    QTextCursor cursor(m_editor->sourceDocument());
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    m_editor->setSourceTextCursor(cursor);
    m_editor->centerCursor();
    m_editor->setFocus();
}

void MainWindow::insertTemplate() {
    if (!ensureVaultWritable())
        return;
    if (!m_vault)
        return;
    const QString rel = VaultSettings::value(
        m_vault->root(), QStringLiteral("templatesFolder"));
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
    if (m_readMode || !m_vault || path.isEmpty())
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

void MainWindow::insertImage() {
    if (!ensureVaultWritable())
        return;
    if (!m_vault || m_currentPath.isEmpty()) {
        notify(tr("Open a note before inserting images"), 2500);
        return;
    }

    QStringList patterns;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats()) {
        const QString suffix = QString::fromLatin1(fmt).toLower();
        const QString pattern = QStringLiteral("*.") + suffix;
        if (!patterns.contains(pattern))
            patterns.append(pattern);
    }
    patterns.sort(Qt::CaseInsensitive);
    const QString filter =
        tr("Images (%1);;All Files (*)").arg(patterns.join(QLatin1Char(' ')));
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Insert Image"), QFileInfo(m_currentPath).absolutePath(), filter,
        nullptr, QFileDialog::DontUseNativeDialog);
    insertImagesFromFiles(paths);
}

void MainWindow::insertImagesFromFiles(const QStringList &paths) {
    if (paths.isEmpty())
        return;
    if (!ensureVaultWritable())
        return;
    if (!m_vault || m_currentPath.isEmpty()) {
        notify(tr("Open a note before inserting images"), 2500);
        return;
    }

    QStringList lines;
    int skipped = 0;
    for (const QString &sourcePath : paths) {
        const QFileInfo sourceInfo(sourcePath);
        const QString attachedPath = attachImageFile(sourcePath);
        if (attachedPath.isEmpty()) {
            ++skipped;
            continue;
        }
        lines.append(imageMarkdownForPath(attachedPath,
                                          sourceInfo.completeBaseName()));
    }

    if (lines.isEmpty()) {
        notify(tr("No readable image was inserted"), 3000);
        return;
    }

    insertImageMarkdownLines(lines);
    if (skipped > 0)
        notify(tr("Inserted %n image(s); skipped %1", nullptr, lines.size())
                   .arg(skipped),
               3500);
    else
        notify(tr("Inserted %n image(s)", nullptr, lines.size()), 2500);
}

void MainWindow::insertPastedImage(const QImage &image) {
    if (!ensureVaultWritable())
        return;
    if (!m_vault || m_currentPath.isEmpty()) {
        notify(tr("Open a note before inserting images"), 2500);
        return;
    }
    const QString attachedPath = savePastedImageAttachment(image);
    if (attachedPath.isEmpty()) {
        notify(tr("Could not save pasted image"), 3000);
        return;
    }
    insertImageMarkdownLines(
        {imageMarkdownForPath(attachedPath, tr("Pasted image"))});
    notify(tr("Inserted pasted image"), 2500);
}

QString MainWindow::attachImageFile(const QString &sourcePath) {
    if (!m_vault)
        return QString();

    const QFileInfo sourceInfo(sourcePath);
    QImageReader reader(sourcePath);
    if (!sourceInfo.isFile() || !reader.canRead())
        return QString();

    const QString rootPath = QDir(m_vault->root()).canonicalPath();
    const QString sourceCanonical = sourceInfo.canonicalFilePath();
    if (!rootPath.isEmpty() && !sourceCanonical.isEmpty()) {
#ifdef Q_OS_WIN
        constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
        constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
        if (sourceCanonical.compare(rootPath, cs) == 0 ||
            sourceCanonical.startsWith(rootPath + QLatin1Char('/'), cs)) {
            return sourceInfo.absoluteFilePath();
        }
    }

    QString suffix = sourceInfo.suffix().toLower();
    if (suffix.isEmpty())
        suffix = QString::fromLatin1(reader.format()).toLower();
    const QString dest =
        uniqueAttachmentPath(sourceInfo.completeBaseName(), suffix);
    if (dest.isEmpty())
        return QString();
    if (!QDir().mkpath(QFileInfo(dest).absolutePath()))
        return QString();
    return QFile::copy(sourcePath, dest) ? dest : QString();
}

QString MainWindow::savePastedImageAttachment(const QImage &image) {
    if (!m_vault || image.isNull())
        return QString();

    const QString base =
        QStringLiteral("pasted-image-") +
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString dest = uniqueAttachmentPath(base, QStringLiteral("png"));
    if (dest.isEmpty())
        return QString();
    if (!QDir().mkpath(QFileInfo(dest).absolutePath()))
        return QString();
    return image.save(dest, "PNG") ? dest : QString();
}

QString MainWindow::uniqueAttachmentPath(const QString &baseName,
                                         const QString &suffix) const {
    if (!m_vault)
        return QString();
    QString ext = suffix.trimmed().toLower();
    if (ext.isEmpty())
        ext = QStringLiteral("png");
    if (!ext.startsWith(QLatin1Char('.')))
        ext.prepend(QLatin1Char('.'));

    const QDir dir(QDir(m_vault->root()).filePath(QStringLiteral("_attachments")));
    const QString base = attachmentBaseName(baseName);
    QString candidate = dir.filePath(base + ext);
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir.filePath(QStringLiteral("%1-%2%3").arg(base).arg(n).arg(ext));
        ++n;
    }
    return candidate;
}

QString MainWindow::imageMarkdownForPath(const QString &path,
                                         const QString &altText) const {
    if (!m_vault || path.isEmpty())
        return QString();
    const QString baseDir = m_currentPath.isEmpty()
                                ? m_vault->root()
                                : QFileInfo(m_currentPath).absolutePath();
    const QString rel = QDir(baseDir).relativeFilePath(path);
    return QStringLiteral("![%1](%2)")
        .arg(markdownAltText(altText), markdownImageTarget(rel));
}

void MainWindow::insertImageMarkdownLines(const QStringList &lines) {
    if (m_readMode || lines.isEmpty())
        return;
    QTextCursor c = m_editor->textCursor();
    if (c.position() < m_editor->firstContentPosition())
        c.setPosition(m_editor->firstContentPosition());

    QString insertion = lines.join(QLatin1Char('\n'));
    if (!c.atBlockStart())
        insertion.prepend(QLatin1Char('\n'));
    if (!c.atBlockEnd())
        insertion.append(QLatin1Char('\n'));

    c.beginEditBlock();
    c.insertText(insertion);
    c.endEditBlock();
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

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateResponsiveLayout();
    positionShortcutCheatsheet();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::ApplicationDeactivate ||
        (watched == this && event->type() == QEvent::WindowDeactivate)) {
        hideShortcutCheatsheet();
    }

    if (event->type() == QEvent::KeyPress ||
        event->type() == QEvent::KeyRelease) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool pressed = event->type() == QEvent::KeyPress;

        // Always observe the releases that end an active hold, even if focus
        // moved between children while the panel was visible. Let Alt release
        // continue to the editor so its independent Quick Jump state also
        // returns to idle; X remains consumed and can never reach note text.
        if (m_shortcutCheatsheetHeld) {
            if (keyEvent->isAutoRepeat() &&
                (keyEvent->key() == Qt::Key_Alt ||
                 keyEvent->key() == Qt::Key_X)) {
                return true;
            }
            if (pressed && keyEvent->key() == Qt::Key_X) {
                if (m_shortcutReleaseTimer)
                    m_shortcutReleaseTimer->stop();
                return true;
            }
            if (!pressed && keyEvent->key() == Qt::Key_Alt) {
                hideShortcutCheatsheet();
                return false;
            }
            if (!pressed && keyEvent->key() == Qt::Key_X) {
                if (m_shortcutReleaseTimer)
                    m_shortcutReleaseTimer->start();
                else
                    hideShortcutCheatsheet();
                return true;
            }
            return true;
        }

        QWidget *receiver = qobject_cast<QWidget *>(watched);
        const bool belongsToMainWindow =
            receiver && (receiver == this || isAncestorOf(receiver));
        const Qt::KeyboardModifiers modifiers =
            keyEvent->modifiers() &
            ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
        if (pressed && belongsToMainWindow &&
            keyEvent->key() == Qt::Key_X &&
            modifiers == Qt::AltModifier) {
            showShortcutCheatsheet();
            return true;
        }
    }

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
    } else if (watched == m_titleEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers mods = ke->modifiers();
        if ((mods & Qt::AltModifier) &&
            !(mods & (Qt::ControlModifier | Qt::MetaModifier |
                      Qt::ShiftModifier)) &&
            (ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right)) {
            if (ke->key() == Qt::Key_Left)
                navigateBack();
            else
                navigateForward();
            return true;
        }
        if ((mods & Qt::ControlModifier) &&
            !(mods & (Qt::AltModifier | Qt::MetaModifier |
                      Qt::ShiftModifier)) &&
            (ke->key() == Qt::Key_BracketLeft ||
             ke->key() == Qt::Key_BracketRight)) {
            if (ke->key() == Qt::Key_BracketLeft)
                navigateBack();
            else
                navigateForward();
            return true;
        }
#ifdef Q_OS_MACOS
        if ((mods & Qt::MetaModifier) &&
            !(mods & (Qt::AltModifier | Qt::ControlModifier |
                      Qt::ShiftModifier)) &&
            (ke->key() == Qt::Key_BracketLeft ||
             ke->key() == Qt::Key_BracketRight)) {
            if (ke->key() == Qt::Key_BracketLeft)
                navigateBack();
            else
                navigateForward();
            return true;
        }
#endif
    } else if (watched == m_editor && event->type() == QEvent::Resize) {
        if (m_findBar && m_findBar->isVisible())
            positionFindBar();
        if (m_toast && m_toast->isVisible())
            positionToast();
    } else if (watched == m_centerPane && event->type() == QEvent::Resize) {
        if (m_mascot && m_mascot->isVisible())
            positionMascot();
        if (m_toast && m_toast->isVisible())
            positionToast();
    } else if (watched == m_splitHandle) {
        // A click (press + release without a drag) toggles the sidebar; a real
        // drag is left to the splitter.
        if (event->type() == QEvent::MouseButtonPress) {
            m_handlePressPos = static_cast<QMouseEvent *>(event)->globalPosition()
                                   .toPoint();
        } else if (event->type() == QEvent::MouseButtonRelease) {
            const QPoint up = static_cast<QMouseEvent *>(event)->globalPosition()
                                  .toPoint();
            if ((up - m_handlePressPos).manhattanLength() < 4)
                toggleSidebar();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onTreeIndexClicked(const QModelIndex &index) {
    // Ctrl/Shift clicks are selection gestures (multi-select) — let them just
    // extend the selection without opening a note or folding a folder.
    if (QGuiApplication::keyboardModifiers() &
        (Qt::ControlModifier | Qt::ShiftModifier))
        return;
    const QString path = index.data(kPathRole).toString();
    if (!path.isEmpty()) {
        openNoteByPath(path);
        return;
    }
    // A single click on a folder row folds / unfolds it.
    if (!index.data(kDirRole).toString().isEmpty())
        m_noteTree->setExpanded(index, !m_noteTree->isExpanded(index));
}

void MainWindow::onTreeContextMenu(const QPoint &pos) {
    if (!m_vault)
        return;
    const QModelIndex item = m_noteTree->indexAt(pos);
    const QString notePath = item.isValid() ? item.data(kPathRole).toString() : QString();
    const QString folderPath = item.isValid() ? item.data(kDirRole).toString() : QString();

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
    if (m_noteTree->selectionModel()) {
        const QModelIndexList rows = m_noteTree->selectionModel()->selectedRows();
        for (const QModelIndex &idx : rows) {
            const QString d = idx.data(kDirRole).toString();
            const QString p = d.isEmpty() ? idx.data(kPathRole).toString() : d;
            if (!p.isEmpty())
                selPaths << p;
        }
    }
    const bool bulk = item.isValid() && m_noteTree->selectionModel() &&
                      m_noteTree->selectionModel()->isSelected(item) &&
                      selPaths.size() > 1;

    QMenu menu(this);
    menu.setObjectName(QStringLiteral("noteTreeContextMenu"));
    QAction *newNote =
        menu.addAction(tr("New Note"), this, [this, dir] { newNoteIn(dir); });
    QAction *newFolder = menu.addAction(
        tr("New Folder"), this, [this, dir] { newFolderIn(dir); });
    const bool writablePage = !m_readMode &&
                              m_activePage == PageLocation::Kind::Note;
    newNote->setEnabled(writablePage);
    newFolder->setEnabled(writablePage);
    if (bulk) {
        menu.addSeparator();
        QAction *remove = menu.addAction(
            tr("Delete %1 Items").arg(selPaths.size()), this,
            [this, selPaths] { deleteEntries(selPaths); });
        remove->setEnabled(writablePage);
    } else if (!notePath.isEmpty()) {
        menu.addSeparator();
        QAction *remove = menu.addAction(
            tr("Delete Note"), this,
            [this, notePath] { deleteEntries({notePath}); });
        remove->setEnabled(writablePage);
    } else if (!folderPath.isEmpty()) {
        menu.addSeparator();
        QAction *remove = menu.addAction(
            tr("Delete Folder"), this,
            [this, folderPath] { deleteEntries({folderPath}); });
        remove->setEnabled(writablePage);
    }
    menu.exec(m_noteTree->viewport()->mapToGlobal(pos));
}

// Clear per-vault settings that pointed at a deleted note/folder so Home, new
// note, and template choices never retain stale paths.
void MainWindow::clearStaleSettingsFor(const QString &path, bool isFolder) {
    const QString rel = QDir(m_vault->root()).relativeFilePath(path);
    const QString root = m_vault->root();
    const QString home =
        VaultSettings::value(root, QStringLiteral("homeNote"));
    const QString nf =
        VaultSettings::value(root, QStringLiteral("newNoteFolder"));
    const QString templates =
        VaultSettings::value(root, QStringLiteral("templatesFolder"));
    if (home == rel || (isFolder && home.startsWith(rel + QLatin1Char('/'))))
        VaultSettings::remove(root, QStringLiteral("homeNote"));
    if (isFolder && (nf == rel || nf.startsWith(rel + QLatin1Char('/'))))
        VaultSettings::remove(root, QStringLiteral("newNoteFolder"));
    if (isFolder &&
        (templates == rel || templates.startsWith(rel + QLatin1Char('/'))))
        VaultSettings::remove(root, QStringLiteral("templatesFolder"));
}

// After a deletion: rescan, and if the open note was removed (on its own or
// inside a deleted folder), prune history and open a sensible fallback.
void MainWindow::reconcileAfterDeletion() {
    const bool currentGone =
        !m_currentPath.isEmpty() && !QFileInfo::exists(m_currentPath);
    const QHash<QString, NoteFileMeta> previous = m_noteMeta;
    m_vault->scan();
    updateIndexForScannedVault(previous);
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
    m_pendingNoteDir.clear();
    m_lastSavedFingerprint = 0;
    // Empty the editor + title buffer now. Otherwise the pre-load save that
    // openNoteByPath() runs below would see an empty m_currentPath next to the
    // deleted note's still-present title/body and re-create it as a new note —
    // making the deletion appear to silently fail.
    m_editor->clearFolds(); // drop the deleted note's folds before clearing
    m_loading = true;
    m_editor->clear();
    m_editor->setImagePaths(QString(), QString());
    m_editor->sourceDocument()->setModified(false);
    m_loading = false;
    m_titleEdit->blockSignals(true);
    m_titleEdit->clear();
    m_titleEdit->blockSignals(false);

    pruneHistory();
    refreshTree();

    // Show the Home note, else the most recent still-open note, else blank.
    QString fallback;
    const QString home = VaultSettings::value(
        m_vault->root(), QStringLiteral("homeNote"));
    if (!home.isEmpty()) {
        const QString p = QDir(m_vault->root()).filePath(home);
        if (QFileInfo::exists(p))
            fallback = p;
    }
    if (fallback.isEmpty() && !m_history.isEmpty())
        for (auto it = m_history.crbegin(); it != m_history.crend(); ++it)
            if (it->kind == PageLocation::Kind::Note &&
                QFileInfo::exists(it->path)) {
                fallback = it->path;
                break;
            }
    if (!fallback.isEmpty())
        openNoteByPath(fallback);
    else {
        setWindowTitle(QStringLiteral("Emerald"));
        if (m_mobileLayout)
            showMobileNotes();
    }
}

void MainWindow::deleteEntries(const QStringList &pathsIn) {
    if (!ensureVaultWritable())
        return;
    // A selected folder takes its contents with it, so ignore nested children.
    const QStringList paths = topLevelPaths(pathsIn);
    if (paths.isEmpty())
        return;

    QString question;
    if (paths.size() == 1) {
        const QString p = paths.first();
        const bool isFolder = QFileInfo(p).isDir();
        question = isFolder ? tr("Move the folder to the trash?")
                            : tr("Move the note to the trash?");
    } else {
        question = tr("Move the selected items to the trash?");
    }
    if (!runTrashDialog(this, question))
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
    if (!ensureVaultWritable())
        return;
    if (!m_vault)
        return;
    saveCurrent();

    m_currentPath.clear();
    m_currentTitle.clear();
    m_pendingNoteDir = QDir(dir).exists() ? dir : m_vault->root();
    m_lastSavedFingerprint = 0;

    m_editor->clearFolds();
    m_loading = true;
    m_editor->clear();
    m_editor->setImagePaths(m_pendingNoteDir, m_vault->root());
    m_editor->sourceDocument()->setModified(false);
    m_loading = false;

    m_titleEdit->blockSignals(true);
    m_titleEdit->clear();
    m_titleEdit->blockSignals(false);
    m_titleEdit->setFocus();

    if (m_noteTree && m_noteTree->selectionModel())
        m_noteTree->selectionModel()->clearSelection();
    setWindowTitle(QStringLiteral("Emerald — New Note"));
    updateNavActions();
    if (m_mobileLayout)
        showMobileEditor();
}

void MainWindow::moveItems(const QStringList &srcPaths, const QString &destDirIn) {
    if (!ensureVaultWritable())
        return;
    if (!m_vault || srcPaths.isEmpty())
        return;
    const QString destDir = destDirIn.isEmpty() ? m_vault->root() : destDirIn;
    int moved = 0;
    for (const QString &srcPath : srcPaths) {
        QStringList movedNotes;
        const bool movingFolder = QFileInfo(srcPath).isDir();
        if (movingFolder) {
            for (const Note &n : m_vault->notes())
                if (n.path == srcPath ||
                    n.path.startsWith(srcPath + QLatin1Char('/')))
                    movedNotes << n.path;
        }
        const QString newPath = m_vault->movePath(srcPath, destDir);
        if (newPath.isEmpty())
            continue;
        if (movingFolder) {
            for (const QString &p : movedNotes) {
                const QString mapped = p == srcPath ? newPath
                                                    : newPath + p.mid(srcPath.length());
                m_searchIndex.renamePath(p, mapped, Vault::titleFromPath(mapped));
                m_linkGraphIndex.renamePath(p, mapped,
                                            Vault::titleFromPath(mapped));
            }
        } else {
            m_searchIndex.renamePath(srcPath, newPath, Vault::titleFromPath(newPath));
            m_linkGraphIndex.renamePath(srcPath, newPath,
                                        Vault::titleFromPath(newPath));
        }
        // Follow the open note / history if they lived in what just moved.
        auto remap = [&](QString &p) {
            if (p == srcPath)
                p = newPath;
            else if (p.startsWith(srcPath + QLatin1Char('/')))
                p = newPath + p.mid(srcPath.length());
        };
        remap(m_currentPath);
        for (PageLocation &location : m_history)
            remap(location.path);
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
    m_linkGraphIndex.setNotes(m_vault->root(), m_vault->notes());
    m_noteMeta = scannedNoteMeta();
    refreshTree();
    refreshGraphPage();
    if (!m_currentPath.isEmpty()) {
        setWindowTitle(
            QStringLiteral("Emerald — %1").arg(Vault::titleFromPath(m_currentPath)));
        selectInTree(m_currentPath);
        QSettings().setValue(QStringLiteral("lastNote"), m_currentPath);
    }
}

void MainWindow::newFolderIn(const QString &dir) {
    if (!ensureVaultWritable())
        return;
    QString name;
    if (!runFolderNameDialog(this, &name))
        return;
    if (!m_vault->createFolder(dir, name)) {
        notify(tr("Couldn't create that folder"), 3000);
        return;
    }
    m_vault->scan();
    m_linkGraphIndex.setNotes(m_vault->root(), m_vault->notes());
    refreshTree();
    refreshGraphPage();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveCurrent();
    if (m_vault && m_graphPage)
        VaultSettings::setValue(m_vault->root(), QStringLiteral("graphState"),
                                m_graphPage->savedState());
    saveCursorPositions(); // remember caret positions for the next launch
    if (m_mobileLayout && !m_desktopSplitterSizes.isEmpty()) {
        const QList<int> mobileSizes = m_splitter->sizes();
        m_splitter->setSizes(m_desktopSplitterSizes);
        QSettings().setValue(QStringLiteral("splitterState"),
                             m_splitter->saveState());
        m_splitter->setSizes(mobileSizes);
    } else {
        QSettings().setValue(QStringLiteral("splitterState"),
                             m_splitter->saveState());
    }
    event->accept();
}
