// ============================================================================
//  DEPTH - "How to play" for the game menu (the playtest, 2026-10-05: players didn't know how most games worked,
//  and the Controls page offered bindings that didn't belong to the game in front of them). One entry per place:
//  what you're doing there, how a round goes, and the keys it really uses. menu.cpp draws it; the Controls page
//  shows the same key list beside the few actions that can be rebound there.
// ============================================================================
#include "game.h"
#include "input.h"
#include <algorithm>

namespace {
const HowTo& Get(int key) {
    static HowTo H[24];
    static bool made = false;
    if (!made) {
        made = true;
        auto& hub = H[0];
        hub.title = "The Nautilus";
        hub.lines = {"The submarine is home. Click a station in the salon to use it: the Helm (expeditions), Crew Quarters, the Radar Room (recruits), the Ward and Sick Bay, the Workshop, the Library, the Periscope (platform dives), the card table (Flats), the Deep Arcade (games with friends) and the Study hatch.",
                     "Drag crew portraits from the roster on the right into the four party slots, then press Embark to choose an expedition at the Helm.",
                     "The game saves whenever you come back to the salon."};
        hub.keys = {{"Mouse", "everything aboard"}, {"Esc or P", "this menu"}, {"F11", "fullscreen"}};
        auto& ex = H[1];
        ex.title = "Expeditions";
        ex.lines = {"Lead four crew through a sonar-charted dive. Click a neighbouring room on the chart to move. Fights are turn based: each crew member acts in turn order; pick a skill from the bar, then a target.",
                    "Ranks matter: each skill says which ranks it can be used from and which it can hit. Watch health (red) and nerves (blue): at 100 nerves a hero is Rattled or Steeled.",
                    "Light falls as you go; burn a battery to raise it. Camps heal, curios can help or hurt, and the boss waits in the far room. Retreat keeps your crew but forfeits the objective."};
        ex.keys = {{"Mouse", "move, choose skills and targets"}, {"Tab (rebindable)", "the sonar scope"}, {"Esc or P", "this menu"}};
        auto& pk = H[2];
        pk.title = "Platform dives";
        pk.lines = {"Cross the level to the exit. One touch of a hazard or an enemy is fatal, and a death restarts the level.",
                    "The hard part is the jumping: precise landings, wall jumps up narrow chimneys, timed jets. Creatures live their own lives: watch what eats what.",
                    "Down slides while running (and under low beams); double-tap a direction to dash; hold Up while falling to slow the fall; push into a wall's lip to grab it."};
        pk.keys = {{"A / D (rebindable)", "run"}, {"W (rebindable)", "climb, slow a fall"}, {"S (rebindable)", "slide, roll, slide down poles"}, {"Space (rebindable)", "jump (again off walls)"}, {"Shift (rebindable)", "modifier: backflip off poles"}, {"Double-tap a direction", "dash"}};
        auto& ab = H[3];
        ab.title = "The Abyss";
        ab.lines = {"Swim down the trench in 3D. Your helmet lamp lights what's ahead; the deep things come for you.", "Pressure-bulbs drive the Trench-Maw back; void-moss hides you; ghost-kelp restores your stamina."};
        ab.keys = {{"A / D (rebindable)", "swim left / right"}, {"W / S (rebindable)", "swim up / down"}, {"Space (rebindable)", "dash"}, {"Shift (rebindable)", "glide"}};
        auto& fl = H[4];
        fl.title = "Flats (the card table)";
        fl.lines = {"A creature duel against the dealer on a 4 x 4 board. Each turn: draw (a card or a minnow), play creatures into your front row, then ring the bell.",
                    "Every creature strikes the space straight ahead. A hit on an empty lane lands on the scales; tip the scales 8 your way to win the battle.",
                    "Costs: BLOOD means sacrificing your own creatures (click them); BONES come from your creatures dying. Between battles, the map's nodes change your deck."};
        fl.keys = {{"Mouse", "pick cards, lanes and sacrifices"}, {"Right-click", "cancel"}, {"Hover a card", "read it"}};
        auto& ar = H[5];
        ar.title = "The Deep Arcade";
        ar.lines = {"Pick a group with the tabs (Action, Strategy, Fighting, Traditional, Slop), roll the drum with the mouse wheel or the arrows, and pick a game.",
                    "Host opens a table; friends click Browse, or Join and type the host's code (same network) or address (over ZeroTier: the 10.x.x.x address). The host can fill empty seats with AI, picks the rules, and starts.",
                    "Many games also play solo against bots from their own button on the drum's face."};
        ar.keys = {{"Mouse wheel / Up, Down", "roll the drum"}, {"Caps Lock (rebindable)", "push to talk at a table"}, {"Enter", "send a chat line"}};
        auto& sc = H[6];
        sc.title = "Scuttle";
        sc.lines = {"Race your crab from the start to the tide line (space 8). The first crab there wins the heat; win two heats to take the match.",
                    "On your turn play one card from your hand: move (Scuttle 1-3, Sidestep), push everyone (Wave, Current), or hinder a rival (Pinch, Rock, Gull). Shell protects you; Molt refreshes your hand.",
                    "Before a heat you may bet pearls face down on who wins."};
        sc.keys = {{"Mouse", "play a card, then click its target"}, {"Right-click", "cancel a target"}};
        auto& rt = H[7];
        rt.title = "Red Tide";
        rt.lines = {"Co-op divers in a living reef. Each tide, kill and harvest creatures to make the quota: scrip buys gear between tides.",
                    "Blood in the water draws predators, and the predators draw worse. Stay together, mind your air, and get out before the Hunt."};
        rt.keys = {{"WASD", "swim"}, {"Space / Ctrl", "up / down"}, {"Shift", "sprint"}, {"Right mouse", "aim"}, {"Left mouse", "fire"}, {"R", "reload"}, {"E", "use"}, {"V", "knife"}, {"G", "limpet"}, {"1-3", "weapons"}};
        auto& tw = H[8];
        tw.title = "The Trawl";
        tw.lines = {"Work a steam trawler by night with your crew: steam past the harbour line, fish (rods, the net, the harpoon), gut and ice the catch, and keep her afloat, then sell at the quay before the Owners' deadline.",
                    "A fish on the line: reel with left mouse, ease off with right when it runs or jumps, steer it with the mouse. Watch the line's tension."};
        tw.keys = {{"WASD", "walk (follows your look in first person)"}, {"C or Ctrl", "crouch (first person)"}, {"Mouse", "look / aim"}, {"E", "use a station, ladders"}, {"Left mouse", "cast, reel, gut"}, {"Right mouse", "bow to a jumping fish"}, {"Space", "gaff a fish alongside"}, {"Scroll", "drag or depth"}, {"V", "top-down / first person"}, {"G", "order a bot hand"}, {"F", "a bot follows you"}};
        auto& fg = H[9];
        fg.title = "The Flight";
        fg.lines = {"Be the bird. Fly your Founder over the islands, dive for fish, fill the nest's courtship bowl (three fish) to call a mate, and grow a colony that fishes, scouts and fights for you.",
                    "Each day the colony eats from its caches. The colony panel (Tab) sets roles; the chart (M) sends flocks to grounds, islands and rivals."};
        fg.keys = {{"Mouse", "steer (look where you fly)"}, {"W", "flap"}, {"Shift", "sprint"}, {"S", "flare / brake"}, {"A / D", "turn"}, {"Space", "take off"}, {"E", "use: bowl, cache, twigs, nest site"}, {"F", "eat"}, {"Tab", "colony"}, {"M", "chart"}, {"G", "lead the nearest flock"}};
        auto& mf = H[10];
        mf.title = "Mouthful";
        mf.lines = {"Start as a fry. Anything smaller than 60% of you is swallowed whole; close to your size is a fight; bigger: flee.",
                    "Eat to grow through the tiers; at each fork pick a path (shark, crustacean and the rest). The biggest mouth at the end wears the crown."};
        mf.keys = {{"Mouse", "steer"}, {"W", "swim"}, {"S", "stop"}, {"Shift", "boost (stamina)"}, {"Space / Ctrl", "up / down"}, {"Left click", "bite"}, {"Right click or E", "your form's ability"}, {"Tab", "the food chain"}};
        auto& no = H[11];
        no.title = "A Night Off";
        no.lines = {"One night ashore at the Sodden Gull. Drink, play the bar's games, flirt, fight, and make it to the morning with both kidneys.",
                    "Every drink lowers charisma and raises toughness. Games at the tables win or lose your wages. Walk out the door to end your night.",
                    "Bullshit (at the card table): play cards face down, claiming the rank that's called. Anyone may call \"Bullshit!\": a liar picks up the pile, a wrong accuser does. Empty your hand to win."};
        no.keys = {{"WASD", "walk"}, {"Shift", "hurry"}, {"Ctrl", "squat"}, {"Mouse", "look"}, {"E", "the bar, the hatch, a game, a patron; the door: home"}, {"Enter", "talk to the table"}, {"F5-F10", "toast, point, laugh, shrug, fists up, fall over"}, {"Left / right mouse", "jab / haymaker (in a fight)"}, {"F / G / Q", "grab or throw / shove / block"}, {"Space", "dodge"}, {"R / X", "pick up / throw"}, {"C", "smash a bottle on the bar"}};
        auto& sf = H[12];
        sf.title = "Scuffle";
        sf.lines = {"A stick fight: the last stick standing wins the round; first to the target wins the match. Crates fall from the sky with weapons and gear, and the stage's hazards kill anyone.",
                    "After about 45 seconds the wall closes in. Modes add teams, a plank to hold, an egg to carry, sharks, duels, a Gauntlet race and boss fights.",
                    "After a round, R replays the last 10 seconds. The locker (on the arcade's Scuffle panel) holds skins, hats and the crate.",
                "New to it? The arcade's Scuffle panel has Training: twelve short lessons against a dummy (run, climb, dive, punch, the haymaker, the dive kick, grab and throw, deflecting shots, weapons, the knockout)."};
        sf.keys = {{"A / D", "run"}, {"W or Space", "jump (and climb walls)"}, {"S", "duck; dive in the air; duck on a gun to swap"}, {"Mouse", "aim"}, {"Left click", "fire, punch (hold against someone: grab)"}, {"E or right mouse", "gear"}, {"T", "taunt"}, {"1-3", "Duel: pick a weapon"}};
        auto& st = H[13];
        st.title = "The Study";
        st.lines = {"A quiet room below decks for studying: a soundscape you mix, a chronometer, and course packs to work through.", "Open the desk's drawers for the soundscape, the timer and the courses."};
        st.keys = {{"Mouse", "everything"}};
        auto& pe = H[14];
        pe.title = "The Periscope";
        pe.lines = {"Choose a platform dive. Each level unlocks when the previous is cleared. The folder beside each opens its dossier: the story, the food web and field notes.", "Options here: checkpoints (no relic), Normal or Hard, and each boss on or off."};
        pe.keys = {{"Mouse", "choose"}};
        auto& wd = H[15];
        wd.title = "Warp Dodgeball";
        wd.lines = {"Team dodgeball in first person. Hit an opponent with a live ball (before it touches the floor) and they're out. Catch one and the thrower is out, and your first teammate out comes back. Knock a whole team out to take the round; first to three rounds wins.",
                    "Each round opens with the rush: the balls sit on the centre line, and a ball grabbed there must be carried behind your attack line before it can be thrown. Never step over the centre line.",
                    "Everyone carries a portal pair. Shoot A and B onto the light grey panels and throw through one to come out of the other at the same speed. Careful: your own ball, off a wall or out of a portal, gets YOU out if it hits you.",
                    "Hold the throw to charge (a tap lobs, a full charge fires flat and fast; held too long, your aim shakes). Z or X, or a sideways flick of the mouse as you let go, curves it. Holding a ball blocks a soft throw, but a hard one knocks it out of your hands."};
        auto& nc = H[17];
        nc.title = "NOCLIP";
        nc.lines = {"You're salvagers for the Liminal Recovery Bureau in the Backrooms. Each day you land at a Threshold Lab, find things worth money, carry them back to a Lab's crate, signal the portal at the desk (a 30-second charge that every entity on the level can hear), and stand in the ring when it opens.",
                    "On the Surface, sell to the Bureau (quota credit; it keeps the campaign alive) or the Fence (cash for gear). Meet the week's quota by the end of day 3 or the Bureau stops maintaining your portals.",
                    "Watch three meters: health, sanity (the Backrooms change what you see when it's low: almond water, company and Lab lights restore it) and stamina. Entities each hunt by one sense and have a tell and a counter: never run from a Smiler, walk past Hounds. Restart dormant Labs (a breaker, then a fuel canister) to build the network you can jump across.",
                    "After 24:00 it's Overtime: the portals shut until 06:00 and only a lit Lab is safe."};
        nc.keys = {{"WASD / mouse", "walk / look"}, {"Shift", "sprint"}, {"Ctrl", "crouch"}, {"E", "use, pick up, doors, a Lab's fittings, revive"}, {"G / T", "drop / throw"}, {"F", "headlamp"}, {"1-5, wheel", "a tool"}, {"Left mouse", "use the tool"}, {"M", "the field map (with the way back dotted)"}, {"B (hold)", "the bearing back to a Lab, along the corridors"}, {"Y", "accept a Partygoer's invitation (don't)"}, {"Caps Lock", "talk (nearby only)"}, {"F8", "the tape effect: off, light, full"}};
        auto& fw = H[16];
        fw.title = "Fowl Play";
        fw.lines = {"Six stalls on the gun club's porch, one sky over the marsh. Each round is a 60-second hunt: birds flush from the reeds in waves, and whoever fires the killing shot gets the bird (a goose softened by one player is stolen by another). Most birds after 15 rounds wins.",
                    "Birds pay money at the tally. Between hunts you have 45 seconds in the clubhouse: buy guns and attachments at the counter (Mr. Zappa has a deal just for you), crank the mystery-gun machine, play the slots and scratch-offs, or visit the raccoon's Slop Shop for cosmetics and sabotage.",
                    "Sabotage lands on your target's next hunt and everyone sees who bought it; every item has a counter (buy it before the hunt). Shooting the dog costs you a bird. Decoys have no eyes and cost $10. The armored duck's weak spot is its belly."};
        fw.keys = {{"Mouse", "aim"}, {"Left mouse", "shoot"}, {"Right mouse", "scope / both barrels"}, {"A / D", "step in your stall"}, {"Q / E", "lean"}, {"Ctrl", "crouch"}, {"R", "reload"}, {"1 / 2, wheel", "swap guns"}, {"H", "swap with the stall hook"}, {"V (hold)", "wipe a smudged lens"}, {"T", "scratch a ticket from your pocket"}, {"WASD, E", "walk the room, use a station"}, {"F", "pick up a gun from the floor"}};
        auto& fz = H[19];
        fz.title = "Fathoms";
        fz.lines = {"A steampunk island RTS for 2-6 players. Grow a colony from your Harbor: workers gather Food, Brass and Coal and bring them to a drop site (the Harbor, a Farmstead for food, a Mine Shed for ore); Fishing Boats fish the shallows. Cottages raise your population.",
                    "Advance at the Harbor through three eras: Sail, Steam (needs two of Barracks, Stable, Dock, Farmstead, Mine Shed) and Leviathan (Ichor: ruins, volcano vents, or the pirates' black market). Each faction has a mechanic, two unique units, a hero, an ultimate and a wonder.",
                    "Claim other islands with a Lighthouse: board workers onto a Transport (right-click it), sail, then Unload on the shore. Steam ships need coal: beyond a Harbor, Dock or Coaling Station they slow down. Landed troops take +25% damage for 10 s; morale falls away from your ground.",
                    "Neutral powers: pirate coves (black market, raids you can hire, counter-bids), tribal towns (raid you from minute 6; tribute, conquest, or the Islanders' Kinship), volcanoes (the Sun God and the altar), sunken ruins with relics, the Ghost Ship, the Kraken.",
                    "Win by conquest (no Harbor or Colony Hall left), by holding every relic for 3 minutes, by holding the volcano's altar for 4, or with the best score at the time cap."};
        fz.keys = {{"Left drag", "select (double-click: all of a kind)"}, {"Right click", "move, attack, gather, garrison, board"}, {"Ctrl + right click", "attack a cove or a tribal town"}, {"A then click", "attack-move"}, {"S", "stop"}, {"R", "hero ability"}, {"U then click", "unload a transport"}, {"Arrows / edges / middle drag", "pan"}, {"Wheel", "zoom"}, {"Ctrl+1-9 / 1-9", "set / recall a group"}, {"Tab", "next idle worker"}, {"H", "your Harbor"}, {"Space", "the last alert"}, {"F1 / F2 / F3", "help / diplomacy / Logbook"}, {"Delete", "dismiss the selection"}, {"Alt+click (Ctrl, Shift)", "ping your allies: attack (defend, danger)"}};
        auto& bpz = H[18];
        bpz.title = "Ball Pit Brawl";
        bpz.lines = {"A foam-weapon shooter inside a four-level play centre. Everyone starts with a foam knife and a single-shot blaster; knockouts and objectives earn score, which you spend at your team's store (at your base, B) on better foam guns and darts. Spending never lowers your leaderboard score.",
                     "Any live ball knocks a player out in one hit, until its first bounce. Grab balls from the pits (F) and throw them; or get on a cannon (E): six balls a second, but it overheats, and every ball comes back through one shared conveyor, so firing drains everyone's supply.",
                     "Climb the cargo nets and ladders (slow, exposed), ride the slides (fast, one way), cross the rope bridges (your aim wobbles) or the netted routes (you can see through nets but not shoot through them). Crawl tunnels are knife only. Crouch in a ball pit to hide under the balls: darts can't reach you.",
                     "Out of darts? Hold Q to vacuum up foam darts from the floor, anyone's. Score earned without being knocked out builds a streak that unlocks your three chosen rewards (4, 5, 6; pick them with L).",
                     "Modes: free for all, team deathmatch, capture the flag (flags on each tower's roof), and disarm the bomb (no respawns in a round)."};
        bpz.keys = {{"WASD / mouse", "move / look"}, {"Space", "jump"}, {"Ctrl or C", "crouch (in a pit: submerge)"}, {"Left mouse", "fire / throw a ball"}, {"Right mouse", "aim down the sights"}, {"1 / 2, wheel", "starter / your bought gun"}, {"R", "reload"}, {"V or middle mouse", "foam knife"}, {"Q (hold)", "vacuum darts"}, {"F", "grab a ball"}, {"E", "cannon, slide, plant, disarm, leave an RC car"}, {"B", "the store"}, {"4 5 6", "streak rewards"}, {"L", "choose rewards"}, {"T / G", "pick / use a joke item"}, {"Tab", "scores"}, {"H", "the key card"}};        wd.keys = {{"WASD / mouse", "move / look"}, {"Shift", "sprint"}, {"Space", "jump"}, {"Ctrl", "crouch (hold)"}, {"C", "squat (a quick duck)"}, {"Q + direction", "dive"}, {"Left mouse", "hold to charge, release to throw"}, {"Right mouse", "cancel the throw"}, {"Z / X", "curve left / right"}, {"E / R", "portal A / B"}, {"F or middle mouse", "catch (when the ring goes green)"}, {"H", "the key card"}, {"Tab (when out)", "the view: the whole court, your bench, or a teammate"}};
    }
    return H[std::clamp(key, 0, 23)];
}
}

const HowTo& HowToFor(const Game& g) {
    switch (g.scene) {
        case Scene::Dungeon: return Get(1);
        case Scene::Platformer: return Get(2);
        case Scene::Abyss: return Get(3);
        case Scene::Cards: return Get(4);
        case Scene::Arcade: { int ag = ArcadeTableGame(); return Get(ag == 2 ? 6 : 5); }   // (Scuttle is played on the arcade's own screen)
        case Scene::RedTide: return Get(7);
        case Scene::Trawl: return Get(8);
        case Scene::Flight: return Get(9);
        case Scene::Mouthful: return Get(10);
        case Scene::NightOff: return Get(11);
        case Scene::Scuffle: return Get(12);
        case Scene::Study: return Get(13);
        case Scene::Periscope: return Get(14);
        case Scene::Warp: return Get(15);
        case Scene::Fowl: return Get(16);
        case Scene::Noclip: return Get(17);
        case Scene::BallPit: return Get(18);
        case Scene::Fathoms: return Get(19);
        default: return Get(0);
    }
}
// which rebindable actions mean something here (the rest are hidden from the Controls page)
bool ActUsedIn(const Game& g, int a) {
    if (a == A_MENU) return true;
    switch (g.scene) {
        case Scene::Platformer: case Scene::Abyss: return a <= A_MOD;
        case Scene::Dungeon: return a == A_SCOPE;
        case Scene::Arcade: case Scene::RedTide: case Scene::Trawl: case Scene::Flight: case Scene::Mouthful: case Scene::NightOff: case Scene::Scuffle: case Scene::Warp: case Scene::Fowl: case Scene::Noclip: case Scene::BallPit: case Scene::Fathoms: return a == A_TALK;
        default: return false;
    }
}
