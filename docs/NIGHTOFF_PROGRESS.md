# A Night Off: build log

Design: `Reference_For_Future_MP_Games/A Night Off — Arcade Game 6 Design Document.pdf` (OCR in `docs/nightoff_pdf_pages/`).
Build order is the doc's p. 29 (nine stages).

## Stage 1: the bar, the meter, the night (done)
- `nightoff.h/.cpp` (headless `no::Night`), `nightoff_game.cpp` (the scene), data in `data/nightoff/`.
- The Sodden Gull: the main bar, the snug, the back room, the games corner, the kitchen hatch, the toilets, the yard.
- The drunk meter's bands (doc p. 6) set charisma and toughness; twelve drinks; the tab, prices by the hour, last call
  at 1:30 a.m. (double), cut-offs; the drunk walk (weave, stumbles, lurch), vomiting, passing out, walking home.
- The morning paper (headline, a line per player). Checks: `depth.exe --night-test`. Shots `night_*`.

## Stage 2: patrons and conversation (done)
- `nightoff_patrons.cpp`; data `nightoff_patrons.json` (8 types, 30 traits, 8 generic secrets, 29 regulars, the crowd
  curve) and `nightoff_dialogue.json`; nav nodes and seat lists in `nightoff_bar.json`.
- The crowd curve brings regulars in at their hours and fills the room with generated extras (Dead / Normal / Packed);
  patrons walk the nav graph, take free seats by type and haunt, drink, sour after 1 a.m. and leave at their hour.
  The regulars' stools are theirs (sitting on one starts a feud).
- Conversation (doc p. 10, p. 40): E near a patron; Ask / Agree / Joke / Challenge (d100 + charisma + mood + noise
  against a difficulty bent by traits), Listen (Talkers), Buy them a drink, Leave. Three wins end it well (a secret, a
  laugh buff, or an item), two losses badly (violent types shove). Past 80 on the meter the most insulting line comes
  out half the time. Mr. Lemmon pays your tab if you listen to him for 10 game minutes.
- Scene: patrons drawn on the shared crew rig (seated, walking, drinking, talking), name and mood faces over nearby
  heads (traits and known secrets too), the conversation panel; the camera swings round to frame a conversation.
- Checks: `--night-test` (the dead-night crowd, sitting, the talk flow, drunk substitutions), `--patron-check`.
  Shots `night_crowd`, `night_talk`.

## Stage 3: the bar games (done)
- `nightoff_games.h/.cpp` (headless rules, physics and bots), `nightoff_tables.cpp` (the games in the night: stations,
  challengers, `StartGame`, `GameAction` from Input, the opponents' turns, settling up), `nightoff_gamesui.cpp` (the
  screens), data `nightoff_games.json` (the meter's aim curve, opponents with skills and tells, stakes, the machines'
  weights, scratch-off odds, the 22-card tarot).
- Darts: 501 straight out (Around the Clock in the rules too); a reticle that drifts with the meter, released on a timing
  bar; Red Haddock hustles, Captain Vane plays for 100, the bartender plays one game a night for your tab.
- Pool: 8-ball on real 2D physics (cushions, pockets, English as follow/draw); a wobbling cue line, a power bar, an
  English dial, ball in hand; past 60 the cue sometimes misses the ball. Sly Pennick hustles (and loses the first game
  on purpose, then offers double or nothing); Bosun Grieve is easy.
- Mini golf: nine holes in the yard (the windmill, a dogleg, the drain, the sleeping dog, the loop, the bank, the
  gauntlet, the fish tank); a swaying line and a power bar; closes at 2 a.m. Finnegan hustles. The bot finds its way
  round walls with a distance field per hole.
- Slots: three machines, two rigged (84% and 76%) and one honest (115%: the fortune teller will tell you which);
  three kidneys pays 1,000 and a kidney; a wrecked player pulls until broke.
- Scratch-offs from the dispenser or Pip's coat (luckier; one in fifty is a map to the safe), scratched with the
  cursor, messier when drunk.
- The fortune teller: three cards, every reading true tonight (a kidney thief in the room by name, a thief due through
  the door, the hustler who'll lose once on purpose, who leaves when, the honest wheel, last call, the rich patron).
- `depth.exe --game-check <darts|pool|golf|slots|all> [games]`: a bot against each hustler at 0, 40 and 80 drunk.
  Last run: darts 58/39/6, pool 51/41/4, golf 56/36/9 (targets 55/35/10); slots 84/115/76%.
- `--night-test` plays each game through Input in a live night. Shots `night_game_*`.
- Not yet: cheating and side bets (stage 8), rain on holes 4 and 7 (events, stage 7), superstitious patrons acting on
  their readings.

## Next
4. Fights and weapons, mess and damage.  5. Flirting and the morning after.  6. Multiplayer (six), modes.
7. Events, the rest of the regulars.  8. Poker and bullshit, cheating, side bets.  9. Sound, profile, internet play.
