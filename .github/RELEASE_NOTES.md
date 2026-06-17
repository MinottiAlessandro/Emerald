## Emerald 1.1.0

A feature release: ARM64 Linux builds, clickable web links, and a cleaner icon.

### New
- **ARM64 Linux builds.** A native `Emerald-aarch64.AppImage` joins the x86-64
  one, so Emerald runs on Apple Silicon Linux, modern Raspberry Pi / SBC desktops
  and other aarch64 machines (needs glibc ≥ 2.39).
- **Clickable internet links.** Standard Markdown `[text](https://…)` links now
  render inline like wiki-links — the `](url)` melts away leaving just the text,
  which opens in your browser on click (Ctrl+click on the line you're editing).
- **Universal macOS app.** The `.dmg` is a single universal binary for both
  Apple Silicon and Intel.

### Polish
- **Refined app icon** — the white corners are gone (transparent now), so the
  icon looks right on dark backgrounds and in every OS's icon shelf.
- **Stable download links.** Release artifacts now have version-less names
  (`Emerald-macOS.dmg`, `Emerald-x86_64.AppImage`, …) served from
  `releases/latest`, so the README links always point at the newest build.

### Downloads
| Platform | File | Notes |
|---|---|---|
| Linux (x86-64) | `Emerald-x86_64.AppImage` | `chmod +x` and run |
| Linux (ARM64)  | `Emerald-aarch64.AppImage` | `chmod +x` and run · glibc ≥ 2.39 |
| macOS (universal) | `Emerald-macOS.dmg` | drag to Applications; first launch **right-click → Open → Open**. If still blocked: `xattr -cr /Applications/emerald.app` |
| Windows | `Emerald-win64.zip` | extract and run `emerald.exe` |

Every download bundles its own Qt runtime — nothing else to install. Built
automatically by the `Release` GitHub Actions workflow.
