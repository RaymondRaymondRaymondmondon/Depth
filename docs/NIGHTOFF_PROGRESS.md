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

## Stage 4: fights, weapons, the mess and the bill (done)
- `nightoff_brawl.h/.cpp`, data `nightoff_fights.json`. Fighters are `Who` (a player, a patron, the alley dog), each
  with a `Combat`: HP 100 x toughness, jab / haymaker (a windup you can see) / grab (1.5 s, then thrown) / shove /
  block (halves) / dodge (above 60 it's falling over); swings drift with the meter above 40, so a drunk haymaker can hit
  the wrong person and pull them in.
- A brawl pulls in the violent within 3 m and anyone Delighted with a fighter; Sister Ash ends one by walking in;
  cowards run below 40% HP; a knockout is 30 s on the floor, three in a night and you're barred (thrown out).
  Violent patrons challenged in conversation swing first now.
- Props (`Prop`, 73 in the bar): tables (flip, break under a thrown body), chairs and stools (picked up, swung, thrown,
  broken), the street windows (a body through one is out for the night), the slot machines, the piano, the mirror,
  glasses and bottles on the tables (swept off a flipped table), the cue rack (a cue snaps after three hits into a
  jagged half), darts, golf clubs, the kitchen's pan (Tam sells it for 20) and knife, the shotgun under the till.
- A bottle smashed on the bar (C) is a broken bottle: the fight is armed and the police come in 3 minutes; while
  they're in, anyone fighting or armed is arrested. Taking the shotgun bars you; firing it hospitalizes whoever's in
  the cone, freezes every fight and ends your night in a cell.
- The ledger: every break in a brawl goes on its bill with what broke it; the bill goes on the starter's tab (if a
  patron started it and the bartender dislikes you, it's yours anyway); the bartender sours per fight and per break.
  Winners get the after-fight swing (+10% charisma with violent and loud patrons, -20% with everyone else).
- The alley dog: share your chips three times (5 each) and it follows you and bites and holds your foes.
- Scene: props drawn (flipped tables, broken chairs, shattered windows, a sparking slot machine), fight poses (guard,
  windup, swing, block, flinch, floored), floating hit words, health bars, your corner (HP, weapon, keys), the police
  countdown, the dog. Controls: LMB jab, RMB haymaker, F grab/throw, G shove, Q block, Space dodge, R pick up, X throw,
  C smash a bottle, E feed the dog.
- The gate (in `--night-test`): a brawl started in the games room runs (about 12 s, 6 fighters, 2 knockouts), wrecks
  it (a table, a window with a body through it, chairs, glasses, a bottle), and the bill matches what broke and lands
  on the starter's tab. Also checked: barred after three knockouts, Sister Ash, the smashed bottle and the police
  arrest, the dog. Shots `night_brawl`, `night_wreck`, `night_dog`.

## Stage 5: flirting, going home, the morning after (done)
- `nightoff_flirt.cpp`, data `nightoff_scoring.json` (the scoreboard's points, where going home ends up, the headline
  rules in order, where you wake), the `flirt` block of `nightoff_dialogue.json` (lines per option, drunk surprises, the
  thieves' funniest lines, the tells, the offers), and a `home` per regular in `nightoff_patrons.json`.
- The flirt: T near a patron (or Flirt in a conversation); open with a compliment, a joke, a drink or a dance, then two
  more exchanges (compliment, joke, ask about them, lean in); each line's chance is charisma + its fit (a romantic
  patron wants a compliment, a flirty one a joke, a suspicious one a drink first, a loud one a dance) + mood + the
  after-fight swing. Three successes make the offer (two on a packed night after 10, and for the kidney thieves, the best
  flirts in the bar); two failures end it. At 60+ what you say is a surprise. A sober player reads the tells (the
  thief's glance at the toilets' window, a married patron's ring, a sincere laugh).
- The offer: take it and the night ends in a fade; decline and they're your friend (they join your side in a fight).
  Where it ends up is the patron's: sincere (40%), a sweet nothing (15%), rich (5%), married (10%: you lose a shoe),
  robbery (15%: your money and your coat), the kidney thieves (10%: one kidney, every drink counts double); the fortune
  teller asks you herself after a late reading (a tarot card in your pocket). The extras roll the same odds, the kidney
  at 3%. A dog at your side saves the kidney; a bad night waits 30 s at the door when there are friends in the bar, and
  one who gets there first stops it.
- The bartender, for a drink: who's trouble tonight (one true name, one false).
- The endings: a stabbing sends you to hospital (a bill of 100), an arrest is a 200 fine, closing time while you're out
  cold is Knocked out.
- The morning: the doc's scoreboard (money kept, games won, fights won and won sober, going home, events, stories, both
  kidneys, walked home sober, the unpaid tab), a headline from the first rule the night satisfies (the shotgun, the
  kidney, a body through a window, an arrest, the mansion, the hospital, beating the bartender, a big bill, a 180, a
  hole in one, the slots' kidney, the married one's shoe, a sober win, everyone asleep...), and a story per player
  (where they woke, the best moment, what they lost). The morning screen shows them in columns.
- The gate (in `--night-test`): flirt with Dottie Finch, take her offer, wake in a bathtub with one kidney; the
  headline is "ONE SAILOR, ONE KIDNEY, A TRIUMPHANT <ACCORDION|GOAT|...>". Also checked: the regulars' odds, the
  line's fit, the dog, the teammate at the door, a sincere night's 100, a declined offer's friend, the bartender's
  warning. Shots `night_flirt`, `night_morning_kidney`.

## Next
6. Multiplayer and the morning after.  
   (six), modes.
7. Events, the rest of the regulars.  8. Poker and bullshit, cheating, side bets.  9. Sound, profile, internet play.
