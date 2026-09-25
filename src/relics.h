// ============================================================================
//  DEPTH - the relic system: the registry of all 20 relics, the equip rules, the synergy engine, the on-hit
//  effects, and the sprite generator that draws each relic's SVG icon into a texture.
// ============================================================================
#pragma once
#include "game.h"

// What an on-hit relic effect can see and change. HeroAct builds one after a hero's blow has landed and hands
// it to each equipped relic (and each active synergy); afterwards the combat code applies whatever was set.
struct CombatState {
    Game* game = nullptr;
    Hero* hero = nullptr;
    Enemy* target = nullptr;
    int damage = 0;            // damage the blow just did
    bool crit = false;
    bool chain = false;        // a Tesla arc is guaranteed
    // filled in by effects:
    bool stunTarget = false;
    int bleedTarget = 0;       // bleed per turn to lay on the target
    int recoilDamage = 0;      // damage to the hero (dynamite, backfires)
    int selfStress = 0;        // stress to the hero (occult cost)
    int stressRelief = 0;      // stress taken off the whole party
    int splashFront = 0;       // damage to the enemies in the front two ranks (dynamite)
    int arcDamage = 0;         // damage to the next enemy in line (tesla)
};

const char* RelicCategoryName(RelicCategory c);

class RelicRegistry {
public:
    static const std::vector<RelicDef>& All();       // all 20, in a fixed order (ids are save-file ids)
    static int Count();
};

// ---- equip rules: two relics at most, one hard weapon at most, no duplicates
bool CanEquipRelic(const Hero& h, int relicId, std::string* why = nullptr);
bool CanEquipRelicInSlot(const Hero& h, int relicId);      // true if there is a free slot and the rules pass

// ---- synergies: pairs of relics that do more together
struct RelicSynergy {
    bool active = false;
    const char* name = "";
    const char* text = "";
    RelicFx fx;              // added to the hero's bundle
    int dmg = 0, armorPenExtra = 0;
};
RelicSynergy CheckRelicSynergies(const RelicDef& relic1, const RelicDef& relic2);

// ---- what a hero's relics add up to (relic fx + active synergy)
RelicFx RelicBundle(const Hero& h);
void RunCombatRelicEffects(CombatState& cs);              // calls each equipped relic's on-hit effect
int InvCapacity(const Game& g);                           // 5 slots, plus what the party's packs add

// ---- icons: SVG strings are rasterised into small textures (a tiny SVG subset: rect, circle, ellipse, line,
// polygon, polyline and path with M L H V C Z; fill, stroke, stroke-width, opacity, transform="rotate()")
namespace RelicSpriteGenerator {
void Init();                                              // draw every relic's icon (needs an open window)
void Unload();
Texture2D Sprite(int relicId);                            // id 0 texture if unavailable
bool Has(int relicId);
bool HasItem(int kind);                                  // 0 battery, 1 bandage, 2 key
Texture2D ItemSprite(int kind);
Texture2D RenderSvg(const std::string& svg, int px);      // one SVG into a fresh texture
extern const char* PIPE_WRENCH_SVG;                       // the three working initial templates
extern const char* TESLA_GUN_SVG;
extern const char* AWAKENED_LANTERN_SVG;
}  // namespace RelicSpriteGenerator
