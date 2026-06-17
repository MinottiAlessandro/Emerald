## Emerald 1.1.1

A small fix-up release polishing links, navigation and folding.

### Fixes
- **Link click area stays on the text.** Clicking in the empty space past the
  end of a line ending in a `[[link]]` or `[text](url)` no longer follows the
  link — only the link text itself is clickable.
- **Alt / Option + ← / → navigate back / forward on macOS.** The Option+Arrow
  history shortcut used to be swallowed by the editor's word navigation on
  macOS; it now works on every platform.

### Changed
- **Tidier heading folding.** Collapsing a heading now leaves the blank line(s)
  before the next heading visible, so collapsed sections keep their spacing. A
  heading with nothing but blank lines under it is no longer foldable.

### Downloads
| Platform | File | Notes |
|---|---|---|
| Linux (x86-64) | `Emerald-x86_64.AppImage` | `chmod +x` and run |
| Linux (ARM64)  | `Emerald-aarch64.AppImage` | `chmod +x` and run · glibc ≥ 2.39 |
| macOS (universal) | `Emerald-macOS.dmg` | drag to Applications; first launch **right-click → Open → Open**. If still blocked: `xattr -cr /Applications/emerald.app` |
| Windows | `Emerald-win64.zip` | extract and run `emerald.exe` |

Every download bundles its own Qt runtime — nothing else to install. Built
automatically by the `Release` GitHub Actions workflow.
