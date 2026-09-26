// Pixel-art portraits for the Flats creature cards: one small sprite per card name, drawn as crisp squares with an ink outline.
#include "raylib.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Sprite { const char* name; std::vector<const char*> rows; };

Color Pal(char c) {
    switch (c) {
        case 'w': return {238, 236, 226, 255};
        case 'l': return {192, 198, 206, 255};
        case 'e': return {124, 132, 146, 255};
        case 'a': return {58, 64, 84, 255};
        case 'r': return {206, 62, 52, 255};
        case 'o': return {234, 132, 42, 255};
        case 'y': return {242, 208, 72, 255};
        case 'g': return {84, 172, 92, 255};
        case 'G': return {40, 112, 72, 255};
        case 'b': return {72, 134, 212, 255};
        case 'B': return {40, 72, 142, 255};
        case 'c': return {120, 214, 226, 255};
        case 'p': return {146, 92, 184, 255};
        case 'm': return {236, 142, 164, 255};
        case 'n': return {142, 92, 56, 255};
        case 'N': return {92, 56, 36, 255};
        case 's': return {220, 192, 142, 255};
        case 'k': return {22, 20, 30, 255};
        default: return {0, 0, 0, 0};
    }
}

const std::vector<Sprite>& Sprites() {
    static const std::vector<Sprite> s = {
        {"Minnow", {"....eeee........", "..eelllllle..ee.", ".elllkllllleeee.", ".ellllllllle.ee.", "..eelllllee.....", "....eeee........"}},
        {"Hermit Crab", {"..o.........o...", ".ooo...nnn.ooo..", ".oo..nnnyynn.oo.", "..oo.nyynnyn.o..", "...ooonnyyynn...", "...ooonnnnnn....", "..o.oooooooo.o..", ".o.o..o..o..o.o."}},
        {"Flying Fish", {"......ll........", ".....lbbl.......", "..b.lbbbbl......", ".bbbbbbbbbbb....", "bbbbbkbbbbbbb...", ".bblllllllllbb..", "..bbllllllbb..b.", "....bbbbb....bb."}},
        {"Anglerfish", {".........y......", "........y.......", ".......y........", "....aaaaaaa.....", "..aaaaaaaaaaa...", ".aaaaywaaaaaaa..", ".aaaaaaaaaaaaaa.", ".aawawawawaaaaa.", "..aaaaaaaaaaaaa.", "....aaaa..aaa..."}},
        {"Pufferfish", {"..o.o..o..o.....", "...oyyyyyyo.....", ".ooyyyyyyyyoo...", "..yyykyyyyyyy...", ".oyyyyyyyyyyyo..", "..yyswwsyyyyyy..", ".oyyyyyyyyyyo.oo", "...oyyyyyyyo.ooo", "..o.o..o..o....."}},
        {"Sea Urchin", {".p...p..p...p...", "..p..p..p..p....", "...pppppppp.....", "pppppkppppppppp.", "...pppppppp.....", "pppppppppppp.pp.", "..p..p..p..p....", ".p...p..p...p..."}},
        {"Clownfish", {"......oo........", "....ooooooo.....", "..ooowwooowwoo..", ".oookwwooowwooo.", ".oooowwooowwoo.o", "..ooowwooowwoooo", "....ooooooo..oo."}},
        {"Fry", {"..ll.......ll..", ".lkllee..lkllee.", "..ll..ee..ll.ee.", "......ll........", "....lkllee......", ".....ll..ee....."}},
        {"Ship's Cat", {"..o.o...........", "..ooo...........", ".oooo.....oo....", ".okoko...oooo...", ".ooooo..ooo.....", ".oonoo.oooo.....", "..ooooooooo.....", "..oooooooooo....", "..oo.oo..oo....."}},
        {"Ballast Cask", {"....nnnnnnnn....", "...nnnnnnnnnn...", "..eeeeeeeeeeee..", "..nnnnnnnnnnnn..", "..nnnnnNNnnnnn..", "..nnnnnNNnnnnn..", "..eeeeeeeeeeee..", "...nnnnnnnnnn...", "....nnnnnnnn...."}},
        {"Salvage Diver", {".....yyyy.......", "....yyyyyy......", "...yycccyyy.....", "...ycccccyy.....", "...yycccyyy.....", "....yyyyyy.bbb..", "..bbbbbbbbbbb...", ".bbbbbbbbbbbb...", ".bb.bbbbb.bb....", "..gg.....gg....."}},
        {"Mudskipper", {".....k.k........", "....gggggg......", "..ggggggggggg...", ".ggggnggggggggg.", ".gggggggggggg.gg", "..ggnnggggnnggg.", ".nn.nn.nn.nn..g."}},
        {"Sailfish", {".....bb.........", "....bBBb........", "...bBBBBb.......", ".bbbbbbbbbbb....", "lllllkbbbbbbbb..", "....llllllbbbbbb", ".....bbbbb.bb..."}},
        {"Skeleton Sailor", {"...kkkkkkkk.....", "..kkkkyykkkk....", ".kkkkkkkkkkkk...", "...wwwwwwww.....", "...wkkwwkkw.....", "...wkkwwkkw.....", "....wwkkww......", "...cwwwwwwc.....", "..cccwkwkwccc...", "...cccwkwccc....", "....cc.cc.cc...."}},
        {"Stingray", {"......bbbb......", "...bbbbbbbbbb...", ".bbbbkbbbbkbbbb.", "bbbbbbbbbbbbbbbb", ".bbbbbbbbbbbbeee", "...bbbbbbbbbe...", ".....bbbbb.e...."}},
        {"Manta Ray", {".....b....b.....", ".bb.bbb..bbb.bb.", "bbbbbbbbbbbbbbbb", "bbbbbkbbbbkbbbbb", ".bbbbbwwwwbbbbb.", "..bbbbwwwwbbbb..", "....bbbwwbbb....", "......eee.......", ".......e........"}},
        {"Sea Turtle", {"....gggggggg....", "..gGGgGGgGGgg...", ".gGgGGgGGgGGgg..", ".ggGGgGGgGGgggg.", "gggggggggggggg..", "sskgggggggggg.s.", "ss.ss......ss..."}},
        {"Coral Queen", {"..r.r.r.rr.r.r..", "..r.rrrrrr.r.r..", "...rryyyyrrr....", "....mmmmmm......", "...mkmmmmkm.....", "....mmrrmm......", "...rrrrrrrr.....", "..rrrrrrrrrr....", ".rrr.rrrr.rrr..."}},
        {"Crab Sentinel", {".rr..........rr.", "rrrr...ee...rrrr", "rr.r..eeee..r.rr", ".rr.rrrrrrrr.rr.", "...rrkrrrrkrr...", "...rrrrrrrrrr...", "..rrrelllerrr...", ".r.rr.rrrr.rr.r.", "r..r..r..r..r..r"}},
        {"Ghost Crab", {".ww..........ww.", "wwww...cc...wwww", "ww.w..c..c..w.ww", ".ww.wwwwwwww.ww.", "...wwcwwwwcww...", "...wwwwwwwwww...", "..wwwwwwwwwww...", ".w.ww.wwww.ww.w.", "c..c..c..c..c..c"}},
        {"Sea Anemone", {".m.m.mm.m.m.m...", ".m.mm.mm.mm.m...", "..mmmmmmmmmmm...", "...mmmmmmmmm....", "....pppppppp....", "...ppppppppp....", "..eeeeeeeeeeee.."}},
        {"Hammerhead", {"..e.............", "..ee....ee......", "..eee.eeeeee....", "..eekeeeeeeeee..", "..eeeeeeeeeeeeee", "..eeewwwwwweee..", "..ee.eewwweee...", "..e....e....e..."}},
        {"Barracuda", {"............ee..", ".lllllllllllllee", "lllkllllllllllee", "lwwwwllleeeeeee.", ".llllllllll.ee.."}},
        {"Moray Eel", {"...nnnnnnnn.....", "..nnnnnnnnnn....", ".nnnggggggnnn...", ".nngggggggggg...", ".nnnkggggwwwgg..", ".nnnnggggggggg..", "..nnnnnngggg....", "...nnnnnnnn....."}},
        {"Nautilus", {"....ssss........", "...soooosss.....", "..soossooos.....", ".soossoossos....", ".soosoooosos....", ".sooossssoos....", "..soooooooss....", "...ssssss.mm.mm.", ".......mmmmmmmm."}},
        {"Great White", {".......ee.......", "......eeee......", ".....eeeeee.....", ".eeeeeeeeeeeeeee", "eeeekeeeeeeeeeee", "eewwwwwlllllleee", ".lwwwwwllllllle.", "..lllllllllll.ee", ".......ll......."}},
        {"Sperm Whale", {".aaaaaaaaaaa....", "aaaaaaaaaaaaaa..", "aakaaaaaaaaaaaaa", "aaaaaaaaaaaaaaaa", "awwawwawwaaaaaaa", ".aaaaaaaaaaaa.aa", "..............aa"}},
        {"Kraken Spawn", {"....pppppp......", "...pppppppp.....", "..ppwkppwkpp....", "..pppppppppp....", "..pp.pppp.pp....", ".pp.pp..pp.pp...", "pp..p....p..pp..", ".p.pp....pp..p.."}},
        {"Sea Serpent", {".........gggg...", "........gggggg..", ".......ggkggrr..", "....gg..gggggg..", "...gggg..gggg...", "..gg.gg...gg....", ".gg...gg.gg.....", "gg.....ggg......"}},
        {"Drowned King", {"..y.y.yy.y.y....", "..yyyyyyyyyy....", "...wwwwwwww.....", "...wkkwwkkw.....", "...wwwwwwww.....", "....wwkkww......", ".gg.wwwwww.gg...", "..gggwkwkwggg...", "...gg.gg.gg....."}},
        {"Chambered Titan", {"..w..w..w..w....", "..sssssssss.....", ".sooosssoooss...", "soossoooossoos..", "sooosooossooos..", "soosoooooooos...", ".soooosssooss...", "..ssssssss.mmm..", ".......mmmmmmm.."}},
        {"Bilge Rat", {".ee.............", "eeee.....eee....", "eekee...eeeeee..", "eeeeeeeeeeeeeem.", ".eeeeeeeeeeeee.m", "..e.e....e.e..m."}},
        {"Deckhand", {"....wwwww.......", "...wwwwwww......", "...ssssss.......", "...sksskss......", "....ssss........", "..bbwbbwbbw.....", "..bwbbwbbwb.....", "..bbwbbwbbw.....", "...aa...aa......"}},
        {"Rusted Anchor", {".......ee.......", "......e..e......", ".......ee.......", "..eeeeeeeeee....", ".......ne.......", ".......en.......", ".e.....ne....e..", ".ee....ee...ee..", "..eee..en.eee...", "....eeeeeee....."}},
        {"Barnacle Husk", {".....eeeeee.....", "...eeeeeeeeee...", "..elelleeleelee.", ".eeeeeeeeeeeeee.", ".eelweeelweeeee.", ".eeeeeeeeeeeeee.", "..eeeeeeeeeeee.."}},
        {"The Croupier", {"....aaaaaa......", "....aaaaaa......", "..aaaaaaaaaaaa..", "....ssssss......", "...sksssksss....", "...aaaaaaaa.....", "....sssssss.....", ".....rrrrr......", "..aaaaraaaaaa...", "..aaaaaaaaaaaa.."}},
        {"Boulder", {"....eeeeeee.....", "..eeeleeeeeee...", ".eeleeeeeeeeeee.", ".eeeeeeeeaeeeee.", ".eeeeeeeeeeeeee.", "..eeeeeaeeeeee..", "....eeeeeeee...."}},
        {"Black Goat", {".w..........w...", ".ww........ww...", "..ww.aaaa.ww....", "...waaaaaaw.....", "...arraarra.....", "...aaaaaaaa.....", "....aaaaaa......", ".....awwa.......", ".....aaaa......."}},
    };
    return s;
}
}  // namespace

// Draws the named creature's pixel portrait centred in `box`. Returns false if the card has no sprite (the caller draws its suit icon).
bool DrawCreaturePixels(const std::string& name, Rectangle box, float dim) {
    const Sprite* sp = nullptr;
    for (const Sprite& s : Sprites()) if (name == s.name) { sp = &s; break; }
    if (!sp) return false;
    int w = 16, h = (int)sp->rows.size();
    float cell = std::max(1.0f, (float)(int)(std::min(box.width / w, box.height / 12.0f)));
    float ox = box.x + (box.width - w * cell) / 2, oy = box.y + (box.height - h * cell) / 2;
    Color ink = {18, 14, 20, 255};
    if (dim < 1) ink.a = (unsigned char)(255 * dim);
    auto filled = [&](int x, int y) {
        if (y < 0 || y >= h || x < 0) return false;
        const char* r = sp->rows[y];
        return x < (int)strlen(r) && r[x] != '.';
    };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (filled(x, y)) continue;
            if (filled(x - 1, y) || filled(x + 1, y) || filled(x, y - 1) || filled(x, y + 1))
                DrawRectangleRec({ox + x * cell, oy + y * cell, cell, cell}, ink);
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w && x < (int)strlen(sp->rows[y]); x++)
            if (filled(x, y)) DrawRectangleRec({ox + x * cell, oy + y * cell, cell, cell}, Pal(sp->rows[y][x]));
    return true;
}
