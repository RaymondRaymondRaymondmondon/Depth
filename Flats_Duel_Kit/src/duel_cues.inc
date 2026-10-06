        // --- Flats Duel (Deep Arcade game 1). PASTE INTO: data.cpp, CueTable(), after the "arc.*" rows.
        // name, bus, recipe, freq, dur, gain, pitchVar, variants, priority, maxSim, duck. Only existing recipes are used,
        // so nothing new has to be synthesized; run depth.exe --audio-test afterwards (every registered cue must sound
        // and not clip).
        {"duel.turn", CB_UI, CR_BELL, 740, 0.8f, 0.10f, 0.0f, 2, 2, 1, false},        // your turn (a lighter bell than the table's)
        {"duel.theirturn", CB_UI, CR_TICK, 900, 0.06f, 0.06f, 0.02f, 2, 0, 1, false}, // the table passes to them
        {"duel.pick", CB_SFX, CR_CHALK, 2200, 0.10f, 0.10f, 0.1f, 4, 1, 2, false},    // a draft pick slides off the pack
        {"duel.pass", CB_SFX, CR_WHOOSH, 520, 0.35f, 0.10f, 0.08f, 3, 1, 1, false},   // the pack goes across the table
        {"duel.pack", CB_SFX, CR_CREAK, 300, 0.45f, 0.12f, 0.08f, 2, 1, 1, false},    // a new pack's twine and paper
        {"duel.swap", CB_SFX, CR_CHALK, 1600, 0.16f, 0.10f, 0.1f, 3, 1, 2, false},    // a sideboard swap
        {"duel.ready", CB_UI, CR_LATCH, 900, 0.3f, 0.14f, 0.05f, 2, 1, 1, false},     // the ready lever
        {"duel.reach", CB_SFX, CR_WHOOSH, 700, 0.22f, 0.07f, 0.1f, 3, 0, 2, false},   // the opponent's hand reaching out (the slap is flats' own)
        {"duel.clock", CB_UI, CR_CLOCK, 1500, 0.05f, 0.08f, 0.02f, 2, 0, 1, false},   // each of your last 10 seconds (arc.tick is Scuttle's)
        {"duel.timeout", CB_UI, CR_ERROR, 150, 0.35f, 0.16f, 0.03f, 1, 2, 1, false},  // the bell rang itself
        {"duel.game", CB_MUSIC, CR_FANFARE, 294, 1.4f, 0.14f, 0.0f, 1, 3, 1, true},   // you took a game
        {"duel.gamelost", CB_MUSIC, CR_GONG, 98, 1.3f, 0.12f, 0.03f, 1, 3, 1, false}, // they took one
        {"duel.match", CB_MUSIC, CR_FANFARE, 262, 2.0f, 0.16f, 0.0f, 1, 4, 1, true},  // the match (arc.match would do; this one is longer)
        {"duel.sudden", CB_MUSIC, CR_SWELL, 110, 1.6f, 0.14f, 0.0f, 1, 3, 1, true},   // level scales at the round limit: sudden death
        // the six emotes, played by the sender's figure (flats_duel_data.cpp EmoteOf(e).cue names them)
        {"duel.em.nod", CB_VOICE, CR_GRUNT, 180, 0.25f, 0.10f, 0.1f, 3, 1, 1, false},
        {"duel.em.sneer", CB_VOICE, CR_GRUNT, 140, 0.35f, 0.10f, 0.1f, 3, 1, 1, false},
        {"duel.em.shrug", CB_SFX, CR_WHOOSH, 380, 0.3f, 0.08f, 0.1f, 2, 1, 1, false},
        {"duel.em.tiphat", CB_SFX, CR_PLAQUE, 1100, 0.2f, 0.10f, 0.05f, 2, 1, 1, false},
        {"duel.em.clap", CB_SFX, CR_THUD, 900, 0.08f, 0.12f, 0.12f, 4, 1, 3, false}, // played three times, on each clap (EmotePose's cosine)
        {"duel.em.gulp", CB_SFX, CR_BUBBLING, 320, 0.3f, 0.10f, 0.08f, 2, 1, 1, false},
