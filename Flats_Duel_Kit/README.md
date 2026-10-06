# Depth: Flats Duel kit

Everything needed to bring **Flats Duel** (Deep Arcade game 1) into Depth. It lives in `Depth\Flats_Duel_Kit\` and isn't compiled into the game until GUIDE.md's steps copy its files into `src\`.

**New here? [SESSION_REPORT.md](SESSION_REPORT.md)** explains the whole session (this kit and the ZeroTier online setup). **Then [HANDOFF.md](HANDOFF.md)** (what was done, what's next), then [GUIDE.md](GUIDE.md). It gives the order, where each file goes, and the check after each step.

## Contents
- **The engine:** a tested, headless rules engine, a symmetric board built on the unchanged single-player engine (`src/flats_duel*`).
- **Data:** the modes, the tuning numbers, six presets, the ban list, 10 dealer skins with their lines, and the emotes.
- **Networking:** the GameHost for the arcade session, plus a leak-proof snapshot format with a byte-for-byte hidden-information test.
- **Art:** the opponent in any skin (expressions, emotes, tells, reaching to lay a card), the icons, and the turn clock. See the renders in `previews/`.
- **The screen:** a reference table screen, sound cues, and a design-doc part.

## Results (bot vs bot, `tools/build_kit.ps1`)
- **First player wins:** 50.6% (Draft), 50.3% (Constructed), 50.7% (Quick), over 4,000 matches each. The target is 48-52%.
- **Presets:** each wins 43-59% overall.
- **Tests:**
  - leak: 21,012 snapshots unchanged by hidden information
  - views: 10,264 snapshots decoded and mirrored
  - wire: 40 matches finished over bytes with 0 failures

Rebuild and recheck:
```bash
powershell -ExecutionPolicy Bypass -File tools\build_kit.ps1 -Previews
```
