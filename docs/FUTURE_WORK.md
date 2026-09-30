# Depth: future work

Everything still to do, as of 2026-09-29. The plan is `Depth — Master Reference for Claude Code.pdf` (15 stages; progress in
`docs/MASTER_PROGRESS.md`). The other guiding documents are `Depth.pdf.pdf` (the original brief) and the Fathoms RTS design
document (`Final Paper_ Annotated Outline, Ray Phillips.pdf`, for stage 14). `Reference_ Zombies.pdf` is for a future fifth
Deep Arcade game; leave it until the user starts that game.

Working rules that still apply: numbers go in `data.cpp`; every system gets a headless self-test; the parkour side is
finished and not touched; all art is procedural; each stage ends at its Definition of Done; balance changes are logged in
`docs/MASTER_PROGRESS.md`.

## Where things stand

| Stage | Work | Status |
|---|---|---|
| 1 | Shared rig, lights, palettes, `--silhouette` | Built; still waiting for the user's side-by-side sign-off against Darkest Dungeon |
| 2 | The salon as Darkest Dungeon's hamlet | Done (see the open items below) |
| 3 | Sound architecture | Done |
| 4 | Sonar chart expeditions | Done |
| 5 | Expedition visual overhaul | **In progress** (paused here, see below) |
| 6 | EnemyBrain | Done |
| 7 | New expedition systems, the Trench and the Hadal | Done (balance deviations logged) |
| 8 | Expedition sound | Done (accessibility items open, see below) |
| 9-15 | Flats visuals, Flats systems, Deep Arcade + Scuttle, Flats Duel, the Trawl, Fathoms, arcade sound | Not started |

## Stage 5: expedition visual overhaul (in progress)

Done so far: the combat HUD, effects language and camera; every hero on the rig; the creature kit (IK legs, verlet
feelers, snapping claws) on the Cave's creatures, IK-planted legs on the Island/Weeds/Atlantis walkers, and chains for the
Siren's hair, Neptune's beard and Cthulhu's face; a boss phase change at half health (a 1.4 s rage clip, then an agitated
idle and a hotter palette, a stronger shift for the Abyssal Eye's third phase); mid-distance props and a foreground
occluder in every location; living details (crabs crossing the Cave floor, drips, the Cave's hanging net, the Island's
fireflies, shaking totem and a dog's eyes, fish schools, bubbles and a passing shadow in the Weeds, drifting banners, the
eye's blink and beat-synced sigils in Atlantis).

**This last pass (living details, the eye's blink, the Cave net, the phase-change clip) builds and renders but has not had
a careful visual review.** Look at `--shots shots foes_new`, `boss_sun_phase2`, `boss_abyssal_eye3` and the location
combat shots before building on it.

Still to do:
- **The full clip set for every class and enemy** (Master Reference: idle, stressed idle, Death's Door idle, walk, guard,
  a windup and strike per attack family (slash, thrust, swing, shoot, throw, cast, heal, song), hit, dodge, stagger,
  crit reaction, death, victory). The rig has the clip ids (`rig::CL_*`); heroes use a subset, enemies act through the
  `Ctx::P` deformation (reach, flinch, rear).
- **`--figures` sheets showing every clip** (they show idle, walk, windup, strike, hit and death now).
- **A signature idle per enemy** (the Sea Louse scuttling in place, the Siren's hair drifting, the Cultist's chains swaying).
- **Mini-boss entrance clips** (the Lobster surfacing from the silt, the Great White sliding in from the fog).
- **Death clips per enemy family**: crustaceans crack and spill, drowned things dissolve into bubbles, the tribe drops to
  a knee, Atlanteans crumble like statues.
- **Boss sets with independently moving parts**: Cthulhu's wings beating and sigil arm glowing in rhythm, the Queen's brood
  crawling on her carapace, Neptune's water spiralling round the trident, the Sun God's mask cracking as he takes damage.
- **Backgrounds as `Prop` lists with their own idle motion on all seven parallax layers** (props exist on the mid layer;
  the far layers are still painted bands), plus volumetric beams with dust from the key light.
- **More secondary motion**: feathers, tassels, fins and tentacles on the remaining Island/Weeds/Atlantis creatures (each
  has at least one hanging chain from stage 1).
- **"No figure motionless for more than 2 s"**: check it on every figure.
- **Hub figures on the rig**: the salon's hands, the dealer and the cat still use the older drawer (from stage 2's
  Definition of Done).
- **Guard should step the guarding figure in front of the one it protects** (Master Reference, combat additions). The
  mechanic works; the staging doesn't.

## Stage 7 and balance: open items

- **The four Shallows are balanced alike up to the endgame** (the user's call: Cthulhu is the endgame and stays hard).
  Last results (win % at cave levels 0/1/3/5/6): Cave 56/64/59/54/43, Island 52/63/63/56/44, Weeds 55/60/68/57/48.
  Atlantis reaches Cthulhu 85/83/81/68/58% of the time. Targets are 60/65/58/55/45: levels 0 and 1 still run about 5
  points low everywhere.
- **Open question for the user:** do Atlantis's mini-bosses (Armored Lost One, Alien Horror) count as endgame fights? They
  are currently held to target like everything else.
- The Hadal's level 7 wins 32% against a target of 35%. The Trench (45/38/36% at levels 5/6/7) has no target of its own.
- Against fresh crews the Sun God and Neptune now lose 93-94% (the older standalone target was 66-85%). The expedition
  numbers are the ones held level; decide whether the standalone target still matters.
- Spec deviations kept for balance: 5 camp points (spec 4), Triage Tent heals 6 (spec 3).
- `--sim` stands in for a player's gold by drilling slotted abilities (`SIM_DRILLS_BY_TIER`); revisit once real play data
  exists.
- A Nurse's Bone Saw is a button on the chart screen, not a camp skill.
- Elites get a brass name plate; the Master Reference also asks for a palette accent.
- The card sharp's free battle (a voyage event) isn't covered by `--flats-ui-test`.
- **Open question for the user:** is level 7 (the Hadal) the final crew level? It is built as the cap for now.

## Stage 8: sound, open items

- **Accessibility** (Master Reference, sound design): every sound tell (a dealer's tell, a Trawl threat, an eruption
  tremor) also needs a visual cue, and a **subtitles toggle** should show barks and dealer lines as text.
- Crew barks (the supportive bark between bonded heroes is a floating word, not spoken).
- The rest of the cue list belongs to later stages: Flats sounds (stage 9/15), the arcade (stage 15).

## Stages 9-15 (not started)

- **9. Flats visuals, card physics, dealer rigs** (`flats_physics.cpp`, `CardBody`): every card drawn with a 2-frame idle,
  frame wear by tier, edition finishes; the table, the salon behind it, five rigged dealers with hand styles, bottles
  with visible contents, the map as a sea chart under glass, node scenes, totems and charms on the table; card physics
  for the hand fan, drag, drop, sacrifice, draw (3D flip), dealer plays, strikes, death, fledgling/undying, the scales,
  bottles, the bell, momentum; dealer reactions and taunts. Gate: `--flats-ui-test` shows every motion.
- **10. Flats long map and systems**: 15 layers in three regions (the Shallows, the Reef, the Drowned Court), new nodes
  (Harbor, Toll, Fog, Elite lane, Undertow, Wager, Salvager), tribe passives, 12 new sigils, deck archetypes, Sea Marks,
  dealer tells, side bets, Depths 1-10, deck limits and a saved favourite deck. Gates: `--flats-sim` and `--flats-gen`.
- **11. The Deep Arcade, networking, Scuttle** (the networking design is docs/design/5_Depth_Arcade_Networking.md, steps N1-N7): cabinet menu (drum of reels, Host/Join/Browse), lobby, shared frame,
  GameNetworkingSockets (ENet fallback), host-authoritative model, LAN broadcast, a relay server (`relay/`), version
  check, reconnects, profiles and arcade tokens (cosmetics only), then Scuttle. Gate: two machines on different networks
  play Scuttle by join code; `--scuttle-sim`, `--net-loop`.
- **12. Flats Duel**: symmetric board, turn timers, second-player bonus, bans, draft/constructed/quick modes, sideboard,
  dealer skins, emotes, spectators, hidden information kept on the host. Gate: `--flats-duel-sim` first-player 48-52%.
- **13. The Trawl**: co-op night fishing horror (dock, sail, night, sell; stations, threats with tells and counters,
  grounds, gear, ghosts). Gate: `--trawl-sim` about 80% quota on the Shallows, 30% on Atlantis Waters.
- **14. Fathoms**: the island RTS, following its own design document's eight stages. Gate: a six-player match online.
- **15. Arcade sound and the final mix**: arcade menu music, every game's cues, a final mix pass. Gate: the full
  `--audio-test`.

## Other open items

- Stage 1's figures: the user's verdict was "right direction, still a ways to go". The Siren and Wisp's painted art are
  references to redraw on the rig.
- The Study hatch leads to an "Under Refit" placeholder. **Open question for the user:** should the hatch be visible from
  the start or unlock after the first expedition?
- **Open question for the user:** should arcade tokens ever unlock anything in single-player, or stay arcade-only?
- The parkour side is finished; its own history is in `ECOSYSTEM_ROADMAP.md`. A known pre-existing issue there: Pirate
  layouts often need several draws to validate (`--verify`).
- The fifth Deep Arcade game (the zombies reference) comes after the four in the Master Reference, only when asked.
