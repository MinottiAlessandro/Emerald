# Emerald Mascot — Art Asset Specification

> The brief an artist draws against, and the contract the rendering engine
> implements. Target: **Emerald 1.5.0**. Status: **draft for commission**.

Each Emerald note grows a small creature in the bottom-right corner, seeded from
the note's title + text. Today every creature is drawn procedurally with
`QPainter` primitives (`src/ui/Mascot.cpp`). 1.5.0 keeps that system's spine and
**reskins its leaves** with hand-drawn art: the seed → traits → layered
composite stays identical; only the individual *parts* become SVG drawings.

This means **you draw parts, not whole creatures.** The app already knows how to
combine a body + ears + eyes + mouth + tail + extras into ~34 recognisable
archetypes; your art slots into that machine. A finished "cat" emerges from
`body=round` + `topper=cat-ears` + `mouth=cat` + `tail=cat` + whiskers, each
drawn once and reused across every archetype that calls for it.

---

## 1. Canvas & coordinate system

- **Logical canvas: `100 × 110` units** (width × height). All art and all
  anchors live in this space. The app scales it to the widget (132×152 live,
  120×132 in the gallery, ×2/×3 on HiDPI), so vector stays crisp at any size.
- **Origin top-left, y increases downward** (standard SVG / screen convention).
- **`viewBox="0 0 100 110"`** on every SVG. No fixed `width`/`height` — the app
  sizes it.
- Reference anchors the current creatures are built around (use as guides):
  - **Horizontal centre: `x = 50`.**
  - **Ground line (feet / bottom of body): `y = 96`.**
  - Keep a **~5-unit margin** on all sides; nothing should touch the viewBox edge
    (wings and toppers are the widest/tallest parts — give them room).

A drawing template (`docs/mascot-template.svg`, to be added) will ship a 100×110
guide canvas with the centre line, ground line, margin box, and the default
mount points marked, so parts are authored against a shared rig.

---

## 2. File format

- **SVG**, one file per part, rendered via `QSvgRenderer` (Qt6::Svg — already a
  Qt first-party module, so it stays within Emerald's one-dependency rule).
- Keep paths simple and **flat / geometric** to match Emerald's UI (flat dark
  theme, the emerald-green accent `#2bbf74`). No gradients heavier than a single
  soft shade layer; no embedded raster; no external font references (convert any
  text to paths).
- Each part SVG is structured as **two tagged groups** so the app can recolor it:

  ```xml
  <svg viewBox="0 0 100 110" xmlns="http://www.w3.org/2000/svg">
    <g id="tint">   <!-- flat fills the app recolors per palette slot -->
      <path fill="#ffffff" data-slot="body" d="…"/>
      <path fill="#ffffff" data-slot="belly" d="…"/>
    </g>
    <g id="shade">  <!-- fixed shading / outline, composited on top, NOT recolored -->
      <path fill="#00000022" d="…"/>   <!-- soft shadow -->
      <path fill="none" stroke="#000" stroke-width="1.6" d="…"/> <!-- outline -->
    </g>
  </svg>
  ```

  - Everything inside **`#tint`** is a flat solid fill the app replaces with a
    palette colour (see §5). Author these in **plain white `#ffffff`** and mark
    each with `data-slot="…"` naming which palette slot it takes
    (`body`/`edge`/`belly`/`accent`/`dark`/`white`).
  - Everything inside **`#shade`** is drawn as-authored on top (shadows,
    highlights, the dark outline, eye glints). This is where the hand-drawn
    character lives. Use translucent black/white so it reads on any tint.
  - This flat-base + shading-overlay split is what keeps recoloring clean — a
    naive hue-rotate over fully-shaded art goes muddy.

Fallback if SVG proves impractical for a part: transparent PNG at **@3x**
(300×330) is accepted, but SVG is strongly preferred (smaller, crisper, tintable).

---

## 3. The parts to draw (the catalog)

Draw each list below as an independent part on the shared canvas, mounted at the
anchor named in §4. Names are the **asset filenames** (kebab-case, no extension).
This roster mirrors the current procedural catalog exactly — append-only from
here (see §6), never reorder or rename.

### 3a. Bodies — `art/body/<name>.svg` (14)
The base silhouette. Carries its own anchor record (§4). `round` is the default.

`round` · `tall` · `wide` · `slime` · `ghost` · `bottle` · `star` · `gem` ·
`mushroom` · `robot` · `octo` · `jelly` · `cactus` · `snowman`

> The 34 archetypes are *recipes* over these bodies + the overlays below (e.g.
> a **fox** = `wide`/`round` body + `cat-ears` + `bushy` tail + warm palette).
> You do **not** draw 34 bodies — you draw 14, and they recombine. A short
> reference table of all 34 recipes is in Appendix A so you can sanity-check that
> each archetype reads correctly once composed.

### 3b. Toppers (head) — `art/topper/<name>.svg` (17 + none)
Mount at the body's `topper` anchor.

`cat-ears` · `bear-ears` · `bunny-ears` · `mouse-ears` · `horns-small` ·
`horns-curved` · `unicorn` · `antlers` · `halo` · `crown` · `wizard-hat` ·
`antennae` · `leaf` · `flame` · `tuft` · `top-hat` · `spikes`

### 3c. Eyes — `art/eyes/<name>.svg` (9)
Drawn as a **pair** spanning the `face` anchor at half-gap `eyeGap` (§4); the
single-eye archetype is `cyclops` (one centred eye). Provide a 2-frame **blink**
(open + closed) per eye set — closed can be a simple arc.

`dot` · `sparkle` · `sleepy` · `oval` · `happy` · `angry` · `cyclops` · `star` ·
`wink`

### 3d. Mouths — `art/mouth/<name>.svg` (7)
Mount just below the `face` anchor.

`smile` · `cat` · `open` · `fang` · `beak` · `grin` · `flat`

### 3e. Tails — `art/tail/<name>.svg` (6 + none)
Mount at the `tail` anchor (body's lower side). Drawn for the right side; the app
may mirror.

`cat` · `bushy` · `devil` · `fish` · `thin` · `fluffy`

### 3f. Backs / wings — `art/back/<name>.svg` (4 + none)
Mount at the `back` anchor; drawn **behind** the body (lowest layer).

`bat-wings` · `feather-wings` · `fairy-wings` · `cape`

### 3g. Patterns — `art/pattern/<name>.svg` (3 + none)
A tile/overlay clipped to the body silhouette (the app clips it to the body
path). Author on the full 100×110 so it can be masked.

`spots` · `stripes` · `scales`

### 3h. Extras — `art/extra/<name>.svg`
Small accents the recipes toggle on. Mount points in §4 where not obvious.

`cheeks` (two blush ovals at face sides) · `whiskers` · `sparkles` (3 twinkles
around the creature) · `gem` (a held/embedded jewel) · `carrot` (snowman nose,
at `face`) · `feet` (two at `ground`) · `belly` (a lighter front oval, drawn
over the body before pattern).

---

## 4. The anchor contract (most important section)

Hand-drawn bodies won't match the procedural geometry, so **anchors move from
code to data.** Each **body** ships an anchor record telling the app where every
overlay mounts on *that* body. Overlays are drawn at their slot's anchor with no
per-overlay geometry — so a single `cat-ears.svg` sits correctly on every body.

Anchors are points (and a couple of scalars) in the 100×110 canvas:

| Anchor    | Meaning                                                            |
|-----------|-------------------------------------------------------------------|
| `face`    | centre of the eye/mouth cluster `(x, y)`                          |
| `eyeGap`  | half the distance between the two eyes (scalar)                   |
| `topper`  | where ears/hats/horns mount `(x, y)` (their bottom-centre)        |
| `tail`    | where the tail attaches `(x, y)` (body's lower-right)             |
| `back`    | centre of the wings/cape behind the body `(x, y)`                 |
| `ground`  | feet baseline `(x, y)` — usually `(50, 96)`                       |

These ship as one JSON manifest the app reads once at startup,
`art/anchors.json`:

```json
{
  "version": 2,
  "bodies": {
    "round":   { "face": [50, 50], "eyeGap": 11, "topper": [50, 22],
                 "tail": [72, 70], "back": [50, 46], "ground": [50, 96] },
    "tall":    { "face": [50, 44], "eyeGap": 9,  "topper": [50, 16], "…": "…" },
    "ghost":   { "face": [50, 46], "eyeGap": 11, "topper": [50, 18], "…": "…" }
  }
}
```

For the artist: the `tall`/`ghost` numbers above are placeholders. Draw the body,
then read the mount points off your drawing and fill them in (the template's
gridlines make this a glance). One record per body in §3a.

### How overlays are placed (the canonical anchors)

Author every **overlay** (topper, eyes, mouth, tail, back, pattern, extras) on
the full 100×110 canvas positioned to sit correctly on the **default `round`
body**, whose mounts are the **canonical anchors**:

| Slot    | Canonical point |
|---------|-----------------|
| `face`  | `(50, 50)`      |
| `topper`| `(50, 22)`      |
| `tail`  | `(72, 70)`      |
| `back`  | `(50, 46)`      |
| `ground`| `(50, 96)`      |

To place an overlay on any other body, the app shifts it by
`bodyAnchor[slot] − canonicalAnchor[slot]`. So `round`'s anchor record should
equal the canonical points above (zero shift — its overlays sit exactly as
drawn), and every other body just needs honest mount points for the app to nudge
the shared overlay into place. Practical consequences for authoring:

- **Eyes and mouth both mount at the `face` anchor** — draw the eye *pair* and
  the mouth on the canvas around `(50, 50)`; the app moves the cluster as a unit
  per body. (`eyeGap` is recorded for future per-eye spacing but isn't applied
  yet, so author the pair at the gap that looks right on `round`.)
- **The body art includes its own belly/underside** (it isn't a separate part);
  only the markings that vary by archetype — `pattern` — are a separate overlay,
  drawn full-canvas over the body (author it to sit within the silhouette; the
  app does not auto-clip it yet).
- **Tails are not auto-mirrored yet** — draw the tail on the side its anchor sits.
- **Extras mount at:** `cheeks`/`whiskers`/`carrot`/`gem` → `face`,
  `sparkles` → `back`, `feet` → `ground`.
- **Blink:** ship the closed-eye frame as `art/eyes/<name>-closed.svg`; the app
  swaps it in on hover and falls back to the open eyes if it's absent.

---

## 5. Recoloring (palette slots)

Emerald derives a 6-slot palette per creature from a base **hue** + a **mode**
(`normal`, `fiery`, `ghostly`, `metallic`, `pastel`). The app fills each
`#tint` path's color from the slot named in its `data-slot`:

| Slot    | Role                                          |
|---------|-----------------------------------------------|
| `body`  | main fill                                     |
| `edge`  | darker rim / outline tint                     |
| `belly` | lighter front / underside                     |
| `accent`| complementary pop (gems, cheeks, highlights)  |
| `dark`  | near-black structural shapes                  |
| `white` | eye whites / glints (ghostly mode tints it)   |

So: author `#tint` shapes in flat white tagged with the slot they should take;
the `#shade` group is drawn unchanged on top. The same part then works across
all five palette modes and every hue with no re-drawing.

---

## 6. Determinism & versioning (read before adding anything)

The seed lives **inside the note** as `<!-- mascot: 13845229104471 -->` and must
reproduce the same creature forever. Two rules keep that promise:

1. **The catalog is append-only.** Never reorder or rename a part within a slot;
   only add new parts/archetypes at the **end** of a list. The procedural code
   picks parts by index off one RNG stream, so reordering silently changes every
   existing user's creature.
2. **Growing a slot is versioned.** Because `index % count` shifts when a count
   grows, any catalog *expansion* (new archetype, new overlay) bumps a catalog
   version, and stored seeds may carry it: `<!-- mascot: N v2 -->` (absent ⇒
   `v1`). The app keeps each version's selection mapping so a v1 note always
   renders its v1 creature.

For the **initial migration this is free**: we keep the existing `rollTraits`
recipes byte-for-byte and only swap *rendering* (procedural → SVG). Same seed ⇒
same trait composition ⇒ the same creature, now hand-drawn. Versioning only
becomes relevant the first time we *add* art beyond the current 34.

---

## 7. Z-order (back to front)

The app composites parts in this fixed order — draw each part to read correctly
in this stack:

```
back (wings/cape) → tail → body → belly → pattern → topper → face (eyes+mouth) → extras
```

---

## 8. Delivery — one archetype at a time

The engine renders SVG when the asset exists and **falls back to the current
procedural part** when it doesn't, so art can land incrementally with no flag
day. A natural delivery unit is **one archetype fully composed** — e.g. "cat":
`body/round` + `topper/cat-ears` + `eyes/{dot,sparkle}` + `mouth/cat` +
`tail/cat` + `extra/whiskers` + `extra/cheeks`, plus the `round` anchor record.
Ship that, and cats render hand-drawn while everything else stays procedural
until its parts arrive.

**Definition of done for a part:** SVG with `viewBox="0 0 100 110"`, a `#tint`
group (white fills tagged `data-slot`) and a `#shade` group, renders cleanly at
24px and 300px, and (for bodies) an entry in `art/anchors.json`.

---

## Appendix A — the 34 archetype recipes

Each archetype selects a body + overlays; some traits are randomised per-seed
(shown as `a|b`). This is reference only — you draw the *parts*, not these.

| Archetype | body     | topper       | eyes      | mouth   | tail   | back        | extras / palette                |                        |
| --------- | -------- | ------------ | --------- | ------- | ------ | ----------- | ------------------------------- | ---------------------- |
| cat       | round    | cat-ears     | dot\      | sparkle | cat    | cat         | –                               | whiskers, cheeks?      |
| fox       | wide\    | round        | cat-ears  | dot     | smile  | bushy       | –                               | warm hue, cheeks?      |
| bear      | wide\    | round        | bear-ears | dot     | smile  | –           | –                               | brown hue?             |
| bunny     | round    | bunny-ears   | dot\      | sparkle | smile  | fluffy      | –                               | cheeks                 |
| mouse     | round    | mouse-ears   | dot       | smile   | thin   | –           | metallic, cheeks?               |                        |
| frog      | wide     | sparkle      | –         | grin    | –      | –           | green hue                       |                        |
| owl       | round    | tuft         | sparkle   | beak    | –      | feather?    | spots, no feet                  |                        |
| penguin   | tall     | –            | dot       | beak    | –      | –           | blue hue                        |                        |
| fish      | wide     | –            | sparkle   | open    | fish   | –           | scales, no feet                 |                        |
| deer      | round    | antlers      | dot\      | sleepy  | –      | fluffy      | –                               | tan hue, cheeks?       |
| dragon    | round    | horns-small\ | curved    | dot     | fang   | fish        | bat-wings                       | scales, gem?           |
| unicorn   | round    | unicorn      | sparkle   | smile   | fluffy | –           | pastel, gem, sparkles           |                        |
| demon     | round    | horns-curved | angry     | fang    | devil  | –           | red hue                         |                        |
| angel     | round    | halo         | happy     | smile   | –      | feather     | pastel, cheeks                  |                        |
| phoenix   | round    | flame        | sparkle   | beak    | fish   | feather     | fiery                           |                        |
| ghost     | ghost    | –            | sleepy\   | oval    | open   | –           | –                               | ghostly, no feet/belly |
| slime     | slime    | antennae?    | dot       | –       | –      | –           | no feet/belly                   |                        |
| golem     | gem\     | round        | –         | dot     | smile  | –           | –                               | metallic, gem          |
| fairy     | round    | antennae?    | sparkle   | smile   | –      | fairy-wings | pastel, sparkles, cheeks        |                        |
| griffin   | round    | tuft         | dot       | beak    | cat    | feather     | tan hue, no feet                |                        |
| cyclops   | round    | horns-small  | cyclops   | fang    | –      | –           | green hue                       |                        |
| robot     | robot    | antennae     | oval      | flat    | –      | –           | metallic                        |                        |
| mushroom  | mushroom | –            | dot       | smile   | –      | –           | spots, cheeks?, no feet/belly   |                        |
| potion    | bottle   | –            | dot       | smile   | –      | –           | sparkles?, no feet/belly        |                        |
| star      | star     | –            | happy     | –       | –      | –           | yellow, sparkles, no feet/belly |                        |
| crystal   | gem      | –            | sparkle   | –       | –      | –           | metallic, gem, sparkles         |                        |
| dino      | tall\    | round        | spikes    | dot     | fang   | fish        | –                               | green hue              |
| octopus   | octo     | –            | sparkle   | –       | –      | –           | purple hue, no feet/belly       |                        |
| jellyfish | jelly    | –            | happy\    | dot     | –      | –           | –                               | ghostly, sparkles?     |
| snowman   | snowman  | top-hat      | dot       | flat    | –      | –           | carrot, no feet/belly           |                        |
| cactus    | cactus   | –            | happy\    | dot     | –      | –           | –                               | green, cheeks?         |
| ghost-cat | ghost    | cat-ears     | sleepy    | cat     | –      | –           | whiskers, ghostly               |                        |
| bat       | round    | cat-ears     | dot       | fang    | –      | bat-wings   | purple hue                      |                        |
| bee       | round\   | wide         | antennae  | dot     | smile  | –           | fairy-wings                     | stripes, yellow        |
