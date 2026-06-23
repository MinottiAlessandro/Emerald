# Emerald — Manual Test Checklist

A full functional checklist for verifying a build on any machine (Linux ·
macOS · Windows). Copy this file, tick the boxes, note the OS/version at the
top. Items marked **★** are 1.4.2 regression checks; **◆** are new in 1.5.0 —
verify those first on a fresh build.

> Environment: OS ____________ · version ____________ · build/tag ____________

---

## 1. First run & vault management
- [ ] App launches with no vault and prompts / lets you open one.
- [ ] **Open vault** (`Ctrl+O`) — pick a folder; the sidebar fills with its `.md` files.
- [ ] **Switch vault** (`Ctrl+Shift+O`) — opens a different folder, sidebar reloads. ★
- [ ] Re-launch the app — it reopens the last vault automatically.
- [ ] Opening a folder with sub-folders shows the full tree.

## 2. Notes (create / rename / delete)
- [ ] **New note** (`Ctrl+N`) — appears in the sidebar, editor focuses the body.
- [ ] Right-click in the tree → **New note** and **New sub-folder** create them where clicked.
- [ ] Title line (top of editor) = filename; editing the title renames the file on disk.
- [ ] **Rename** (`F2`) — focuses the title and selects all. ★
- [ ] Renaming a note rewrites every inbound `[[link]]` in other notes.
- [ ] **Delete note** (`Ctrl+Shift+Backspace`) — removes the note (with confirm if applicable). ★
- [ ] `Ctrl+Delete` in the body still deletes the word forward (does NOT delete the note). ★
- [ ] Drag-and-drop a note in the tree moves it to another folder.

## 3. Live preview — inline formatting
For each: type the markup, move the caret off the line, confirm it renders;
move the caret back onto the line, confirm the raw markers reappear.
- [ ] Headings `#` … `######` (sizes scale with font).
- [ ] **Bold** `**x**`, *italic* `*x*`, ***both*** `***x***`.
- [ ] `inline code`, ~~strike~~ `~~x~~`, ==highlight== `==x==`.
- [ ] Blockquote `> x`, horizontal rule `---`.
- [ ] Task list `- [ ]` / `- [x]` — clicking the rendered checkbox toggles it.
- [ ] **Overlapping emphasis** — `==dog ~~cat *horse **elephant***~~==` stacks all four styles.
- [ ] **Selecting across lines reveals raw markup on every selected line.** Select several rendered lines (a heading, **bold**, a `[link](url)`, a list item, a table row, a `$…$` formula, a code block) — each selected line shows its actual source, not the rendered form; lines outside the selection stay rendered. Collapsing/shrinking the selection re-renders the deselected lines. ◆
- [ ] **Editor-painted decorations also melt under a selection** (not just the highlighter text): selecting across them shows the raw source. ◆
  - [ ] **Bullet glyphs** (●/○/▪) → raw `-`/`*`/`+`. ◆
  - [ ] **Task checkboxes** (☐/☑) → raw `- [ ]` / `- [x]`. ◆
  - [ ] **Horizontal rule** line → raw `---`. ◆
  - [ ] **Code-block box** (dark background + header bar) → plain text with the raw ` ``` ` fences, no box. ◆
  - [ ] Same elements still render when not selected, and show raw when the bare caret is on their line. ◆

## 4. Lists
- [ ] Enter continues a bullet / numbered / task list (numbers increment, indent preserved).
- [ ] Enter mid-item splits, carrying text after the caret to a new marked item.
- [ ] Enter on an empty item ends the list.
- [ ] `Tab` / `Shift+Tab` indent / outdent an item.
- [ ] Multi-line selection + `Tab`/`Shift+Tab` indents/outdents every line.
- [ ] Off the active line, dashes render as bullet glyphs (●/○/▪ by nesting level).
- [ ] **Ctrl+Enter** opens a new line below without splitting, continuing the list.

## 5. Tables
- [ ] Enter on a header row auto-adds the `| --- |` separator + first data row; caret lands in cell 1.
- [ ] `Tab` walks cells and grows the grid at its edges; columns re-align on each press.
- [ ] Enter on the last row leaves the table.
- [ ] Leaving a table re-aligns (prettifies) its columns into a monospace grid.
- [ ] **A row that would wrap to a new line does NOT get auto-formatted** (grid no longer breaks). ★
- [ ] **Tab on the `| --- |` separator row** lands in the first cell of the data row below — creating that row only when the table is still just header + separator (an existing data row is reused, not pushed down). ◆

## 6. Code blocks ★
- [ ] **Type ```` ``` ````** — the third backtick auto-inserts a closing fence below; caret stays on the opening fence ready for a language tag. ★
- [ ] Off the active line, a fenced block renders as a code box (language tag + copy button).
- [ ] Caret **inside** the block reveals BOTH the opening and closing fences. ★
- [ ] Copy button copies the block's code.
- [ ] **Selecting the whole block + something outside** shows the raw source (` ``` `), NOT both the rendered box and raw backticks at once. ★
- [ ] No hairline/overpainted seam under the code-box header. ★
- [ ] Lines inside a code block render verbatim (no bullets, rules, headings).

## 7. Math (built-in TeX subset)
- [ ] Inline `$x^2$` renders in place; caret/selection inside shows raw source.
- [ ] Display `$$ … $$` renders; can span multiple lines.
- [ ] Bare dollars stay literal (`$5 and $12`).
- [ ] **Math inside inline code stays literal** — `` `$x^2$` `` shows the raw `$x^2$` as code, no formula painted (a bare `$x^2$` still renders). ◆
- [ ] Fractions `\frac`, roots `\sqrt`, sub/superscripts, big operators with limits (`\sum`, `\int`).
- [ ] Accents (`\hat`, `\vec`, `\bar`), `\left( … \right)`, matrices (`pmatrix`, `bmatrix`).
- [ ] A symbol command sample (Greek, arrows) renders.

## 8. Links & navigation
- [ ] `[[Note]]` renders, click jumps to the target.
- [ ] `[[Note]]` to a non-existent note auto-creates it.
- [ ] `[[Note|alias]]` shows just the alias.
- [ ] Typing `[[` pops fuzzy autocomplete of existing titles; selecting inserts it.
- [ ] External link `[text](https://…)` renders, opens in the system browser.
- [ ] **Insert link** (`Ctrl+K`) — wraps selection as `[sel]()` (caret in parens) or inserts `[]()` (caret in brackets). ★
- [ ] **Back / Forward** (`Alt+←` / `Alt+→`), mouse side buttons, and sidebar arrows all navigate history.

## 9. Search
- [ ] **Find in note** (`Ctrl+F`).
- [ ] **Search vault** (`Ctrl+Shift+F`) — popup with ranked results; type to filter, ↑/↓ to move, Enter jumps to first match.

## 10. Templates
- [ ] **Settings → Templates folder** points at a folder in the vault.
- [ ] **Insert Template…** (`Ctrl+T`) opens a picker of every note under it (sub-folders included). ★
- [ ] Chosen template drops in at the caret.
- [ ] Placeholders fill on insert: `{{date}}`, `{{time}}`, `{{title}}`.
- [ ] Formatted placeholders work: `{{date:YYYY/MM/DD}}`, `{{time:HH:mm:ss}}`.

## 11. Mascots
- [ ] A note past the char threshold (if auto-gen enabled in **Settings → Mascot**) grows a corner mascot.
- [ ] **Generate** / **Delete** mascot from the gear menu.
- [ ] Hover gives a gentle blink/bob.
- [ ] Clicking a mascot **or pressing `Ctrl+G`** opens the vault-wide gallery; clicking a creature jumps to its note. ◆
- [ ] Press **↑** at the top of a note reveals the hidden seed line (`<!-- mascot: … -->`); editing/deleting it updates the creature.
- [ ] The seed line is invisible in a plain Markdown viewer (it's an HTML comment).

## 12. Editor appearance & settings
- [ ] **Settings** (`Ctrl+,`) opens the dialog. ★
- [ ] Settings dialog shows shortcut labels next to actions where applicable. ★
- [ ] Change editor **font family + size** — applies and persists across restart.
- [ ] **Default font** is a system monospace face on a clean profile (no saved font override). ★
- [ ] **Line spacing** setting changes row spacing and survives note loads.
- [ ] **Font size** `Ctrl++` / `Ctrl+-` / `Ctrl+0` (up / down / reset).
- [ ] **Toggle sidebar** (`Ctrl+\` or the gear menu) collapses the left pane fully and restores it; clicking the splitter handle does the same. ◆
- [ ] Heading sizes scale with the body font.
- [ ] Dark theme renders correctly (embedded QSS).

## 13. Folding
- [ ] Fold control on a heading collapses everything down to the next same/higher heading.
- [ ] Fold control on a fenced code block collapses it.
- [ ] Editing visible trailing blank lines below a folded section doesn't pull more text into the fold.

## 14. Persistence & external edits
- [ ] Edits autosave (debounced) — re-open the note, changes are there.
- [ ] **Save now** (`Ctrl+S`).
- [ ] Edit a note's file outside the app — Emerald detects it and reloads.
- [ ] **Create** a `.md` file in the vault with another program — it appears in the sidebar tree without reopening the vault. ◆
- [ ] **Delete** a note's file externally — it vanishes from the tree. ◆
- [ ] **Rename** a note's file externally — the tree shows the new name. ◆
- [ ] Create a note inside a **new sub-folder** externally — both the folder and note appear (subfolders are watched too). ◆
- [ ] An externally-added note is immediately **searchable** (`Ctrl+Shift+F`) — the index rebuilds on the change. ◆
- [ ] Adding/removing files externally **keeps the open note selected** and expanded folders open. ◆
- [ ] **Edit the OPEN note in another editor and save** — Emerald reloads the new content in place (no blank, no stale text), without needing to switch notes. ◆
  - [ ] Works for an **atomic-save** editor (VS Code, gedit, Kate — write temp + rename). ◆
  - [ ] Works for a **backup-rename** editor (Vim default — moves the file aside, writes a fresh one). ◆
  - [ ] Works for an **in-place** editor (truncate + rewrite). ◆
- [ ] If you have **unsaved local edits** in Emerald and the file also changes on disk, Emerald keeps your version and warns ("Changed on disk — saving will keep your version") rather than overwriting. ◆
- [ ] If the open note is **deleted** externally, Emerald notes it ("removed on disk") and doesn't silently recreate an empty file. ◆

## 15. Updates
- [ ] **Check for Updates…** queries GitHub for the latest release.
- [ ] On Linux AppImage, the in-place update path works (if a newer release exists).

## 16. Keyboard shortcuts — full sweep ★
Verify each fires and that menu items show their shortcut label.

| Action                             | Shortcut                       | OK  |
| ---------------------------------- | ------------------------------ | --- |
| Open vault                         | `Ctrl+O`                       | [ ] |
| Switch vault                       | `Ctrl+Shift+O`                 | [ ] |
| New note                           | `Ctrl+N`                       | [ ] |
| Go to note (quick open)            | `Ctrl+P`                       | [ ] |
| Insert template                    | `Ctrl+T`                       | [ ] |
| Save now                           | `Ctrl+S`                       | [ ] |
| Rename note                        | `F2`                           | [ ] |
| Delete note                        | `Ctrl+Shift+Backspace`         | [ ] |
| Find in note                       | `Ctrl+F`                       | [ ] |
| Search vault                       | `Ctrl+Shift+F`                 | [ ] |
| Settings                           | `Ctrl+,`                       | [ ] |
| Back / Forward                     | `Alt+←` / `Alt+→`              | [ ] |
| Bold / Italic                      | `Ctrl+B` / `Ctrl+I`            | [ ] |
| Insert link                        | `Ctrl+K`                       | [ ] |
| Heading 1–6 (press again to clear) | `Ctrl+1` … `Ctrl+6`            | [ ] |
| Select line                        | `Ctrl+L`                       | [ ] |
| Move line up / down                | `Alt+↑` / `Alt+↓`              | [ ] |
| Indent / outdent                   | `Tab` / `Shift+Tab`            | [ ] |
| Font size up / down / reset        | `Ctrl++` / `Ctrl+-` / `Ctrl+0` | [ ] |
| Toggle sidebar                     | `Ctrl+\`                       | [ ] |
| Mascot gallery                     | `Ctrl+G`                       | [ ] |
| Quit                               | `Ctrl+Q`                       | [ ] |

> On macOS use **⌘** where **Ctrl** is listed.

## 17. Platform-specific launch
- [ ] **Linux x86-64**: `chmod +x Emerald-x86_64.AppImage` then run.
- [ ] **Linux ARM64**: runs on a glibc ≥ 2.39 system.
- [ ] **macOS**: first launch **right-click → Open → Open**; if still blocked, `xattr -cr /Applications/Emerald.app`.
- [ ] **Windows**: extract `Emerald-win64.zip`, run `emerald.exe`.
- [ ] Each package runs with no separate Qt install (bundled runtime).
