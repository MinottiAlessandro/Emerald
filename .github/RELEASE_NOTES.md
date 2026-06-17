## Emerald 1.0.1

Maintenance release that makes the **macOS build actually launch**. The 1.0.0
`.dmg` shipped a bundle whose main executable had no `LC_RPATH`, so macOS aborted
at startup with *"Library not loaded: @rpath/QtWidgets.framework… no LC_RPATH's
found."* If you grabbed 1.0.0 on a Mac, replace it with this build.

### Fixes
- **macOS: app launches correctly.** The bundle now carries an
  `@executable_path/../Frameworks` rpath, so it locates its bundled Qt
  frameworks instead of crashing at startup.
- **macOS: universal binary.** The `.dmg` now runs natively on both Apple
  Silicon and Intel Macs.
- **macOS: ad-hoc signed** so Gatekeeper no longer reports the app as "damaged".

Linux and Windows are functionally unchanged from 1.0.0 (rebuilt for parity).

### What Emerald is
A minimal, fast, Obsidian-like Markdown note editor built with Qt 6: live inline
preview, wiki-style `[[links]]` with rename-aware backlinks, fast full-text
search, a folder-tree sidebar, debounced autosave and external-edit watching —
all over plain `.md` files, on Linux, macOS and Windows.

### Downloads
- **Linux** — `Emerald-1.0.1-x86_64.AppImage` (portable; `chmod +x` and run).
- **macOS** — `Emerald-1.0.1-Darwin.dmg` (universal; drag to Applications).
  Ad-hoc signed, not notarized: on first launch **right-click the app → Open →
  Open**. If macOS still blocks it, run `xattr -cr /Applications/Emerald.app` in
  Terminal.
- **Windows** — `Emerald-1.0.1-win64.zip` (extract and run `emerald.exe`).

Built automatically by the `Release` GitHub Actions workflow.
