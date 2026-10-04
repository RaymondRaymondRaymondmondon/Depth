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

## Next
3. Games: darts, pool, then golf, slots, scratch-offs, the fortune teller.
4. Fights and weapons, mess and damage.  5. Flirting and the morning after.  6. Multiplayer (six), modes.
7. Events, the rest of the regulars.  8. Poker and bullshit, cheating, side bets.  9. Sound, profile, internet play.
