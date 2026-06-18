## Emerald 1.2.0

A feature release: self-updating, multi-line indenting, adjustable line
spacing, and a clean-up to heading folding.

### New
- **Check for Updates.** A new **Check for Updates…** entry in the gear menu
  asks GitHub for the latest release, and — on Linux AppImage — downloads it,
  swaps it in place, and offers to restart. macOS and Windows open the new
  download. The current version now shows in the bottom-right, by the gear.
- **Indent a block with Tab.** Select several lines and press **Tab** /
  **Shift+Tab** to indent / outdent them all at once (two spaces a level); a
  single list item still indents the way it always did.
- **Adjustable line spacing.** Settings gains a **Line spacing** control
  (100–250%) to open up the gap between rows, with a live preview.
- **Ctrl+Enter — new line, no split.** Start a fresh line below without
  splitting the current one. Inside a list it keeps continuing the list (and
  clears an empty bullet), so it behaves like a normal Enter that never breaks
  the line you're on.

### Fixes
- **No more doubled fold markers.** Collapsing a child heading and then its
  parent used to paint two fold arrows and two `…` next to the parent title.
  A heading hidden inside an enclosing fold no longer draws its own marker.

### Downloads
| Platform | File | Notes |
|---|---|---|
| Linux (x86-64) | `Emerald-x86_64.AppImage` | `chmod +x` and run |
| Linux (ARM64)  | `Emerald-aarch64.AppImage` | `chmod +x` and run · glibc ≥ 2.39 |
| macOS (universal) | `Emerald-macOS.dmg` | drag to Applications; first launch **right-click → Open → Open**. If still blocked: `xattr -cr /Applications/emerald.app` |
| Windows | `Emerald-win64.zip` | extract and run `emerald.exe` |

Every download bundles its own Qt runtime — nothing else to install. Built
automatically by the `Release` GitHub Actions workflow.
