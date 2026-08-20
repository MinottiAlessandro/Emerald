# Emerald — Manual Test Checklist

A full functional checklist for verifying a build on any machine (Linux ·
macOS · Windows). Copy this file, tick the boxes, note the OS/version at the
top. Items marked **★** or **◆** cover historically high-risk regressions. For
2.0.0, prioritize Read Mode, Graph View, callouts, tables, lists, and scrolling.

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
- [ ] Start a quote group with `> [!tip]` — off the active line, `[!tip]`
      becomes a bold **💡 Tip** title and every quoted body line shares its
      green surface. `> [!warning] Custom` shows its unique warning emoji and
      colors the complete group orange. There is no background gap between
      title/body rows, and a top-level callout aligns with ordinary paragraphs.
      A `[!type]` marker on a later line remains ordinary quoted text.
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
- [ ] Long bullet, numbered, and task-list items align wrapped continuation lines
      with the item text, including at nested indentation levels.
- [ ] Enter mid-item splits, carrying text after the caret to a new marked item.
- [ ] Enter on an empty item ends the list.
- [ ] `Tab` / `Shift+Tab` indent / outdent an item.
- [ ] Multi-line selection + `Tab`/`Shift+Tab` indents/outdents every line.
- [ ] Off the active line, dashes render as bullet glyphs (●/○/▪ by nesting level).
- [ ] Consecutive indented bullet, numbered, and task items belong to their
      nearest shallower parent. A parent's fold arrow hides its whole deeper-
      indented run, stops before the next same-level item or paragraph, and
      expands again in both Edit and Read Mode. Nested parent arrows affect only
      their own descendants. ◆
- [ ] **Ctrl+Enter** opens a new line below without splitting, continuing the list.

## 4a. Spell checking
- [ ] Spell checking starts enabled with **English (US)** under **Settings → Spelling** and works after disconnecting the network.
- [ ] Type `This is a mistkae` and leave the word; only `mistkae` receives the red spelling underline. The incomplete word under the caret is not underlined until the caret leaves it.
- [ ] A misspelling inside bold/highlight/strike retains both its Markdown styling and the spelling underline.
- [ ] Inline/fenced code, `$math$`, URLs, email addresses, HTML, image paths, and unaliased `[[wiki targets]]` are ignored. Visible `[link labels](url)` and `[[target|aliases]]` are checked.
- [ ] Right-click a misspelling: correction suggestions appear on demand and replace only that word.
- [ ] **Add to personal dictionary** removes every matching underline immediately and survives restarting Emerald. **Ignore for this session** lasts only until restart.
- [ ] **Manage languages…** shows English as included. Download Italian, German, French, or Spanish; interrupted/corrupt downloads do not appear as installed. Select an installed language, restart, and confirm it remains selected. A selected pack cannot be removed until another language is active.
- [ ] Optional language downloads use the matching `spell-dictionaries-v*` Emerald release and still fail closed if an asset is missing, oversized, or does not match the SHA-256 embedded in the application.
- [ ] After installing an optional language, a newer application manifest with different hashes reports **Update available**. Updating safely replaces the old pack; an interrupted update leaves the existing files recoverable.
- [ ] Downloaded dictionaries and personal words live in Emerald's application-data folder, not in the vault.

## 5. Tables
- [ ] Enter anywhere on a fresh header row auto-adds the `| --- |` separator + first data row; caret lands in the first data cell.
- [ ] Enter anywhere in an established header or its separator row formats the table when it fits, then lands in the first cell of the first data row.
- [ ] Enter in a body row moves to the same cell in the data row below.
- [ ] Enter in the final data row appends a row and moves to the same cell in it.
- [ ] `Tab` walks cells and grows the grid at its edges; columns re-align on each press.
- [ ] Leaving a table re-aligns (prettifies) its columns when the padded rows fit the editor width.
- [ ] Edit Mode shows plain Markdown table source without a background, borders, header skin, or alternating row shading.
- [ ] Read Mode still renders a semantic table.
- [ ] **A row that would wrap to a new line does NOT get auto-formatted** (grid no longer breaks). ★
- [ ] **Tab on the `| --- |` separator row** lands in the first cell of the data row below — creating that row only when the table is still just header + separator (an existing data row is reused, not pushed down). ◆

## 6. Code blocks ★
- [ ] **Type ```` ``` ````** — the third backtick auto-inserts a closing fence below; caret stays on the opening fence ready for a language tag. ★
- [ ] Off the active line, a fenced block renders as a code box (language tag + copy button).
- [ ] Code boxes have padded text, a bordered body, a language pill and clear separation above/below.
- [ ] Inline code has a rounded background, including when it wraps.
- [ ] Caret **inside** the block reveals BOTH the opening and closing fences. ★
- [ ] Copy button copies the block's code.
- [ ] **Selecting the whole block + something outside** shows the raw source (` ``` `), NOT both the rendered box and raw backticks at once. ★
- [ ] No hairline/overpainted seam under the code-box header. ★
- [ ] Lines inside a code block render verbatim (no bullets, rules, headings).

## 7. Math (built-in TeX subset)
- [ ] Inline `$x^2$` renders in place; caret/selection inside shows raw source.
- [ ] Inline formulas share the surrounding shaped-text baseline; tall fractions do not sag below adjacent text.
- [ ] Display `$$ … $$` renders; can span multiple lines.
- [ ] Bare dollars stay literal (`$5 and $12`).
- [ ] **Math inside inline code stays literal** — `` `$x^2$` `` shows the raw `$x^2$` as code, no formula painted (a bare `$x^2$` still renders). ◆
- [ ] Fractions `\frac`, roots `\sqrt`, sub/superscripts, big operators with limits (`\sum`, `\int`).
- [ ] Accents (`\hat`, `\vec`, `\bar`), `\left( … \right)`, matrices (`pmatrix`, `bmatrix`).
- [ ] A symbol command sample (Greek, arrows) renders.

## 7a. Images
- [ ] **Insert Image** (`Ctrl+Shift+I`, gear menu, or editor context menu)
      inserts selected images. A file outside the vault is copied to a unique
      path under `_attachments`; a file already inside the vault is referenced
      in place, and the inserted Markdown path is relative to the note.
- [ ] Pasting image data saves a PNG under `_attachments`; pasting or dropping
      one or more image files inserts each readable image and skips invalid files.
- [ ] A standalone local image keeps its aspect ratio and grows/shrinks with the editor viewport without exceeding the viewport-height cap.
- [ ] Moving the caret onto an image line reveals compact editable Markdown source; moving away restores the preview without an undo step.
- [ ] A missing image shows a bounded fallback card with its target instead of leaving a large blank area.

## 7b. HTML comments
- [ ] An inline or multi-line `<!-- private -->` comment remains visible as subdued, editable source in Edit Mode without changing the note.
- [ ] Inline comments leave the surrounding sentence joined naturally, and a `<!--` / `-->` block spanning several rows leaves no visible rows or gaps in Read Mode.
- [ ] Comment text is absent from vault search, spelling suggestions, Graph View, Quick Jump, Broken Links, and link rewrites after renaming a note. ◆
- [ ] `<!-- literal -->` inside inline code, fenced code, inline math, or display math remains literal rather than becoming a comment. ◆
- [ ] A note containing only comments is treated as empty; the first-line `<!-- mascot: … -->` metadata continues to hide/reveal and generate the same mascot as before.

## 8. Links & navigation
- [ ] `[[Note]]` renders, click jumps to the target.
- [ ] A wrapped link remains clickable only on its rendered text; blank space after it does nothing.
- [ ] `[[Note]]` to a non-existent note auto-creates it.
- [ ] With **Settings → New notes in** set to a sub-folder, clicking a
      non-existent `[[Note]]` creates it in that folder (not the vault root).
- [ ] `[[Note|alias]]` shows just the alias.
- [ ] Typing `[[` pops fuzzy autocomplete of existing titles; selecting inserts it.
- [ ] External link `[text](https://…)` renders, opens in the system browser.
- [ ] **Insert link** (`Ctrl+K`) — wraps selection as `[sel]()` (caret in parens) or inserts `[]()` (caret in brackets). ★
- [ ] **Back / Forward** (`Alt+←` / `Alt+→`), mouse side buttons, and sidebar arrows all navigate history.
- [ ] **Quick Jump** — hold `Alt` briefly and confirm every visible wiki/external link receives a hint in `QWERTYUIOPASDFGHJKLZXCVBNM` order. Keep holding `Alt`, type a hint, and confirm that link opens. With more than 26 visible links, confirm all hints become fixed-width two-key sequences. Releasing `Alt`, pressing Escape, or changing focus dismisses them. ◆
- [ ] **Broken Links** (`Ctrl+Shift+B`, gear menu, or **Settings → Vault → Broken links → Review…**) opens a search-style popup. Confirm missing targets and targets containing only whitespace are labelled correctly; populated targets and links inside inline/fenced code are absent. Type to filter, then press Enter or click a row and confirm its source note opens with the exact `[[link]]` selected. An issue-free vault shows “No broken links found”. ◆

## 9. Search
- [ ] **Find in note** (`Ctrl+F`).
- [ ] **Search vault** (`Ctrl+Shift+F`) — popup with ranked results; type to filter, ↑/↓ to move, Enter jumps to first match.

## 9a. Graph View
- [ ] **Open Graph View** (`Ctrl+Shift+G` or **Settings → Vault → Graph view → Open global**). It replaces the note in the central pane; no dialog, dock, or second top-level window appears. ◆
- [ ] **Open Local Graph** from **Settings → Vault → Graph view → Open local**. The dedicated Graph buttons are absent from both the sidebar footer and mobile toolbar.
- [ ] The **Graph** title, left-aligned search field, and graph controls occupy one header row. The visible note/link totals remain pinned to the bottom-right, including while a selected note's details appear at bottom-left.
- [ ] The graph canvas meets the sidebar's painted divider with no transparent gutter; after fully collapsing the sidebar, the graph likewise reaches the left divider/edge without a persistent gap. Returning to a note restores the wider resize target.
- [ ] The global graph includes every Markdown note, including orphans. Wiki-link aliases and heading fragments resolve to the target note; links in inline/fenced code do not appear. Repeated links form one stronger edge. ◆
- [ ] Pan empty space, zoom around the pointer, drag a node, press **F** to fit, and press **0** to reset the camera. After the layout settles, idle CPU use returns to normal.
- [ ] Hover/select a node to highlight its immediate connections and show incoming/outgoing counts. Double-click it, or select it and press Enter, to open the note.
- [ ] Type in graph search (`/` or `Ctrl+F`) and confirm non-matching nodes dim without causing a layout jump.
- [ ] Filter by top-level folder and toggle **Orphans**, **Missing**, and **Arrows**. Missing targets are hollow/dashed and never create a file when selected or double-clicked; filters remain isolated per vault.
- [ ] In Local mode, switch between **Both directions**, **Outgoing**, and **Incoming** and confirm neighborhood traversal follows the chosen edge direction.
- [ ] Switch to **Local**, choose depth 1–3, and confirm the open/selected note is the root and both incoming and outgoing neighbors appear.
- [ ] Navigate `Note A → Graph → Note B`, then Back twice. The first Back restores the same graph camera/search/filter/selection state; the second restores Note A. Forward walks the same sequence. ◆
- [ ] In Read Mode, Graph View remains available and navigable, while Save/Rename/Delete/Insert actions remain disabled.
- [ ] On a narrow/mobile window, Graph View still occupies the editor side, the filters collapse into a compact menu, tap selects, a second tap opens, drag pans, and pinch zooms.

## 10. Templates
- [ ] **Settings → Templates folder** points at a folder in the vault.
- [ ] **Insert Template…** (`Ctrl+T`) opens a picker of every note under it (sub-folders included). ★
- [ ] Chosen template drops in at the caret.
- [ ] Placeholders fill on insert: `{{date}}`, `{{time}}`, `{{title}}`.
- [ ] Formatted placeholders work: `{{date:YYYY/MM/DD}}`, `{{time:HH:mm:ss}}`.

## 11. Mascots
- [ ] A note past the char threshold (if auto-gen enabled in **Settings → Mascot**) grows a corner mascot.
- [ ] **Generate** (`Ctrl+M`) / **Delete** (`Ctrl+Shift+M`) mascot from the gear menu. ◆
- [ ] `Ctrl+M` is disabled with no note open, and re-rolls the creature on an open note. ◆
- [ ] `Ctrl+Shift+M` is disabled until the note has a mascot, then removes it (clears the seed line). ◆
- [ ] With auto-generation on (**Settings → Mascot**), deleting a note's mascot (`Ctrl+Shift+M`) stops it from auto-regenerating for that note even as you keep typing; a manual **Generate** (`Ctrl+M`) resumes auto-gen. ◆
- [ ] Hover gives a gentle blink/bob.
- [ ] Clicking a mascot **or pressing `Ctrl+G`** opens the vault-wide gallery; clicking a creature jumps to its note. ◆
- [ ] Press **↑** at the top of a note reveals the hidden seed line (`<!-- mascot: … -->`); editing/deleting it updates the creature.
- [ ] The seed line is invisible in a plain Markdown viewer (it's an HTML comment).
- [ ] **Use Image Mascots** (gear menu): with images in the mascots `images/` folder, a note with a mascot shows one of them as a rounded tile (in the corner and the gallery); the same note always maps to the same image. ◆
- [ ] Toggling **Use Image Mascots** off restores the procedural creature; with it on but the `images/` folder empty, mascots fall back to the procedural creature. ◆

## 12. Editor appearance & settings
- [ ] **Settings** (`Ctrl+,`) opens the dialog. ★
- [ ] Settings dialog shows shortcut labels next to actions where applicable. ★
- [ ] Change editor **font family + size** — applies and persists across restart.
- [ ] **Default font** is a system monospace face on a clean profile (no saved font override). ★
- [ ] **Line spacing** setting changes row spacing and survives note loads.
- [ ] **Vault settings stay isolated** — give two vaults different Home notes,
      new-note folders, templates folders, and Read Mode states; switching
      between them restores each vault's own choices without adding metadata
      files to either vault.
- [ ] **Read Mode** (`Ctrl+E` or **Settings → Vault → Read mode**) removes the
      caret and renders the current line like the rest of the note. Plain ↑/↓
      smoothly scroll the page by one visual line without moving a text cursor. ◆
- [ ] On a long list with several wrapped and nested items, scroll to the middle
      and rapidly toggle Read/Edit Mode at least eight times. The same source
      line remains at the same viewport height and the page does not accumulate
      upward or downward drift. ◆
- [ ] Read Mode strips a first-line callout marker, shows its generated or
      custom title with the type's unique emoji, propagates its background over
      the complete quote group without row gaps, and aligns the card left.
- [ ] Select plain text in Read Mode and press `Ctrl+Shift+H`; Emerald inserts
      `==` in the Markdown source, keeps the rendered text selected, and
      autosaves. Select only highlighted words and press it again to remove the
      selected highlight. If even one selected word is not highlighted, the
      shortcut fills every gap instead. Multi-line list/quote selections retain
      their structural prefixes. ◆
- [ ] **Smooth mouse-wheel scrolling** — each ordinary wheel notch eases to its
      target without visible line-sized jumps. Spin several notches quickly and
      confirm they accumulate into one continuous movement rather than restarting
      from the original position. ◆
- [ ] **Trackpad scrolling** — slow two-finger movement follows the fingers at
      pixel precision, and native momentum continues naturally after release;
      there should be no extra easing or delayed tail added by Emerald. Test on
      at least one Linux setup and macOS if available. ◆
- [ ] Start a smooth wheel movement, then click in the note or drag the scrollbar.
      The animation stops immediately and does not pull the page away afterward.
- [ ] While Read Mode is on, typing, title edits, templates, images, mascots,
      new/renamed/moved/deleted notes and folders, and clicking a missing
      `[[link]]` cannot change the vault. `Ctrl+Shift+H` highlights and task
      checkbox toggles are the deliberate exceptions and autosave; links,
      search, folding, selection/copy, and externally updated notes continue to
      work. ◆
- [ ] **Font size** `Ctrl++` / `Ctrl+-` / `Ctrl+0` (up / down / reset).
- [ ] **Toggle sidebar** (`Ctrl+\` or the gear menu) collapses the left pane fully and restores it; clicking the splitter handle does the same. ◆
- [ ] Heading sizes scale with the body font.
- [ ] Dark theme renders correctly (embedded QSS).

## 13. Folding
- [ ] Fold control on a heading collapses everything down to the next same/higher heading.
- [ ] Fold control on a list parent collapses its indented child tree without
      hiding the parent's next sibling; its state survives switching between
      Edit and Read Mode. ◆
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
- [ ] An update is opened/installed only after its SHA-256 digest and byte size
      match GitHub's release metadata; missing or mismatched metadata/file data
      produces a verification error and removes the download.
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
| Insert image attachment            | `Ctrl+Shift+I`                 | [ ] |
| Save now                           | `Ctrl+S`                       | [ ] |
| Rename note                        | `F2`                           | [ ] |
| Delete note                        | `Ctrl+Shift+Backspace`         | [ ] |
| Find in note                       | `Ctrl+F`                       | [ ] |
| Search vault                       | `Ctrl+Shift+F`                 | [ ] |
| Review broken links                | `Ctrl+Shift+B`                 | [ ] |
| Open Graph View                    | `Ctrl+Shift+G`                 | [ ] |
| Toggle Read Mode                   | `Ctrl+E`                       | [ ] |
| Toggle selected Read Mode highlight | `Ctrl+Shift+H`                | [ ] |
| Settings                           | `Ctrl+,`                       | [ ] |
| Back / Forward                     | `Alt+←` / `Alt+→`              | [ ] |
| Quick Jump to visible link         | Hold `Alt`, then type hint     | [ ] |
| Shortcut cheatsheet                | Hold `Alt+X`; release to close | [ ] |
| Bold / Italic                      | `Ctrl+B` / `Ctrl+I`            | [ ] |
| Insert link                        | `Ctrl+K`                       | [ ] |
| Heading 1–6 (press again to clear) | `Ctrl+1` … `Ctrl+6`            | [ ] |
| Select line                        | `Ctrl+L`                       | [ ] |
| Move line up / down                | `Alt+↑` / `Alt+↓`              | [ ] |
| Indent / outdent                   | `Tab` / `Shift+Tab`            | [ ] |
| Font size up / down / reset        | `Ctrl++` / `Ctrl+-` / `Ctrl+0` | [ ] |
| Toggle sidebar                     | `Ctrl+\`                       | [ ] |
| Mascot gallery                     | `Ctrl+G`                       | [ ] |
| Generate mascot                    | `Ctrl+M`                       | [ ] |
| Delete mascot                      | `Ctrl+Shift+M`                 | [ ] |
| Quit                               | `Ctrl+Q`                       | [ ] |

> On macOS use **⌘** where **Ctrl** is listed.

## 17. Platform-specific launch
- [ ] **Linux x86-64**: `chmod +x Emerald-x86_64.AppImage` then run.
- [ ] **Linux ARM64**: runs on a glibc ≥ 2.39 system.
- [ ] **macOS**: first launch **right-click → Open → Open**; if still blocked, `xattr -cr /Applications/Emerald.app`.
- [ ] **Windows**: install `Emerald-win64-setup.exe`; Emerald appears under **Open with** and Default Apps for `.md`/`.markdown`. The portable ZIP still runs without registration.
- [ ] Each package runs with no separate Qt install (bundled runtime).

## 18. Standalone Markdown files
- [ ] With a vault open, double-click a Markdown file elsewhere. It opens in a separate Emerald window without replacing the vault window.
- [ ] Right-click an `.md` and choose **Open with → Emerald**. Test `.markdown` too.
- [ ] Run `emerald /absolute/path/to/file.md`; the requested file opens instead of the last vault.
- [ ] The standalone sidebar contains only that file. Neighboring notes never appear in Quick Open, search, graph, or wiki-link completion.
- [ ] Edit and save a UTF-8 CRLF file (and one with a UTF-8 BOM); its original newline/BOM convention is preserved.
- [ ] Rename from the title field. Only the open file moves, retaining its `.md` or `.markdown` suffix.
- [ ] Relative images already referenced by the document render. Insert Image, pasted-image attachment creation, templates, global search, graphs, new/delete note, and mascot management are disabled.
- [ ] Change the file in another editor; Emerald reloads it when clean and preserves/warns about local edits when dirty.
- [ ] Close the standalone window and relaunch Emerald normally; the last closed vault still opens.
- [ ] **Linux AppImage:** choose **Integrate with Linux Desktop…**, then confirm Emerald appears in the application menu and Markdown **Open with** list.
