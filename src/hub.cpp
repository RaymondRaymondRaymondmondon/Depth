// ============================================================================
//  DEPTH - the station screens you open from the Nautilus's salon (salon.cpp).
// ============================================================================
#include "game.h"
#include <algorithm>
#include <cmath>

// ============================================================ helm
void SceneHelm(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Helm", "Choose a destination for the expedition");
    DrawGoldBadge(g);

    struct Loc { const char* name; const char* desc; bool open; Color col; };
    const Loc locs[4] = {
        {"The Cave", "An undersea cave crawling with oversized crustaceans, sea bugs, and worms. Mini boss: the Lobster.", true, Color{60, 120, 140, 255}},
        {"The Island", "Sun-baked shores ruled by the Sun God and the Coconut Queen.", false, Color{200, 160, 80, 255}},
        {"The Weeds", "A kelp forest of merfolk, octopi, and barracuda. Neptune waits within.", false, Color{70, 140, 80, 255}},
        {"Atlantis", "A sunken city of lost ones who still worship something ancient.", false, Color{110, 90, 150, 255}},
    };
    int partyCount = 0;
    for (int id : g.party) if (id >= 0) partyCount++;

    for (int i = 0; i < 4; i++) {
        Rectangle c{50 + i * 300.0f, 100, 280, 330};
        Panel(c, locs[i].open ? Pal::Paper : Color{176, 168, 150, 255});
        DrawVGradient({c.x + 12, c.y + 12, c.width - 24, 90}, ColorBrightness(locs[i].col, 0.15f), ColorBrightness(locs[i].col, -0.35f));
        DrawTextCenteredBold(locs[i].name, c.x + c.width / 2 + 1, c.y + 40, 30, Fade(BLACK, 0.5f));
        DrawTextCenteredBold(locs[i].name, c.x + c.width / 2, c.y + 38, 30, Pal::Paper);
        DrawWrapped(locs[i].desc, {c.x + 16, c.y + 112, c.width - 32, 80}, 16, Pal::Ink);
        if (locs[i].open) {
            // cave levels 0, 1, 3, 5 and 6: beat one to unlock the next; earlier ones stay open
            int unlocked = std::min(CAVE_TIERS - 1, g.caveTierCleared + 1);
            g.caveTier = std::clamp(g.caveTier, 0, unlocked);
            for (int k = 0; k < CAVE_TIERS; k++) {
                Rectangle chip{c.x + 16 + k * 50.0f, c.y + 196, 44, 30};
                bool open = k <= unlocked, sel = k == g.caveTier;
                if (sel) DrawRectangleRounded({chip.x - 3, chip.y - 3, chip.width + 6, chip.height + 6}, 0.4f, 6, Pal::Teal);
                if (Button(chip, open ? TextFormat("Lv %d", CAVE_TIER_LEVEL[k]) : "?", open, 14)) g.caveTier = k;
                if (k <= g.caveTierCleared) DrawCircle((int)(chip.x + chip.width - 4), (int)chip.y + 4, 4, Pal::Good);
            }
            int lvl = CAVE_TIER_LEVEL[g.caveTier], rooms = lvl >= 3 ? 4 : 3;
            TxtBold(TextFormat("%s  (level %d)", CAVE_TIER_NAME[g.caveTier], lvl), c.x + 16, c.y + 232, 16, Pal::Ink);
            Txt(TextFormat("%d rooms + the Lobster.  Loot x%.1f", rooms, 1.0f + 0.35f * lvl), c.x + 16, c.y + 253, 14, Pal::BrassDk);
            if (Button({c.x + 20, c.y + 276, c.width - 40, 42}, "Embark", partyCount > 0)) {
                StartDungeon(g);
                return;
            }
        } else {
            DrawTextCentered("Not yet charted", c.x + c.width / 2, c.y + 280, 21, Pal::BrassDk);
        }
    }

    Panel({50, 450, 880, 250});
    TxtBold("Expedition party", 70, 464, 23, Pal::Ink);
    bool anyEmptyLoadout = false;
    for (int k = 0; k < PARTY_SIZE; k++) {
        Hero* h = FindHero(g, g.party[k]);
        float x = 70 + k * 215.0f;
        Txt(TextFormat("Rank %d", k + 1), x, 502, 15, Pal::BrassDk);
        if (!h) { Txt("- empty -", x, 526, 19, Pal::BrassDk); continue; }
        if (LoadoutCount(*h) < LOADOUT_SIZE) anyEmptyLoadout = true;
        Stats s = GetStats(*h);
        TxtBold(h->name, x, 524, 21, Pal::Ink);
        Txt(TextFormat("%s  Lv %d", ClassName(h->cls), h->level), x, 550, 16, Pal::BrassDk);
        Txt(TextFormat("HP %d/%d", h->hp, s.maxHp), x, 576, 15, Pal::Ink);
        DrawBar({x, 596, 170, 8}, (float)h->hp / s.maxHp, Pal::Good);
        Txt(TextFormat("Nerves %d/100%s", h->stress, h->rattled ? "  RATTLED" : ""), x, 610, 15, Pal::Ink);
        DrawBar({x, 630, 170, 6}, h->stress / 100.0f, Pal::Stress);
    }
    if (partyCount < 4)
        Txt("Tip: a full party of four is strongly recommended. Visit Crew Quarters.", 70, 660, 17, Pal::Coral);
    else if (anyEmptyLoadout)
        Txt("Someone has an empty ability slot. Fill it in Crew Quarters.", 70, 660, 17, Pal::Coral);
    else
        Txt("Change the marching order and abilities in Crew Quarters. Rank 1 is the front line.", 70, 660, 17, Pal::BrassDk);

    Panel({950, 450, 280, 250});
    TxtBold("Provisions", 970, 464, 23, Pal::Ink);
    DrawWrapped(TextFormat("Each room ahead drains %d light. Batteries recharge it by 40.", LightDrainPerRoom(g)), {970, 500, 240, 80}, 16, Pal::Ink);
    Txt(TextFormat("Batteries: %d", g.batteries), 970, 580, 21, Pal::Ink);
    if (Button({970, 620, 240, 44}, "Buy battery (15g)", g.gold >= 15)) {
        g.gold -= 15;
        g.batteries++;
    }
}

// ============================================================ crew quarters
static void DrawTooltip(const std::string& text, Vector2 at) {
    const float w = 300;
    int lines = 1 + MeasureTxt(text, 15) / (int)(w - 24);
    Rectangle r{std::min(at.x + 16, SCREEN_W - w - 10), at.y + 18, w, 20.0f * lines + 18};
    if (r.y + r.height > SCREEN_H - 6) r.y = at.y - r.height - 8;
    DrawRectangleRounded({r.x + 3, r.y + 4, r.width, r.height}, 0.15f, 6, Fade(BLACK, 0.4f));
    DrawRectangleRounded(r, 0.15f, 6, Color{16, 24, 28, 245});
    DrawRectangleRoundedLinesEx(r, 0.15f, 6, 1.5f, Pal::BrassDk);
    DrawWrapped(text, {r.x + 12, r.y + 9, r.width - 24, r.height}, 15, Pal::Paper);
}

void SceneCrew(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("Crew Quarters", "Choose your party, set the marching order, pick abilities, and fit relics");
    DrawGoldBadge(g);
    if (!FindHero(g, g.selectedHero)) g.selectedHero = g.roster.empty() ? -1 : g.roster[0].id;
    Vector2 m = GetMousePosition();
    std::string tooltip;

    // --- roster
    Panel({20, 90, 300, 612});
    TxtBold(TextFormat("Roster  %d/%d", (int)g.roster.size(), MaxRoster(g)), 36, 102, 21, Pal::Ink);
    float rowH = std::min(70.0f, 550.0f / std::max(1, (int)g.roster.size()));
    for (size_t i = 0; i < g.roster.size(); i++) {
        Hero& h = g.roster[i];
        Rectangle r{32, 134 + i * rowH, 276, rowH - 6};
        bool sel = h.id == g.selectedHero, hover = CheckCollisionPointRec(m, r);
        DrawRectangleRounded(r, 0.15f, 6, sel ? Color{252, 242, 214, 255} : hover ? Color{236, 222, 190, 255} : Color{222, 206, 170, 255});
        if (sel) DrawRectangleRoundedLinesEx(r, 0.15f, 6, 2, Pal::BrassDk);
        DrawRectangle((int)r.x, (int)r.y + 6, 6, (int)r.height - 12, ClassColor(h.cls));
        TxtBold(h.name, r.x + 14, r.y + 6, 18, Pal::Ink);
        Txt(TextFormat("%s  Lv %d", ClassName(h.cls), h.level), r.x + 14, r.y + r.height - 24, 15, Pal::BrassDk);
        Stats s = GetStats(h);
        DrawBar({r.x + 150, r.y + r.height - 22, 116, 7}, (float)h.hp / s.maxHp, Pal::Good);
        DrawBar({r.x + 150, r.y + r.height - 11, 116, 5}, h.stress / 100.0f, Pal::Stress);
        const char* tag = InParty(g, h.id) ? "IN PARTY" : h.onLeave ? "ON LEAVE" : h.rattled ? "RATTLED" : "";
        TxtBold(tag, r.x + r.width - 8 - MeasureTxt(tag, 12, true), r.y + 8, 12,
                h.onLeave ? Pal::Stress : h.rattled ? Pal::Bad : Pal::Copper);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { g.selectedHero = h.id; g.dismissArmed = -1; }
    }

    // --- party slots
    Panel({340, 90, 600, 145});
    TxtBold("Expedition party   (Rank 1 = front line)", 356, 100, 18, Pal::Ink);
    for (int k = 0; k < PARTY_SIZE; k++) {
        Rectangle s{352 + k * 146.0f, 128, 136, 98};
        DrawRectangleRounded(s, 0.12f, 6, Color{214, 196, 158, 255});
        Txt(TextFormat("Rank %d", k + 1), s.x + 8, s.y + 6, 13, Pal::BrassDk);
        Hero* h = FindHero(g, g.party[k]);
        if (!h) { Txt("- empty -", s.x + 8, s.y + 30, 17, Pal::BrassDk); continue; }
        TxtBold(h->name, s.x + 8, s.y + 22, 17, Pal::Ink);
        Txt(ClassName(h->cls), s.x + 8, s.y + 42, 15, Pal::BrassDk);
        if (Button({s.x + 4, s.y + 64, 40, 28}, "<", k > 0)) std::swap(g.party[k], g.party[k - 1]);
        if (Button({s.x + 48, s.y + 64, 40, 28}, "x")) { g.party[k] = -1; CompactParty(g); }
        if (Button({s.x + 92, s.y + 64, 40, 28}, ">", k < 3 && g.party[k + 1] >= 0)) std::swap(g.party[k], g.party[k + 1]);
    }

    // --- details of the selected hero
    Panel({340, 245, 600, 457});
    Hero* sel = FindHero(g, g.selectedHero);
    if (sel) {
        Hero& h = *sel;
        Stats s = GetStats(h);
        TxtBold(h.name, 356, 254, 26, Pal::Ink);
        Txt(TextFormat("%s  -  Level %d", ClassName(h.cls), h.level), 356 + MeasureTxt(h.name, 26, true) + 16, 262, 18, Pal::BrassDk);
        DrawWrapped(ClassBlurb(h.cls), {356, 288, 570, 40}, 15, Pal::Ink);
        int next = XpForNextLevel(h);
        const std::string lines[8] = {
            TextFormat("HP  %d / %d%s", h.hp, s.maxHp, h.deathsDoor ? "  (Death's Door)" : ""),
            TextFormat("Nerves  %d / 100%s", h.stress, h.rattled ? "  RATTLED" : ""),
            TextFormat("Damage  %d - %d", s.dmgMin, s.dmgMax),
            next < 0 ? std::string("XP  max level") : std::string(TextFormat("XP  %d / %d", h.xp, next)),
            TextFormat("Accuracy  %d", s.acc),
            TextFormat("Dodge  %d", s.dodge),
            TextFormat("Protection  %d%%", s.prot),
            TextFormat("Speed  %d", s.speed),
        };
        for (int i = 0; i < 8; i++) Txt(lines[i], 356 + (i / 4) * 290.0f, 330 + (i % 4) * 21.0f, 16, Pal::Ink);

        // abilities: 8 per class, 4 slotted
        const auto& abs = ClassAbilities(h.cls);
        TxtBold(TextFormat("Abilities  %d/%d slotted", LoadoutCount(h), LOADOUT_SIZE), 356, 418, 16, Pal::Ink);
        Txt("Click to slot or unslot. New ones unlock as the crew member levels up.", 560, 420, 12, Pal::BrassDk);
        for (int i = 0; i < (int)abs.size(); i++) {
            const Ability& a = abs[i];
            Rectangle c{352 + (i % 2) * 292.0f, 442 + (i / 2) * 41.0f, 284, 37};
            int slot = -1;
            for (int k = 0; k < LOADOUT_SIZE; k++) if (h.loadout[k] == i) slot = k;
            bool locked = h.level < a.unlockLevel, hov = CheckCollisionPointRec(m, c);
            Color bg = slot >= 0 ? Color{196, 226, 212, 255} : locked ? Color{186, 178, 164, 255} : Color{226, 212, 178, 255};
            if (hov && !locked) bg = ColorBrightness(bg, 0.08f);
            DrawRectangleRounded(c, 0.25f, 6, bg);
            if (slot >= 0) DrawRectangleRoundedLinesEx(c, 0.25f, 6, 2, Color{40, 130, 120, 255});
            TxtBold(a.name, c.x + 9, c.y + 3, 15, locked ? Color{120, 112, 100, 255} : Pal::Ink);
            std::string where = "ranks " + RankString(a.usableFrom);
            if (a.target == Target::Enemy) where += "  >  " + RankString(a.hits);
            else where += a.target == Target::Self ? "  >  self" : a.target == Target::AllAllies ? "  >  party" : "  >  ally";
            Txt(where, c.x + 9, c.y + 20, 12, Pal::BrassDk);
            const char* tag = locked ? TextFormat("Lv %d", a.unlockLevel) : slot >= 0 ? "SLOTTED" : "";
            TxtBold(tag, c.x + c.width - 10 - MeasureTxt(tag, 11, true), c.y + 12, 11, locked ? Pal::BrassDk : Color{40, 130, 120, 255});
            if (!hov) continue;
            tooltip = a.desc;
            if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) continue;
            if (locked) Toast(g, TextFormat("%s unlocks at level %d.", a.name.c_str(), a.unlockLevel));
            else if (slot >= 0) h.loadout[slot] = -1;
            else {
                int free = -1;
                for (int k = LOADOUT_SIZE - 1; k >= 0; k--) if (h.loadout[k] < 0) free = k;
                if (free >= 0) h.loadout[free] = i;
                else Toast(g, "All four slots are full. Unslot an ability first.");
            }
        }

        for (int k = 0; k < 2; k++) {
            Rectangle rs{352 + k * 204.0f, 612, 196, 40};
            DrawRectangleRounded(rs, 0.2f, 6, Color{214, 196, 158, 255});
            if (h.relics[k] >= 0) {
                const RelicDef& rd = Relics()[h.relics[k]];
                TxtBold(rd.name, rs.x + 8, rs.y + 3, 15, Pal::Ink);
                Txt(rd.desc, rs.x + 8, rs.y + 22, 12, Pal::BrassDk);
                if (Button({rs.x + rs.width - 34, rs.y + 6, 28, 28}, "x")) {
                    g.relicStorage.push_back(h.relics[k]);
                    h.hp = std::max(1, h.hp - rd.hp);
                    h.relics[k] = -1;
                    h.hp = std::min(h.hp, GetStats(h).maxHp);
                }
            } else {
                Txt("Empty relic slot", rs.x + 10, rs.y + 12, 15, Pal::BrassDk);
            }
        }

        bool inParty = InParty(g, h.id);
        int partyCount = 0;
        for (int id : g.party) if (id >= 0) partyCount++;
        if (inParty) {
            if (Button({352, 660, 190, 34}, "Remove from party")) {
                for (auto& id : g.party) if (id == h.id) id = -1;
                CompactParty(g);
            }
        } else if (Button({352, 660, 190, 34}, h.onLeave ? "On sick leave" : "Add to party", h.onLeave == 0 && partyCount < PARTY_SIZE)) {
            CompactParty(g);
            for (auto& id : g.party) if (id < 0) { id = h.id; break; }
        }

        bool armed = g.dismissArmed == h.id;
        if (Button({760, 660, 170, 34}, armed ? "Click to confirm" : "Dismiss", g.roster.size() > 1)) {
            if (!armed) {
                g.dismissArmed = h.id;
            } else {
                for (int r : h.relics) if (r >= 0) g.relicStorage.push_back(r);
                for (auto& id : g.party) if (id == h.id) id = -1;
                std::string name = h.name;
                int id = h.id;
                g.roster.erase(std::remove_if(g.roster.begin(), g.roster.end(), [id](const Hero& x) { return x.id == id; }), g.roster.end());
                CompactParty(g);
                g.selectedHero = -1;
                g.dismissArmed = -1;
                Toast(g, name + " has left the Nautilus. Their relics went to storage.");
                return;
            }
        }
    }

    // --- relic storage
    Panel({960, 90, 300, 612});
    TxtBold("Relic storage", 976, 102, 21, Pal::Ink);
    Txt("Two relics per crew member", 976, 128, 13, Pal::BrassDk);
    int n = (int)g.relicStorage.size();
    if (CheckCollisionPointRec(m, {960, 90, 300, 612})) g.relicScroll -= (int)GetMouseWheelMove();
    g.relicScroll = std::clamp(g.relicScroll, 0, std::max(0, n - 10));
    bool canEquip = sel && (sel->relics[0] < 0 || sel->relics[1] < 0);
    if (n == 0) Txt("Empty. Finish expeditions to find relics.", 976, 160, 14, Pal::BrassDk);
    for (int i = g.relicScroll; i < n && i < g.relicScroll + 10; i++) {
        int rid = g.relicStorage[i];
        const RelicDef& rd = Relics()[rid];
        Rectangle r{972, 150 + (i - g.relicScroll) * 54.0f, 276, 48};
        DrawRectangleRounded(r, 0.2f, 6, Color{222, 206, 170, 255});
        TxtBold(rd.name, r.x + 8, r.y + 5, 16, Pal::Ink);
        Txt(rd.desc, r.x + 8, r.y + 27, 12, Pal::BrassDk);
        if (Button({r.x + r.width - 70, r.y + 9, 62, 30}, "Equip", canEquip)) {
            int slot = sel->relics[0] < 0 ? 0 : 1;
            sel->relics[slot] = rid;
            sel->hp += rd.hp;
            g.relicStorage.erase(g.relicStorage.begin() + i);
            break;
        }
    }
    if (n > 10) Txt("Scroll for more", 976, 690, 13, Pal::BrassDk);
    if (!tooltip.empty()) DrawTooltip(tooltip, m);
}

// ============================================================ radar
void SceneRadar(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("Radar Room", "Recruits signal from passing ships; salvagers sell relics");
    DrawGoldBadge(g);

    Panel({30, 90, 620, 612});
    TxtBold("Incoming signals: recruits (100g each)", 50, 104, 21, Pal::Ink);
    int n = (int)g.recruits.size();
    float step = std::min(150.0f, 480.0f / std::max(1, n)), cardH = step - 10;
    for (int i = 0; i < n; i++) {
        Hero& h = g.recruits[i];
        Rectangle c{48, 140 + i * step, 584, cardH};
        DrawRectangleRounded(c, 0.08f, 6, Color{222, 206, 170, 255});
        DrawRectangle((int)c.x, (int)c.y + 10, 8, (int)c.height - 20, ClassColor(h.cls));
        TxtBold(h.name, c.x + 20, c.y + 8, 22, Pal::Ink);
        Txt(ClassName(h.cls), c.x + 30 + MeasureTxt(h.name, 22, true), c.y + 13, 17, Pal::BrassDk);
        if (cardH >= 120) DrawWrapped(ClassBlurb(h.cls), {c.x + 20, c.y + 42, 400, 60}, 15, Pal::Ink);
        Stats s = GetStats(h);
        Txt(TextFormat("HP %d   Dmg %d-%d   Spd %d   Dodge %d   Prot %d", s.maxHp, s.dmgMin, s.dmgMax, s.speed, s.dodge, s.prot),
            c.x + 20, c.y + cardH - 28, 14, Pal::BrassDk);
        bool ok = g.gold >= 100 && (int)g.roster.size() < MaxRoster(g);
        if (Button({c.x + c.width - 140, c.y + cardH / 2 - 22, 124, 44}, "Hire", ok)) {
            g.gold -= 100;
            g.roster.push_back(h);
            Toast(g, h.name + " the " + ClassName(h.cls) + " joins the crew!");
            g.recruits.erase(g.recruits.begin() + i);
            break;
        }
    }
    if ((int)g.roster.size() >= MaxRoster(g))
        Txt(TextFormat("Crew quarters are full (%d). Dismiss someone, or extend the bunks in the Workshop.", MaxRoster(g)), 50, 612, 15, Pal::Coral);
    if (Button({48, 640, 300, 44}, TextFormat("Scan for new signals (%dg)", ScanCost(g)), g.gold >= ScanCost(g))) {
        g.gold -= ScanCost(g);
        g.recruits.clear();
        for (int i = 0; i < RecruitsPerScan(g); i++) g.recruits.push_back(MakeRandomHero(g));
    }

    Panel({670, 90, 580, 612});
    TxtBold("Salvage market: relics", 690, 104, 21, Pal::Ink);
    for (size_t i = 0; i < g.shopRelics.size(); i++) {
        const RelicDef& rd = Relics()[g.shopRelics[i]];
        Rectangle c{688, 140 + i * 100.0f, 544, 88};
        DrawRectangleRounded(c, 0.1f, 6, Color{222, 206, 170, 255});
        TxtBold(rd.name, c.x + 16, c.y + 12, 22, Pal::Ink);
        Txt(rd.desc, c.x + 16, c.y + 46, 17, Pal::BrassDk);
        if (Button({c.x + c.width - 150, c.y + 22, 134, 44}, TextFormat("Buy %dg", rd.price), g.gold >= rd.price)) {
            g.gold -= rd.price;
            g.relicStorage.push_back(g.shopRelics[i]);
            Toast(g, "Bought the " + rd.name + ". It's waiting in Crew Quarters.");
            g.shopRelics.erase(g.shopRelics.begin() + i);
            break;
        }
    }
    Rectangle b{688, 450, 544, 88};
    DrawRectangleRounded(b, 0.1f, 6, Color{222, 206, 170, 255});
    TxtBold("Flashlight battery", b.x + 16, b.y + 12, 22, Pal::Ink);
    Txt(TextFormat("+40 light on an expedition. You have %d.", g.batteries), b.x + 16, b.y + 46, 17, Pal::BrassDk);
    if (Button({b.x + b.width - 150, b.y + 22, 134, 44}, "Buy 15g", g.gold >= 15)) { g.gold -= 15; g.batteries++; }
    DrawWrapped("Stock refreshes after every expedition. Hull upgrades, bigger bunks and a better sonar array are built in the Workshop.",
                {690, 560, 540, 80}, 15, Pal::BrassDk);
}

// ============================================================ workshop
void SceneWorkshop(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Workshop", "Spend gold on lasting upgrades to the Nautilus");
    DrawGoldBadge(g);
    const char* flavor[UP_COUNT] = {
        "Polished mirrors and a stronger bulb. The flashlight lasts longer in the dark.",
        "Hammocks strung between the pipes. Room for more crew aboard.",
        "A bigger ear on the hull. More recruit signals, and cheaper scans.",
        "Better instruments and a steadier surgeon's hand. The Ward charges less.",
    };
    for (int u = 0; u < UP_COUNT; u++) {
        Rectangle c{70 + (u % 2) * 580.0f, 100 + (u / 2) * 290.0f, 560, 270};
        Panel(c);
        int lv = g.upgrades[u];
        TxtBold(UpgradeName(u), c.x + 24, c.y + 20, 26, Pal::Ink);
        for (int k = 0; k < UPGRADE_MAX; k++) {
            Vector2 p{c.x + c.width - 110 + k * 30.0f, c.y + 36};
            DrawCircleV(p, 10, Pal::BrassDk);
            DrawCircleV(p, 7, k < lv ? Pal::Brass : Color{90, 80, 64, 255});
        }
        DrawWrapped(flavor[u], {c.x + 24, c.y + 62, c.width - 48, 50}, 16, Pal::Ink);
        TxtBold("Now:", c.x + 24, c.y + 120, 16, Pal::BrassDk);
        Txt(UpgradeDesc(u, lv), c.x + 80, c.y + 120, 16, Pal::Ink);
        if (lv < UPGRADE_MAX) {
            TxtBold("Next:", c.x + 24, c.y + 146, 16, Pal::BrassDk);
            Txt(UpgradeDesc(u, lv + 1), c.x + 80, c.y + 146, 16, Color{30, 110, 90, 255});
            int price = UpgradePrice(lv + 1);
            if (Button({c.x + 24, c.y + 196, 260, 48}, TextFormat("Build level %d  (%dg)", lv + 1, price), g.gold >= price)) {
                g.gold -= price;
                g.upgrades[u]++;
                Toast(g, TextFormat("%s upgraded to level %d.", UpgradeName(u), g.upgrades[u]));
            }
        } else {
            TxtBold("Fully upgraded.", c.x + 24, c.y + 206, 20, Color{30, 110, 90, 255});
        }
    }
}

// ============================================================ ward
void SceneWard(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    int perHp = WardCostPerHp(g);
    DrawSceneTitle("The Ward", TextFormat("Injured crew can be patched up here (%d gold per HP)", perHp));
    DrawGoldBadge(g);
    int shown = 0, totalCost = 0;
    for (auto& h : g.roster) {
        Stats s = GetStats(h);
        int missing = s.maxHp - h.hp;
        if (missing <= 0) continue;
        int cost = missing * perHp;
        totalCost += cost;
        Rectangle c{140 + (shown % 2) * 510.0f, 110 + (shown / 2) * 120.0f, 490, 104};
        Panel(c);
        TxtBold(h.name, c.x + 18, c.y + 14, 22, Pal::Ink);
        Txt(ClassName(h.cls), c.x + 28 + MeasureTxt(h.name, 22, true), c.y + 19, 17, Pal::BrassDk);
        Txt(TextFormat("HP %d / %d", h.hp, s.maxHp), c.x + 18, c.y + 50, 19, Pal::Ink);
        DrawBar({c.x + 18, c.y + 76, 240, 10}, (float)h.hp / s.maxHp, Pal::Good);
        if (Button({c.x + c.width - 170, c.y + 30, 150, 44}, TextFormat("Treat %dg", cost), g.gold >= cost)) {
            g.gold -= cost;
            h.hp = s.maxHp;
        }
        shown++;
    }
    if (shown == 0) {
        const char* msg = "Everyone is shipshape. No patients today.";
        TxtShadow(msg, SCREEN_W / 2.0f - MeasureTxt(msg, 28, true) / 2.0f, 320, 28, Pal::Paper, true);
    } else if (shown > 1 && Button({SCREEN_W / 2.0f - 150, 640, 300, 48}, TextFormat("Treat everyone (%dg)", totalCost), g.gold >= totalCost)) {
        g.gold -= totalCost;
        for (auto& h : g.roster) h.hp = GetStats(h).maxHp;
    }
}

// ============================================================ sick leave
void SceneSickLeave(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("Sick Bay", "Rest clears all stress and cures Rattled, but they sit out the next expedition");
    DrawGoldBadge(g);
    int shown = 0;
    for (auto& h : g.roster) {
        if (h.stress <= 0 && !h.rattled && h.onLeave == 0) continue;
        Rectangle c{140 + (shown % 2) * 510.0f, 110 + (shown / 2) * 120.0f, 490, 104};
        Panel(c);
        TxtBold(h.name, c.x + 18, c.y + 14, 22, Pal::Ink);
        Txt(ClassName(h.cls), c.x + 28 + MeasureTxt(h.name, 22, true), c.y + 19, 17, Pal::BrassDk);
        if (h.onLeave) {
            Txt("Resting. Back after the next expedition.", c.x + 18, c.y + 56, 17, Pal::Stress);
        } else {
            Txt(TextFormat("Nerves %d / 100%s", h.stress, h.rattled ? "   RATTLED" : ""), c.x + 18, c.y + 50, 19, h.rattled ? Pal::Bad : Pal::Ink);
            DrawBar({c.x + 18, c.y + 76, 240, 10}, h.stress / 100.0f, Pal::Stress);
            int cost = std::max(10, h.stress + (h.rattled ? 40 : 0));
            if (Button({c.x + c.width - 190, c.y + 30, 170, 44}, TextFormat("Grant leave %dg", cost), g.gold >= cost)) {
                g.gold -= cost;
                h.stress = 0;
                h.rattled = false;
                h.onLeave = 1;
                for (auto& id : g.party) if (id == h.id) id = -1;
                CompactParty(g);
                Toast(g, h.name + " is on sick leave and will miss the next expedition.");
            }
        }
        shown++;
    }
    if (shown == 0) {
        const char* msg = "Nerves of steel all round. Nobody needs leave.";
        TxtShadow(msg, SCREEN_W / 2.0f - MeasureTxt(msg, 28, true) / 2.0f, 320, 28, Pal::Paper, true);
    }
}

// ============================================================ library
void SceneBookshelf(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Library", "Everything the crew has learned about the deep");
    const char* tabs[] = {"Crew", "Conditions", "Light & Nerves", "Bestiary", "Platforming"};
    for (int i = 0; i < 5; i++) {
        Rectangle r{140 + i * 205.0f, 92, 195, 42};
        if (i == g.bookTab) DrawRectangleRounded({r.x - 3, r.y - 3, r.width + 6, r.height + 6}, 0.3f, 6, Pal::Teal);
        if (Button(r, tabs[i])) g.bookTab = i;
    }
    Rectangle page{80, 150, SCREEN_W - 160.0f, 550};
    Panel(page);
    Rectangle body{page.x + 30, page.y + 24, page.width - 60, page.height - 40};

    if (g.bookTab == 0) {
        for (int c = 0; c < (int)HeroClass::COUNT; c++) {
            float y = body.y + c * 128.0f;
            HeroClass hc = (HeroClass)c;
            DrawRectangle((int)body.x, (int)y + 4, 8, 110, ClassColor(hc));
            TxtBold(ClassName(hc), body.x + 20, y, 24, Pal::Ink);
            DrawWrapped(ClassBlurb(hc), {body.x + 20, y + 32, body.width - 20, 40}, 16, Pal::Ink);
            std::string ab;
            for (auto& a : ClassAbilities(hc)) ab += a.name + (a.unlockLevel ? TextFormat(" (Lv %d)", a.unlockLevel) : "") + "  |  ";
            DrawWrapped(ab.substr(0, ab.size() - 5), {body.x + 20, y + 72, body.width - 20, 40}, 14, Pal::Copper);
        }
    } else if (g.bookTab == 1) {
        DrawWrapped(
            "BLEED: loses health at the start of each turn for 3 turns.\n\n"
            "POISON: loses health each turn for 3 turns. Poison STACKS (up to three doses), and a poisoned crew member only gets half the benefit of healing.\n\n"
            "STUN: loses their next turn.        MARKED: takes 25% more damage for 3 turns.\n\n"
            "RALLIED: deals extra damage for a few turns.        DODGE+ / ARMOR+: harder to hit or hurt for 3 turns.\n\n"
            "GUARDING: enemies must attack this crew member, who takes reduced damage.\n\n"
            "DEATH'S DOOR: a crew member at 0 HP is still standing, but every further hit has a chance to finish them. Any healing pulls them back.\n\n"
            "RATTLED: a crew member whose Nerves reach 100 is Rattled: less accurate, easier to hit, and may freeze up in combat. Only the Sick Bay cures it.",
            body, 17, Pal::Ink);
    } else if (g.bookTab == 2) {
        DrawWrapped(
            "THE FLASHLIGHT\nEvery room you advance into drains the flashlight (a better reflector from the Workshop slows this). Swap in a battery to recharge it.\n\n"
            "Bright (75+): your crew aim better.\n"
            "Dim (50-74): normal conditions.\n"
            "Murky (1-49): enemies hit more often and your crew's nerves fray faster, but the loot is 40% richer.\n"
            "Pitch Black (0): enemies are deadly accurate and stress piles up quickly, but loot nearly doubles.\n\n"
            "NERVES\nScary attacks, critical hits and falling to Death's Door all add stress. The Nurse's Smelling Salts and the Captain's orders bring it down, and a critical hit by your own crew lifts everyone's spirits.\n"
            "Retreating from an expedition adds stress to the whole party.",
            body, 18, Pal::Ink);
    } else if (g.bookTab == 3) {
        DrawWrapped(
            "THE CAVE (Shallows)\n\n"
            "SEA LOUSE: fast, fragile nibblers. Their bites can cause bleeding.\n\n"
            "PISTOL SHRIMP: tough little shells. Their claw can stun, and their sonic pop rattles nerves from any range.\n\n"
            "BRINE WORM: slow, but spits poison at any rank. Its poison stacks, so cure it early.\n\n"
            "THE LOBSTER (mini boss): heavily armored. Crushing Claw can stun, Tail Sweep hits your front two ranks, and its clacking frays everyone's nerves. "
            "Bleed and poison ignore its armor, and marking it helps everyone hit harder.\n\n"
            "Still uncharted: the Island, the Weeds, and Atlantis.",
            body, 18, Pal::Ink);
    } else {
        DrawWrapped(
            "Platforming runs are about the jumps. Hold jump for a higher leap, tap it for a short hop. Push against a wall in mid-air to slide down it, "
            "and jump to kick off it: two walls close together can be climbed. Falling, steam, spikes and gears send you back to the last checkpoint "
            "(every section is one). Your crew never gets hurt here.\n\n"
            "THE PIPES (easy): no enemies at all, just gears, vents, timed jets and chimneys.\n\n"
            "THE HULL (medium): crabs, leaping eels, urchins and mines. Every creature is deadly to touch. The Kraken waits at the end: dodge its "
            "tentacles and stomp its head three times for a relic. Or just run for the airlock.\n\n"
            "THE PIRATE SHIP (hard): the longest leaps, pirates, parakeets and fire vents. Stomp Blackbeard three times to claim the treasure.\n\n"
            "Clearing a level unlocks the next one and reshuffles its layout. Controls: A/D or arrows, Space/W/Up to jump, Esc to give up. A gamepad works too.",
            body, 17, Pal::Ink);
    }
}

// ============================================================ periscope
void ScenePeriscope(Game& g) {
    DrawCabinBackground();
    if (BackButton(g)) return;
    DrawSceneTitle("The Periscope", "Platforming runs: your crew stays safe, and there's gold to be had");
    DrawGoldBadge(g);
    struct Lvl { const char* diff; const char* desc; const char* reward; };
    const Lvl lv[PL_COUNT] = {
        {"FIRST DIVE", "A long crawl through the Nautilus's steam pipes. No enemies, just hard jumps: steam vents, a shaft to plunge down, and chimneys to wall-jump up.",
         "2 gold per coin, +30 at the valve"},
        {"SECOND DIVE", "Out along the Nautilus's hull, following her ribs and rails deeper toward open water where the Kraken lairs. Crabs, leaping eels, urchins and mines; one touch is fatal.",
         "4 gold per coin, +90"},
        {"THIRD DIVE", "Board a pirate ship: across the deck, down the hatch into the hold, up the companionway to the captain's cabin. Pirates burst out of doors and shoot from cover.",
         "6 gold per coin, +160"},
    };
    for (int i = 0; i < PL_COUNT; i++) {
        bool open = i == 0 || g.platCleared[i - 1];
        Rectangle c{60 + i * 400.0f, 110, 370, 510};
        Panel(c, open ? Pal::Paper : Color{176, 168, 150, 255});
        Color top = i == 0 ? Pal::Copper : i == 1 ? Color{50, 110, 130, 255} : Color{70, 50, 90, 255};
        DrawVGradient({c.x + 14, c.y + 14, c.width - 28, 120}, ColorBrightness(top, 0.15f), ColorBrightness(top, -0.35f));
        DrawTextCenteredBold(PlatLevelName(i), c.x + c.width / 2, c.y + 46, 32, Pal::Paper);
        DrawTextCenteredBold(lv[i].diff, c.x + c.width / 2, c.y + 90, 18, Pal::Paper);
        DrawWrapped(lv[i].desc, {c.x + 20, c.y + 150, c.width - 40, 110}, 16, Pal::Ink);
        DrawWrapped(TextFormat("Reward: %s", lv[i].reward), {c.x + 20, c.y + 262, c.width - 40, 40}, 15, Pal::BrassDk);
        if (open) {
            TxtBold(TextFormat("Layout: %s", PlatLayoutCode(g, i).c_str()), c.x + 20, c.y + 312, 17, Pal::Ink);
            if (g.platBest[i] > 0) Txt(TextFormat("Best time %.1fs", g.platBest[i]), c.x + 230, c.y + 314, 15, Pal::BrassDk);
            if (i == PL_HULL) {
                if (Button({c.x + 20, c.y + 342, c.width - 40, 32}, g.platHullBoss ? "Kraken fight: ON (chance of a relic)" : "Kraken fight: OFF (no relic)", true, 13))
                    g.platHullBoss = !g.platHullBoss;
            } else if (i == PL_PIRATE) {
                if (Button({c.x + 20, c.y + 342, c.width - 40, 32}, g.platPirateBoss ? "Blackbeard fight: ON (relic(s) guaranteed)" : "Blackbeard fight: OFF (no relic)", true, 13))
                    g.platPirateBoss = !g.platPirateBoss;
            }
            if (Button({c.x + 20, c.y + 388, c.width - 40, 44}, "Dive in")) { StartPlatform(g, i); return; }
            if (Button({c.x + 20, c.y + 440, c.width - 40, 40}, "Reshuffle layout (10g)", g.gold >= 10)) {
                g.gold -= 10;
                GeneratePlatLayout(g, i);
                Toast(g, "The sections rattle and rearrange themselves...");
            }
        } else {
            DrawTextCenteredBold(TextFormat("Locked: clear %s first", PlatLevelName(i - 1)), c.x + c.width / 2, c.y + 390, 18, Pal::BrassDk);
        }
    }
    // run options, kept between sessions
    if (Button({60, 632, 250, 40}, g.platHard ? "Difficulty: HARD" : "Difficulty: Normal", true, 17)) g.platHard = !g.platHard;
    Txt(g.platHard ? "Every gear, mine, spiked ball and jet. +50% bonus gold." : "No gears, mines, spiked balls or jets, and a brighter lamp.", 320, 643, 15, Pal::Paper);
    if (Button({700, 632, 250, 40}, g.platCheckpoints ? "Checkpoints: ON" : "Checkpoints: OFF", true, 17)) g.platCheckpoints = !g.platCheckpoints;
    Txt(g.platCheckpoints ? "Respawn in the section you reached,\nbut no relics can be won." : "A death sends you back to the start.\nRelics can be won.", 960, 634, 15, Pal::Paper);
    const char* help = "A/D or arrows to move   |   Space to jump: hold for height, jump off walls   |   Esc to give up   |   Only bosses can be stomped";
    TxtShadow(help, SCREEN_W / 2.0f - MeasureTxt(help, 16) / 2.0f, 690, 16, Color{220, 200, 160, 255});
}
