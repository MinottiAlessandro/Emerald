## Emerald 1.3.0

A feature release: every note can now have its own little **mascot**.

### New
- **Per-note mascots.** Each note can carry a small, procedurally generated
  creature in the bottom-right corner, drawn entirely with the renderer (no
  images, no new dependencies) from a seed mixed out of the note's title and
  text — so it gives the note a memorable visual identity. Around three dozen
  archetypes span ordinary animals (cat, fox, bear, bunny, owl, penguin, fish,
  frog…), mythological creatures (dragon, unicorn, phoenix, angel, demon, ghost,
  fairy, griffin, cyclops…) and objects (robot, mushroom, potion, star, crystal,
  snowman, cactus…). Hover one to see it gently bob and blink.
- **Mascot gallery.** Click a mascot — or open it from the gear menu — to see
  every mascot in the vault laid out in a grid; click one to jump straight to
  its note. (The gallery is a transient view, nothing is saved.)
- **Manual or automatic.** Use **Generate Mascot** / **Delete Mascot** in the
  gear menu (delete is sticky — it won't reappear on its own). Auto-generation
  once a note passes a character count is available under **Settings → Mascot**
  and is **off by default**. Mascots are stored per-vault in
  `.emerald/mascots.json`, so they travel with your notes.
- **Per-line formatting for multi-line selections.** Select several lines and
  press Ctrl+B / Ctrl+I — or type a pairing character (`*` `(` `_` `=` `[` `'`
  `"` `` ` `` `~`) — and each line is formatted on its own: a fully selected
  line is wrapped end to end, a partially selected one only over the selected
  span. Press the bold/italic shortcut again to toggle it back off.

### Fixes
- **Smoother edge scrolling.** When the caret reaches the top or bottom of the
  viewport the editor now scrolls one line at a time instead of jumping.

### Downloads
| Platform | File | Notes |
|---|---|---|
| Linux (x86-64) | `Emerald-x86_64.AppImage` | `chmod +x` and run |
| Linux (ARM64)  | `Emerald-aarch64.AppImage` | `chmod +x` and run · glibc ≥ 2.39 |
| macOS (universal) | `Emerald-macOS.dmg` | drag to Applications; first launch **right-click → Open → Open**. If still blocked: `xattr -cr /Applications/emerald.app` |
| Windows | `Emerald-win64.zip` | extract and run `emerald.exe` |

Every download bundles its own Qt runtime — nothing else to install. Built
automatically by the `Release` GitHub Actions workflow.
