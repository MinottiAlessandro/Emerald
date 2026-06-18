## Emerald 1.2.1

A small fix-up release: line spacing actually works now, plus a handy
wrap-the-selection shortcut.

### Fixes
- **Line spacing now takes effect.** The Settings **Line spacing** control did
  nothing because `QPlainTextEdit` ignores block line-height; the editor now
  drives row spacing through its document layout, so 100–250% really opens up
  the gap between rows (and the choice survives switching notes).

### New
- **Wrap the selection.** Select some text and press **(** **[** **\*** **_**
  **=** **'** **"** **`** or **~** to surround it with that character —
  brackets and parens close with their match, the rest pair with themselves.
  The wrapped text stays selected so you can wrap it again.

### Downloads
| Platform | File | Notes |
|---|---|---|
| Linux (x86-64) | `Emerald-x86_64.AppImage` | `chmod +x` and run |
| Linux (ARM64)  | `Emerald-aarch64.AppImage` | `chmod +x` and run · glibc ≥ 2.39 |
| macOS (universal) | `Emerald-macOS.dmg` | drag to Applications; first launch **right-click → Open → Open**. If still blocked: `xattr -cr /Applications/emerald.app` |
| Windows | `Emerald-win64.zip` | extract and run `emerald.exe` |

Every download bundles its own Qt runtime — nothing else to install. Built
automatically by the `Release` GitHub Actions workflow.
