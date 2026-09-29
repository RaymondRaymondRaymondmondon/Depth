// The Periscope dossier: a manila file per dive. Open it and it lies across the desk - on the left the level's
// story, its food web drawn as a chart (the outline is known from the start; a species you haven't met is a
// question mark), and the notes on whatever entry you've picked; on the right the roster of its beasts and flora.
// Entries fill in as you meet them in play (Game::platSeen, saved); the notes are written from the live data -
// who it hunts, who hunts it, what it does - so they never drift from how the creature actually behaves.
#include "game.h"
#include "beasts.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

const char* LEVEL_STORY[PL_COUNT + 1] = {
    // the Pipes
    "The Nautilus breathes through these ducts. Steam hisses through a thousand joints, gauges tremble, and the heat never leaves the iron. "
    "Life has moved in anyway: moths drawn to the glow-beetles, spiders strung across the trusses, a blind rat that has never seen the sea. "
    "None of it cares that you are here. Only the jumps can kill you - but they will, and often.",
    // the Hull
    "Out along the Nautilus's back, where the plating runs into open blue. A reef has grown on the old rivets: crabs in the seams, "
    "eels in the breaches, anemones that close like fists. A grazing whale drifts over, and something far larger follows its shadow. "
    "The Kraken keeps its lair where the deck ends.",
    // the Pirate Ship
    "A fleet that never made port, riding out a storm that never ends. The decks belong to the ship's animals now - rats, a cat gone half wild, "
    "a dog maddened by fleas - and to the pirates who still stand their watches. Under the waterline the Grand Kraken waits for a hull to hold. "
    "Blackbeard keeps the captain's cabin, and he does not share.",
    // the Island
    "An island of idols to gods no one remembers. A volcano smokes over stilt villages, temples and a river that runs down to the sea; "
    "day turns to night while you climb. The jungle is a food web stacked in layers - beetles, skippers and frogs below, snakes and stalkers "
    "above - and the Arch-Serpent over everything. Learn who is out at night.",
    // the Cave
    "A flooded cave beneath the Shallows, black but for fungus and the things that glow. Everything down here hunts by feel, by sound, by "
    "tremor. The olm and the loach have no eyes to lose; the Echo-Stalker hears your heartbeat; the Tremor Worm feels you run. "
    "Somewhere in the tunnels, the Abyssal Arachnid has strung its webs at the height of a diver's head.",
    // the Weeds
    "A sunlit kelp forest, beautiful from above. The seabed is the danger: urchin carpets, rays buried in the sand, currents that pull "
    "at your legs. Up in the canopy, plankton feeds seahorses, seahorses feed barracuda, and barracuda feed the mermen - who fight the "
    "tiger sharks for everything. Bleed here and the Leviathan will come.",
    // Atlantis
    "The drowned city still keeps its own light: glyphs that burn in the stone, crystal kelp, anglers' lures in the dark. Its guardians "
    "never stood down - the Phalanx still holds its rows, the gargoyles still watch the mirrors, and the Lost Ones still walk to the temple, "
    "listening. Keep moving. Poseidon's Scourge only catches what stops.",
    // the Abyss
    "Below everything: a vertical trench falling into the black, twice as wide as it looks, lit only by your lamp. Glass sponges grow on "
    "the ledges, isopods cling to the walls, and whale-falls feed things that have never seen light. The Trench Maw rises from the "
    "bottom when it hears you. There are depth buoys to the floor. Few have read the last one.",
};

// what each piece of flora does for you (or to you) - the notes can't be derived from the diet, so they're written here
const struct { const char* name; const char* note; } FLORA_NOTES[] = {
    {"Barnacle-Mites", "A crust of mites on the plating. Dash through them: they make the deck slick, and you slide further with less drag."},
    {"Rust-Algae", "Soft red fur on rotten steel. An eel's slam shatters it - and whatever was standing on it."},
    {"Bio-Electric Hydroids", "Feathery stingers that store a charge. Strike one and the shock stuns whatever is nearby - even the Siphon."},
    {"Pressure-Anemone", "It swells and bursts on a rhythm: a jet of water that throws you, if you time it."},
    {"Metal-Eater Moss", "It eats the plating and leaves soft patches. Where it grows thick, the deck gives underfoot."},
    {"Hull-Kelp", "Tough kelp rooted in the rivets. Hold on to it and the megalodon's turbulence can't tear you loose."},
    {"Wood-Borer Worms", "They riddle the planking from inside. Boards they've eaten crack under a hard landing."},
    {"Cannon-Moss", "Thick moss grown over old gun-decks. Land on it and you land in silence - and without a stun."},
    {"Ship-Rot Fungus", "Pale shelves on wet timber. Stand on one too long and it gives way."},
    {"Mast-Kelp", "Kelp that has climbed the masts. Climb it like a ratline."},
    {"Barnacle-Cluster", "Razor shells on the hull. Walking over them is safe; dashing into them is not."},
    {"Siren's Lantern Weed", "A weed that glows when something stirs near it - it shows a hiding cuttlefish."},
    {"Tribal Drum-Fungus", "A taut fungal drum. Land on it and it throws you high - and the boom wakes the centipede."},
    {"Poison-Dart Vine", "It fires darts at movement. Lure a stalker's strike through it."},
    {"Razor-Palm Roots", "Knife-edged roots at the foot of the palms. Deadly to run through; jump them."},
    {"Idol's Bloom", "A flower grown at the feet of the idols. Its scent calms what hunts near it."},
    {"Mangrove Root-Sponge", "A spongy root. Land on it and it bounces you back up."},
    {"Lantern-Shroom", "Glowing caps. They light the water around them - and the things that hunt by light."},
    {"Acid-Lichen", "A burning crust on the rock. Don't wall-jump from it."},
    {"Vibration-Spore", "Puffballs that burst at a footfall and carry the sound to anything listening."},
    {"Cave-Cabbage", "Broad soft leaves. Slide through them and the Echo-Stalker loses your sound."},
    {"Nerve-Root", "Roots that twitch when something heavy moves nearby: a warning that the Tremor Worm is coming."},
    {"Blood-Kelp", "Red kelp that bleeds when grazed. The Leviathan smells it from across the forest - graze it away from your path."},
    {"Luminescent Anemone", "It flashes when touched, and every hunter nearby looks."},
    {"Air-Weed", "Bladders of trapped air. Pop one and it stuns the mantis that was aiming at you."},
    {"Tangle-Vine", "It wraps whatever swims through it slowly. Pass quickly or not at all."},
    {"Spore-Pod", "It bursts into a blinding cloud. Good cover; bad footing."},
    {"Sun-Crystal Kelp", "Kelp grown through crystal. It stores light and gives it back in the dark."},
    {"Prism-Moss", "It bends a hard landing sideways instead of stopping it."},
    {"Aqueduct-Vine", "It grows along the broken aqueducts and holds them together - for now."},
    {"Stasis-Lily", "Touch it and everything near it freezes a moment - even a Phalanx in mid-charge."},
    {"Ruin-Spore", "Spores in the old stone. Disturb them and the Lost Ones hear."},
};

// the Abyss's creatures (they live in abyss.cpp, not the beast engine), in AbyssCreatureKind order
const struct { const char* name; bool flora; const char* note; } ABYSS_ROSTER[] = {
    {"Glass Sponge", false, "A basket of silica on the ledges. It shatters when something heavy lands on it - don't wait on one."},
    {"Giant Isopod", false, "Armoured scavengers clinging to the walls. Brush one and it drops, and rolls down the trench like a boulder."},
    {"Gulper Eel", false, "A whip of a body behind a mouth that is almost all of it. Follow its pink tail-light and you'll find its jaws."},
    {"Bioluminescent Plankton", false, "Motes that flare as you pass. Beautiful, and they tell everything else where you are."},
    {"Vampire Squid", false, "It turns its cloak inside out when threatened and hides in its own light."},
    {"Siphonophore", false, "A colony strung out for metres, a curtain of stinging threads. Find the gaps in the maze."},
    {"Leviathan", false, "Something passes in the dark beyond the lamp. You'll see its eyes before anything else."},
    {"Trench Worm", false, "It lunges from the wall at anything that lingers. Keep moving past the burrows."},
    {"Hatchetfish", false, "Silver prey fish with lights on their bellies. Harmless. Everything else eats them."},
    {"Brine Slug", false, "Slow and heavy. In the bowling lane, it's the ball."},
    {"Rock Ledge", false, "Plain stone that never breaks. Wait out whatever is patrolling below."},
    {"Whale-Fall Scavenger", false, "A gargantuan isopod grazing a whale's skeleton. Its flat back is cover from the Pressure Ghost's pull."},
    {"Angler-Cephalopod", false, "It hides in void-moss and dangles a lure like ghost-kelp. Come close for the light and its beak has you."},
    {"Pressure Ghost", false, "A vast jellyfish. It swells - that's the tell - then inhales everything toward it."},
    {"Trench Maw", false, "The apex: a jaw wider than the trench, rising from the bottom. A pressure-bulb's blast drives it back down."},
    {"Trench Krill", false, "Swarms that only light up in your wake. They show you where the currents run."},
    {"Slime Hagfish", false, "Drawn to blood. Their mucus makes the water slick: you fall faster through it."},
    {"Vent Worms", true, "Red plumes around the vents, where the updraft is. Ride it up."},
    {"Void-Moss", true, "A black moss that swallows light. Dash through it and nothing can see you for a few seconds."},
    {"Pressure-Bulb", true, "A bulb that bursts in a shockwave. It drives the Trench Maw back - once."},
    {"Abyssal Coral", true, "It closes on anything that touches it."},
    {"Ghost-Kelp", true, "Pale kelp that refills your stamina. The angler's lure is made to look like it."},
};

struct Entry { std::string name; bool flora = false, seen = false; int idx = 0; };

std::vector<Entry> Roster(const Game& g, int lv) {
    std::vector<Entry> out;
    if (lv == PL_COUNT) {
        int n = (int)(sizeof(ABYSS_ROSTER) / sizeof(ABYSS_ROSTER[0]));
        for (int k = 0; k < n; k++) if (k != (int)AbyssCreatureKind::RockLedge) out.push_back({ABYSS_ROSTER[k].name, ABYSS_ROSTER[k].flora, (g.platSeen[lv] >> k & 1) != 0, k});
        return out;
    }
    for (int s = 0; s < BeastSpeciesCount(lv); s++) {
        const SpeciesDef& S = BeastSpecies(lv, s);
        if (!S.name || !strcmp(S.name, "Coconut")) continue; // a falling coconut is a prop, not a creature
        out.push_back({S.name, (S.traits & T_FLORA) != 0, s < 64 && (g.platSeen[lv] >> s & 1) != 0, s});
    }
    return out;
}

std::string Names(const Game& g, int lv, const std::vector<int>& ids) {
    std::string s;
    for (size_t k = 0; k < ids.size(); k++) {
        bool seen = ids[k] < 64 && (g.platSeen[lv] >> ids[k] & 1);
        if (k) s += k + 1 == ids.size() ? " and " : ", ";
        s += seen ? BeastSpecies(lv, ids[k]).name : "something unseen";
    }
    return s;
}

// field notes written from the creature's real data
std::string Notes(const Game& g, int lv, const Entry& e) {
    if (lv == PL_COUNT) return ABYSS_ROSTER[e.idx].note;
    if (e.flora) { for (const auto& f : FLORA_NOTES) if (e.name == f.name) return f.note; return "A growth of this place."; }
    const SpeciesDef& S = BeastSpecies(lv, e.idx);
    std::vector<int> eats, eatenBy;
    for (int k = 0; k < BeastWebCount(lv); k++) {
        FoodEdge fe = BeastWebEdge(lv, k);
        if (fe.pred == e.idx && std::find(eats.begin(), eats.end(), fe.prey) == eats.end()) eats.push_back(fe.prey);
        if (fe.prey == e.idx && std::find(eatenBy.begin(), eatenBy.end(), fe.pred) == eatenBy.end()) eatenBy.push_back(fe.pred);
    }
    std::string n;
    if (!eats.empty()) n += "Hunts " + Names(g, lv, eats) + ". ";
    if (!eatenBy.empty()) n += "Hunted by " + Names(g, lv, eatenBy) + ". ";
    if (eats.empty() && S.scavenge > 0.3f) n += "Lives on the dead. ";
    unsigned t = S.traits;
    if (t & T_STRIKER) n += "Coils before it strikes - that's the tell. ";
    if (t & T_CHARGER) n += "Charges along the ground. ";
    if (t & T_DEN_AMBUSH) n += "Waits in its den with only its head out. ";
    if (t & T_CAMO) n += "Vanishes into its surroundings when it keeps still. ";
    if (t & T_MOBBER) n += "In a pack, it turns on bigger threats. ";
    if (t & T_GROOMER) n += "Cleans larger creatures, which leave it be. ";
    if (t & T_PARASITE) n += "Rides a host and changes hosts by smell. ";
    if (t & T_TRAP) n += "Never moves; anything that blunders into it is caught. ";
    if (t & T_DEFENSIVE) n += "Cornered, it fights instead of running. ";
    if (t & T_TOXIC) n += "Poisonous - its predators learn to leave it alone. ";
    if (t & T_CURL) n += "Rolls into a ball when grabbed. ";
    if (t & T_FLASH) n += "Flashes light when frightened. ";
    if (t & T_ECHO) n += "Hunts by sound; darkness doesn't hide you. ";
    if (t & T_LIGHTSEEK) n += "Drawn to light. ";
    if (t & T_ROOST) n += "Rests hanging from the ceiling. ";
    if (t & T_KLEPTO) n += "Steals other hunters' kills. ";
    if (t & T_CARRY) n += "Carries its kill away before it eats. ";
    if (t & T_GIANT) n += "Enormous. ";
    if (S.social) n += "Moves in schools or packs. ";
    if (S.lethal) n += "Deadly to touch. ";
    else if (S.diverPrey > 0.2f) n += "It will come for you. ";
    else if (S.diverFear > 0.5f) n += "Shy of divers. ";
    if (lv == PL_PIPES) n += "Pays you no mind at all. ";
    if (const char* tip = BeastTip(S.name)) n += std::string("\nCounter: ") + tip;
    return n;
}

void DrawManila(Rectangle r, bool hover) { // a file folder: a tab on top, the flap, a paper corner peeking out, a string tie
    Color card = hover ? Color{226, 196, 130, 255} : Color{208, 178, 116, 255}, dk{150, 120, 70, 255}, ink{60, 44, 30, 255};
    DrawRectangleRounded({r.x + 10, r.y, r.width * 0.4f, 16}, 0.4f, 4, card);                       // the tab
    DrawRectangleRoundedLinesEx({r.x + 10, r.y, r.width * 0.4f, 16}, 0.4f, 4, 1.5f, dk);
    DrawRectangle((int)r.x + 22, (int)r.y + 12, (int)(r.width - 44), 10, Color{240, 234, 214, 255}); // a sheet of paper inside
    DrawRectangleRounded({r.x, r.y + 12, r.width, r.height - 12}, 0.08f, 4, card);                  // the folder
    DrawRectangleRoundedLinesEx({r.x, r.y + 12, r.width, r.height - 12}, 0.08f, 4, 2, dk);
    DrawLineEx({r.x + 6, r.y + 22}, {r.x + r.width - 6, r.y + 22}, 1, dk);                         // the fold
    DrawCircle((int)(r.x + r.width - 26), (int)(r.y + r.height / 2 + 6), 5, Color{120, 40, 30, 255});   // a red wax seal
    DrawLineEx({r.x + r.width - 26, r.y + r.height / 2 + 6}, {r.x + r.width - 40, r.y + r.height - 4}, 1.5f, Color{230, 220, 190, 255}); // its string
    TxtBold("DOSSIER", r.x + 16, r.y + r.height / 2 - 2, 18, ink);
    Txt("beasts, flora, food web", r.x + 16, r.y + r.height / 2 + 18, 12, Fade(ink, 0.8f));
}

} // namespace

// A one-line "what it's for", shown over a plant or creature the first time you meet it (ScenePlatformer)
const char* BeastHint(const char* name) {
    static const struct { const char* key; const char* hint; } H[] = {
        {"Hull-Kelp", "hold on to it: turbulence can't tear you loose"}, {"Hydroids", "hit it at speed: stuns everything near - the siphon too"},
        {"Pressure-Anemone", "dash into it: its jet launches you"}, {"Rust-Algae", "rush through: a blinding cloud behind you"},
        {"Barnacle-Mites", "dash through: slick, and faster"}, {"Metal-Eater", "it eats at anything heavy on your tail"},
        {"Siphon", "its tube pulls you in - strike a hydroid to stun it"}, {"Hull-Grazer", "ride its back: the megalodon can't reach you there"},
        {"Cannon-Moss", "land on it: silent, and no stun"}, {"Ship-Rot", "strike it: a spore cloud"}, {"Mast-Kelp", "dash through: it swings back at pursuers"},
        {"Barnacle-Cluster", "walk past it - never dash into it"}, {"Lantern Weed", "its light shows the rigging-mimic"},
        {"Rigging-Mimic", "stop on the ladder before you leap"}, {"Cannoneer", "walk past its porthole - don't sprint"},
        {"Wood-Borer", "rotten planks give way behind you"}, {"Timber-Shell", "ride its shell"},
        {"Drum-Fungus", "land on it: a high bounce - and a boom"}, {"Dart Vine", "never touch it - lure a strike through it"},
        {"Razor-Palm", "jump the roots at a sprint"}, {"Idol's Bloom", "brush past: your dash is fresh again"}, {"Root-Sponge", "land on it: it bounces you back up"},
        {"Mangrove Stalker", "it snaps at the foot of drops - land moving"}, {"Goliath Island", "ride its back"}, {"Glow-Firefly", "its dust on your suit wakes the centipede"},
        {"Lantern-Shroom", "steady light: it shows the arachnid's webs"}, {"Acid-Lichen", "deadly to touch"}, {"Vibration-Spore", "strike it: deafens the stalkers"},
        {"Cave-Cabbage", "slide through: everything forgets you"}, {"Nerve-Root", "it twitches when the worm is coming"}, {"Crystal-Shelled", "walk under it: leeches can't reach you"},
        {"Echo-Stalker", "it hunts by sound - stand still"}, {"Tremor Worm", "sprinting wakes it"}, {"Glow-Shrimp", "their glow on your suit shows the webs"},
        {"Blood-Kelp", "grazed, it bleeds - and the Leviathan comes"}, {"Luminescent Anemone", "safe light: stalkers can't see you in it"},
        {"Air-Weed", "pop it: it shoots you up, and stuns the mantis"}, {"Tangle-Vine", "it drags at you - pass quickly"}, {"Spore-Pod", "a blinding cloud"},
        {"Goliath Manatee", "ride its back"}, {"Silver-Fin", "hide inside the school"}, {"Electric Ray", "never bump into it"}, {"Harpoon Mantis", "move the moment it glows"},
        {"Sun-Crystal", "its glare blinds the gargoyle-morays"}, {"Prism-Moss", "a hard landing turns into a sideways dash"}, {"Aqueduct-Vine", "swing: Up or Down turns it"},
        {"Stasis-Lily", "strike it: everything near freezes"}, {"Ruin-Spore", "strike it: the stonework comes down behind you"}, {"Crystal-Minnow", "dash through: a blinding flash"},
        {"Mosaic-Snail", "its slime makes you slick"}, {"Orichalcum", "ride it across"}, {"Phalanx", "walk in its row - never run"},
    };
    for (const auto& h : H) if (strstr(name, h.key)) return h.hint;
    return nullptr;
}

bool DossierButton(Rectangle r) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    DrawManila(r, hover);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// the open dossier, over the Periscope. Returns true while it's open (the Periscope then ignores its own clicks)
bool DrawDossier(Game& g) {
    int lv = g.dossier;
    if (lv < 0 || lv > PL_COUNT) return false;
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.55f));
    Rectangle F{40, 40, SCREEN_W - 80.0f, SCREEN_H - 80.0f};
    DrawRectangleRounded({F.x - 8, F.y - 8, F.width + 16, F.height + 16}, 0.03f, 6, Color{186, 154, 96, 255});   // the open folder
    Rectangle L{F.x + 10, F.y + 10, F.width / 2 - 16, F.height - 20}, R{F.x + F.width / 2 + 6, F.y + 10, F.width / 2 - 16, F.height - 20};
    for (Rectangle pg : {L, R}) { DrawRectangleRec(pg, Color{240, 232, 210, 255}); DrawRectangleLinesEx(pg, 1, Color{190, 170, 130, 255}); }
    for (int k = 0; k < 18; k++) DrawLineEx({L.x + 8, L.y + 60 + k * 32.0f}, {L.x + L.width - 8, L.y + 60 + k * 32.0f}, 1, Fade(Color{150, 170, 200, 255}, 0.18f)); // ruled lines
    DrawRectangle((int)(F.x + F.width / 2 - 3), (int)F.y, 6, (int)F.height, Color{150, 120, 70, 255}); // the spine crease
    Color ink{46, 34, 26, 255}, faded{120, 100, 80, 255}, red{150, 40, 30, 255};
    const char* title = lv < PL_COUNT ? PlatLevelName(lv) : "The Open Abyss";
    TxtBold(title, L.x + 20, L.y + 14, 30, ink);
    Txt("FIELD DOSSIER - NAUTILUS SURVEY", L.x + L.width - 20 - MeasureTxt("FIELD DOSSIER - NAUTILUS SURVEY", 12), L.y + 24, 12, red);
    DrawWrapped(LEVEL_STORY[lv], {L.x + 20, L.y + 58, L.width - 40, 150}, 15, ink);

    std::vector<Entry> roster = Roster(g, lv);
    int seenN = 0; for (const auto& e : roster) seenN += e.seen;

    // ---- the food web, drawn as a chart: prey below, predators above, arrows run from the eaten to the eater
    Rectangle W{L.x + 16, L.y + 196, L.width - 32, 272};
    DrawRectangleLinesEx(W, 1, Fade(ink, 0.3f));
    TxtBold("THE FOOD WEB", W.x + 8, W.y + 6, 13, red);
    if (lv < PL_COUNT) {
        int n = BeastSpeciesCount(lv);
        std::vector<int> lvl(n, 0);
        std::vector<bool> inWeb(n, false);
        for (int k = 0; k < BeastWebCount(lv); k++) { FoodEdge e = BeastWebEdge(lv, k); if (e.pred < n && e.prey < n) inWeb[e.pred] = inWeb[e.prey] = true; }
        for (int pass = 0; pass < n; pass++) // trophic height: one above the highest thing it eats (capped, so cycles settle)
            for (int k = 0; k < BeastWebCount(lv); k++) { FoodEdge e = BeastWebEdge(lv, k); if (e.pred < n && e.prey < n && e.pred != e.prey) lvl[e.pred] = std::min(4, std::max(lvl[e.pred], lvl[e.prey] + 1)); }
        std::vector<Vector2> at(n, {-1, -1});
        for (int tier = 0; tier <= 4; tier++) {
            std::vector<int> row;
            for (int s = 0; s < n; s++) if (inWeb[s] && lvl[s] == tier) row.push_back(s);
            for (size_t k = 0; k < row.size(); k++) at[row[k]] = {W.x + W.width * (k + 0.5f + (tier % 2) * 0.25f) / (row.size() + 0.5f), W.y + W.height - 28 - tier * 54.0f - (k % 2) * 12.0f}; // staggered so labels and arrows cross less
        }
        for (int k = 0; k < BeastWebCount(lv); k++) {
            FoodEdge e = BeastWebEdge(lv, k);
            if (e.pred >= n || e.prey >= n || at[e.pred].x < 0 || at[e.prey].x < 0 || e.pred == e.prey) continue;
            Vector2 a = at[e.prey], b = at[e.pred];
            Vector2 d{b.x - a.x, b.y - a.y}; float len = std::max(1.0f, sqrtf(d.x * d.x + d.y * d.y)); d = {d.x / len, d.y / len};
            Vector2 tip{b.x - d.x * 12, b.y - d.y * 12};
            DrawLineEx({a.x + d.x * 10, a.y + d.y * 10}, tip, 1.2f, Fade(ink, 0.35f + 0.4f * e.pref));
            DrawTriangle(tip, {tip.x - d.x * 7 - d.y * 4, tip.y - d.y * 7 + d.x * 4}, {tip.x - d.x * 7 + d.y * 4, tip.y - d.y * 7 - d.x * 4}, Fade(ink, 0.6f));
            DrawTriangle(tip, {tip.x - d.x * 7 + d.y * 4, tip.y - d.y * 7 - d.x * 4}, {tip.x - d.x * 7 - d.y * 4, tip.y - d.y * 7 + d.x * 4}, Fade(ink, 0.6f));
        }
        for (int s = 0; s < n; s++) {
            if (at[s].x < 0) continue;
            bool seen = s < 64 && (g.platSeen[lv] >> s & 1);
            bool pick = g.dossierPick == s;
            const SpeciesDef& S = BeastSpecies(lv, s);
            DrawCircleV(at[s], pick ? 9 : 7, S.lethal ? red : seen ? Color{90, 120, 90, 255} : faded);
            std::string lab = seen ? S.name : "?";
            if (lab.size() > 14) lab = lab.substr(0, 13) + ".";
            DrawRectangle((int)(at[s].x - MeasureTxt(lab, 11) / 2.0f - 2), (int)at[s].y + 9, MeasureTxt(lab, 11) + 4, 13, Fade(Color{240, 232, 210, 255}, 0.85f)); // paper behind the label, over the arrows
            Txt(lab, at[s].x - MeasureTxt(lab, 11) / 2.0f, at[s].y + 9, 11, seen ? ink : faded);
            if (CheckCollisionPointCircle(GetMousePosition(), at[s], 10) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g.dossierPick = s;
        }
        Txt("red: deadly to touch", W.x + W.width - 8 - MeasureTxt("red: deadly to touch", 11), W.y + 8, 11, red);
    } else {
        DrawWrapped("Down here nothing keeps to a web you can chart. Hatchetfish and krill feed everything; the hagfish follow blood; the whale-fall feeds the scavenger; and the Trench Maw eats whatever the dark sends down to it.", {W.x + 12, W.y + 34, W.width - 24, 200}, 15, ink);
    }

    // ---- the picked entry's notes
    Rectangle N{L.x + 16, L.y + 474, L.width - 32, L.height - 484};
    const Entry* pickE = nullptr;
    for (const auto& e : roster) if (e.idx == g.dossierPick) pickE = &e;
    if (pickE && pickE->seen) {
        TxtBold(pickE->name, N.x, N.y, 18, ink);
        DrawWrapped(Notes(g, lv, *pickE), {N.x, N.y + 24, N.width, N.height - 24}, 14, ink);
    } else if (pickE) {
        TxtBold("Not yet seen", N.x, N.y, 18, faded);
        DrawWrapped("Nothing on file. Something lives in this part of the web - dive, and keep your eyes open.", {N.x, N.y + 24, N.width, N.height - 24}, 14, faded);
    } else DrawWrapped("Pick an entry - on the chart or in the roster - to read the notes on it.", {N.x, N.y + 6, N.width, N.height}, 14, faded);

    // ---- the roster: beasts, then flora
    TxtBold("ROSTER", R.x + 20, R.y + 16, 22, ink);
    Txt(TextFormat("%d of %d on file", seenN, (int)roster.size()), R.x + R.width - 20 - MeasureTxt(TextFormat("%d of %d on file", seenN, (int)roster.size()), 14), R.y + 22, 14, faded);
    float y = R.y + 52;
    for (int pass = 0; pass < 2; pass++) {
        TxtBold(pass == 0 ? "Beasts" : "Flora", R.x + 20, y, 15, red);
        y += 22;
        for (const auto& e : roster) {
            if (e.flora != (pass == 1)) continue;
            Rectangle row{R.x + 14, y - 2, R.width - 28, 20};
            bool hover = CheckCollisionPointRec(GetMousePosition(), row);
            if (g.dossierPick == e.idx) DrawRectangleRec(row, Fade(Color{220, 190, 120, 255}, 0.5f));
            else if (hover) DrawRectangleRec(row, Fade(Color{220, 190, 120, 255}, 0.25f));
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g.dossierPick = e.idx;
            Txt(e.seen ? e.name : "- unseen -", R.x + 26, y, 14, e.seen ? ink : faded);
            if (e.seen && lv < PL_COUNT && !e.flora && BeastSpecies(lv, e.idx).lethal) Txt("deadly", R.x + R.width - 30 - MeasureTxt("deadly", 12), y + 1, 12, red);
            y += 20;
        }
        y += 8;
    }
    if (Button({R.x + R.width - 150, R.y + R.height - 46, 136, 36}, "Close", true, 17) || IsKeyPressed(KEY_ESCAPE)) { g.dossier = -1; g.dossierPick = -1; }
    return true;
}
