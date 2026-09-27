# Depth: Darkest Dungeon-style concept art

Standalone concept-art gallery: every crew member, foe, boss, room and level in
Depth, drawn as hand-inked plates in the style of Darkest Dungeon (heavy black
ink outlines, hard block shadows, eyes lost under the brow, a muted maritime
palette, vignette and grain).

This is **reference art only** — it does not affect the game's actual
rendering (`src/render.cpp`, `src/dungeon.cpp`, etc.) and none of the game's
own code was changed to produce it. Every illustration here is drawn with
plain Canvas 2D, independent of the game's raylib pipeline.

## Viewing it

Open `index.html` in a browser (it pulls in `lib.js`, `people.js`,
`people2.js`, `beasts.js` and `scenes.js` from this same folder — no build
step, no server required beyond a plain static file open).

## Contents

- **`lib.js`** — the ink-and-shadow drawing toolkit: shape builders (ellipses,
  polygons, tapered lines, bezier "spine" strips for tentacles/tails/worms),
  the `Ink` class that fills, hatches and outlines every shape with a
  consistent light direction, plus vignette/grain post-processing.
- **`people.js`** — the 12 playable crew classes (Nurse, Diver, Captain,
  Mechanic, Whaler, Stowaway, Merman, Dethroned Queen, Automaton, Octopus,
  Siren, Wisp).
- **`people2.js`** — the Nautilus's salon NPCs and cat, the Flats card dealer,
  and every humanoid (or once-human) foe across all four Shallows locations
  and the platform levels, including Blackbeard.
- **`beasts.js`** — every non-humanoid creature: crustaceans, worms, eels,
  the shark, the octopi, and the location bosses (Crustacean Queen, Sun God,
  Neptune, Cthulhu, the Kraken).
- **`scenes.js`** — full scene compositions: the salon hub, the card table,
  each combat location's battle line-up and boss chamber, and the three
  platform levels (Pipes, Hull + Kraken arena, Pirate Ship + Blackbeard's
  cabin).
- **`index.html`** — the gallery page itself: lays out all of the above into
  sections with captions, and lazily renders each plate to its own canvas as
  it scrolls into view.
