## Emerald 1.2.2

A fix-up release for heading folding.

### Fixes
- **Enter at the start of a collapsed heading keeps it collapsed.** Pressing
  Enter at the very start of a folded heading now inserts the blank line above
  without popping the section open.
- **Steady cursor on the blank lines under a fold.** Increasing the line
  spacing left a collapsed section occupying a sliver of space, which made the
  caret on the blank lines below it render only half-drawn. Folded-away lines
  now take no room, so the caret shows in full.
- **Typing under a fold no longer swallows your text.** Typing on the visible
  blank lines beneath a collapsed heading used to pull the new text into the
  fold and hide it. A fold now keeps the extent it had when you collapsed it;
  the typed text stays put, and the section only re-measures when you collapse
  it again.

### Downloads
| Platform | File | Notes |
|---|---|---|
| Linux (x86-64) | `Emerald-x86_64.AppImage` | `chmod +x` and run |
| Linux (ARM64)  | `Emerald-aarch64.AppImage` | `chmod +x` and run · glibc ≥ 2.39 |
| macOS (universal) | `Emerald-macOS.dmg` | drag to Applications; first launch **right-click → Open → Open**. If still blocked: `xattr -cr /Applications/emerald.app` |
| Windows | `Emerald-win64.zip` | extract and run `emerald.exe` |

Every download bundles its own Qt runtime — nothing else to install. Built
automatically by the `Release` GitHub Actions workflow.
