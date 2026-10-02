// The Wardrobe (skins.h): one page for either game. The wallet and the crates (buy one, open one, what came out), the
// store's 15 to buy outright, and the 65 crate skins in a grid by rarity (owned ones show their colours; click one
// you own to wear it). Opened from Red Tide's Locker room and from the Trawl's arcade reel.
#include "skins.h"
#include "game.h"
#include "sound.h"
#include <cmath>
#include <algorithm>

namespace skins {

static Roll gLast; static float gLastT = 0; static int gLastGame = -1;

static void Swatch(Rectangle r, const Skin& s, bool owned) {
    if (!owned) { DrawRectangleRec(r, Color{26, 32, 34, 255}); DrawTextCentered("?", r.x + r.width / 2, r.y + r.height / 2 - 8, 16, Color{90, 100, 100, 255}); return; }
    float w = r.width / 4;
    Color c[4] = {s.hat, s.top, s.trousers, s.trim};
    for (int k = 0; k < 4; k++) DrawRectangleRec({r.x + k * w, r.y, w + 0.5f, r.height}, c[k]);
}

bool WardrobePage(int game) {
    Wardrobe& w = Get(game);
    const auto& cat = Catalogue(game);
    const Color INK{226, 222, 206, 255}, DIM{150, 150, 140, 255};
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{14, 22, 26, 255});
    DrawTextCenteredBold(game == REDTIDE ? "THE WARDROBE: Red Tide's divers" : "THE WARDROBE: the Trawl's crew", SCREEN_W / 2.0f, 18, 28, INK);
    DrawTextCentered("Colours on your own figure: the silhouette never changes. Crates come from milestones, or 150 tokens. A skin you already own rolls nothing.", SCREEN_W / 2.0f, 56, 14, DIM);
    bool back = Button({30, 16, 120, 34}, "< Back", true, 16) || IsKeyPressed(KEY_ESCAPE);
    // the wallet and the crates
    std::string why;
    TxtBold(TextFormat("Tokens: %d", Tokens(game)), 40, 92, 20, Color{230, 200, 110, 255});
    TxtBold(TextFormat("Crates: %d", w.crates), 240, 92, 20, INK);
    if (Button({400, 86, 200, 34}, TextFormat("Buy a crate (%d)", CRATE_PRICE), Tokens(game) >= CRATE_PRICE, 15)) { if (BuyCrate(game, &why)) PlayCue("ui.click"); }
    if (Button({610, 86, 170, 34}, "Open a crate", w.crates > 0, 15)) {
        gLast = OpenCrate(game, (uint32_t)(GetTime() * 1000.0) ^ (uint32_t)GetRandomValue(1, 1 << 30)); gLastT = 4.0f; gLastGame = game;
        if (gLast.ok && !gLast.duplicate && gLast.rarity == LEGEND) PlayCue("mus.boss"); else PlayCue("ui.click");
    }
    if (gLastT > 0 && gLastGame == game && gLast.ok) {
        gLastT -= GetFrameTime();
        const Skin* s = Find(game, gLast.id);
        Color rc = RarityColor(gLast.rarity);
        float a = std::min(1.0f, gLastT);
        if (gLast.duplicate) TxtBold(TextFormat("%s: %s, already yours: nothing this time", RarityName(gLast.rarity), s ? s->name : "?"), 800, 94, 16, Fade(DIM, a));
        else TxtBold(TextFormat("%s! %s", RarityName(gLast.rarity), s ? s->name : "?"), 800, 94, 18, Fade(rc, a));
    }
    // the store
    TxtBold("The store", 40, 140, 20, INK);
    int i = 0;
    for (const auto& s : cat) {
        if (s.rarity != STORE) continue;
        Rectangle r{40.0f + (i % 2) * 300.0f, 170.0f + (i / 2) * 44.0f, 290, 38};
        bool own = w.Owns(s.id), worn = w.worn == s.id, hov = CheckCollisionPointRec(GetMousePosition(), r);
        DrawRectangleRounded(r, 0.2f, 6, worn ? Color{56, 76, 60, 255} : hov ? Color{40, 52, 54, 255} : Color{30, 40, 42, 255});
        Swatch({r.x + 8, r.y + 8, 40, 22}, s, true);
        Txt(s.name, r.x + 58, r.y + 4, 15, INK);
        Txt(worn ? "worn" : own ? "owned: wear" : TextFormat("%d tokens", s.price), r.x + 58, r.y + 21, 12, worn ? Color{140, 220, 140, 255} : own ? DIM : (Tokens(game) >= s.price ? Color{230, 200, 110, 255} : Fade(DIM, 0.5f)));
        if (hov && s.note[0]) Txt(s.note, 40, SCREEN_H - 60, 13, DIM);
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { if (own) Wear(game, s.id); else BuySkin(game, s.id, &why); PlayCue("ui.click"); }
        i++;
    }
    // the collection: the 65 crate skins by rarity
    TxtBold("The crates' skins", 660, 140, 20, INK);
    float y = 170;
    for (int rar = COMMON; rar <= LEGEND; rar++) {
        int have = 0, n = 0;
        for (const auto& s : cat) if (s.rarity == rar) { n++; have += w.Owns(s.id); }
        TxtBold(TextFormat("%s  %d / %d", RarityName(rar), have, n), 660, y, 15, RarityColor(rar));
        y += 22;
        int k = 0;
        for (const auto& s : cat) {
            if (s.rarity != rar) continue;
            Rectangle r{660.0f + (k % 13) * 44.0f, y + (k / 13) * 34.0f, 40, 28};
            bool own = w.Owns(s.id), hov = CheckCollisionPointRec(GetMousePosition(), r);
            Swatch(r, s, own);
            DrawRectangleLinesEx(r, w.worn == s.id ? 3.0f : 1.0f, w.worn == s.id ? Color{140, 220, 140, 255} : Fade(RarityColor(rar), own ? 0.9f : 0.35f));
            if (hov) {
                Txt(own ? TextFormat("%s (%s)%s%s", s.name, RarityName(rar), s.note[0] ? ": " : "", s.note) : TextFormat("Not yet found (%s)", RarityName(rar)), 660, SCREEN_H - 60, 13, own ? INK : DIM);
                if (own && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { Wear(game, s.id); PlayCue("ui.click"); }
            }
            k++;
        }
        y += ((k + 12) / 13) * 34.0f + 8;
    }
    // what's worn
    const Skin* worn = w.worn.empty() ? nullptr : Find(game, w.worn);
    TxtBold(TextFormat("Wearing: %s", worn ? worn->name : "as issued"), 40, SCREEN_H - 36, 16, INK);
    if (worn && Button({300, SCREEN_H - 42, 140, 30}, "As issued", true, 14)) Wear(game, "");
    return back;
}

// ---------------------------------------------------------------- the gallery shots
std::vector<GalleryLabel> gGallery;
void DrawGallery(int game, int page) {
    const Color INK{236, 226, 200, 255}, DIM{170, 166, 150, 255};
    int n = (int)Catalogue(game).size();
    DrawRectangle(0, 0, SCREEN_W, 40, Fade(BLACK, 0.55f));
    DrawTextCenteredBold(TextFormat("%s skins %d-%d of %d", game == REDTIDE ? "Red Tide" : "The Trawl", page * 10 + 1, std::min(n, page * 10 + 10), n), SCREEN_W / 2.0f, 8, 22, INK);
    for (const auto& l : gGallery) {
        DrawRectangleRounded({l.at.x - 92, l.at.y + 4, 184, 40}, 0.3f, 6, Fade(BLACK, 0.6f));
        DrawTextCentered(l.name, l.at.x, l.at.y + 8, 15, INK);
        DrawTextCentered(l.price > 0 ? TextFormat("%s, %d tokens", l.rarity, l.price) : l.rarity, l.at.x, l.at.y + 26, 13, l.c.a ? l.c : DIM);
    }
    gGallery.clear();
}
} // namespace skins
