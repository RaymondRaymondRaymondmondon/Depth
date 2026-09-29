// The parkour section's sound cues. Nothing in the simulation calls into the audio: instead, once a frame, this
// compares the level's state with the last frame's and plays what changed - a jump, a landing, a pose, a shot
// leaving a barrel, a boss's move, a beast that strikes, flees, is hurt, dies, grabs a meal or chews on one -
// so the verifiers and the path search, which run the same code headless, never hear or depend on any of it.
#include "game.h"
#include "beasts.h"
#include "sound.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

float PlatDaylight(const PlatformState& p); // platformer.cpp

namespace {

constexpr float PWf = 20, PHf = 26;
struct Prev {
    bool valid = false;
    int level = -1, deaths = 0;
    float time = 0;
    Vector2 pos{0, 0}, vel{0, 0};
    bool onGround = false, finished = false;
    int pose = 0, wallSide = 0;
    float deathTimer = 0;
    int shots[8] = {};
    std::vector<PlatEnemy> enemies;
    size_t crumbles = 0;
    int bossState = 0, bossHp = 3;
    float tentT[2] = {-1, -1}, sweepT = -1, lungeT = -1, inkT = -1;
    bool beak = false, reach = false, defeated = false;
    bool inWater = false;
};
Prev P;
float gStep = 0, gRainT = 0;
struct BeastPrev { BeastAct act; BeastLife life; float health; int carry; float chewT, alarmT; };
std::unordered_map<int, BeastPrev> gBP;

char TileAt(const PlatformState& p, float x, float y) {
    int tx = (int)floorf(x / 32), ty = (int)floorf(y / 32);
    if (tx < 0 || ty < 0 || tx >= p.w || ty >= p.h) return '.';
    return p.tiles[ty][tx];
}
float SurfacePitch(const PlatformState& p, char under) { // what the boots are on: steel rings, wood thunks, sand and weed hush
    if (under == '=' || under == '|' || p.level == PL_PIPES || (p.level == PL_HULL && under == '#')) return 1.45f;
    if (p.level == PL_PIRATE) return 0.85f;
    if (p.level == PL_ISLAND || p.level == PL_WEEDS) return 0.75f;
    return 1.0f;
}

} // namespace

void ParkourAudio(const PlatformState& p, float dt) {
    AudioLevel(p.level);
    Vector2 ear{p.pos.x + PWf / 2, p.pos.y + PHf / 2};
    AudioListener(ear);
    AudioDay(PlatDaylight(p));
    bool fresh = !P.valid || P.level != p.level || p.time + 0.5f < P.time;
    if (fresh) { P = Prev{}; gBP.clear(); }
    bool underwaterLv = p.level == PL_HULL || p.level == PL_CAVE || p.level == PL_WEEDS || p.level == PL_ATLANTIS;
    bool alive = p.deathTimer <= 0 && !p.finished;

    if (!fresh) {
        // ---- the diver
        if (p.deathTimer > 0 && P.deathTimer <= 0) {
            bool sea = p.waterY > 0 && p.pos.y + PHf > p.waterY;
            if (sea) SfxAt(Sfx::Splash, ear);
            SfxAt(underwaterLv || sea ? Sfx::DeathWater : Sfx::Death, ear);
        }
        if (p.deathTimer <= 0 && P.deathTimer > 0) SfxAt(Sfx::Respawn, ear, 0.7f);
        if (p.finished && !P.finished) SfxAt(Sfx::Win, ear);
        if (alive) {
            bool rose = p.vel.y < -250 && P.vel.y > -120;
            if (rose) {
                if (p.vel.y < -kin::JUMP_V * 1.25f) SfxAt(Sfx::Launch, ear);                  // a drum-fungus, a root-sponge, a vent
                else if (P.wallSide != 0 && !P.onGround) SfxAt(Sfx::WallJump, ear);
                else SfxAt(Sfx::Jump, ear, 0.9f, 0.95f + 0.1f * (float)GetRandomValue(0, 100) / 100);
            }
            if (p.onGround && !P.onGround) {
                if (p.pose == 3) SfxAt(Sfx::Stun, ear);
                else if (p.pose == 2) SfxAt(Sfx::Roll, ear);
                else if (P.vel.y > 650) SfxAt(Sfx::HardLand, ear, 0.8f);
                else if (P.vel.y > 120) SfxAt(Sfx::Land, ear, std::min(1.0f, P.vel.y / 600), SurfacePitch(p, TileAt(p, ear.x, p.pos.y + PHf + 4)) > 1.3f ? 1.25f : 1.0f);
            }
            if (p.pose != P.pose) {
                if (p.pose == 5) SfxAt(Sfx::Dash, ear);
                else if (p.pose == 8) SfxAt(Sfx::Backflip, ear);
                else if (p.pose == 9) SfxAt(Sfx::Grab, ear);
                else if (p.pose == 7) SfxAt(Sfx::Glide, ear);
                else if (p.pose == 4) SfxAt(Sfx::Glide, ear, 0.5f, 0.8f);
            }
            // footsteps, and splashes through the Island's pools
            if (p.onGround && p.pose == 0 && fabsf(p.vel.x) > 80) {
                gStep += fabsf(p.vel.x) * dt;
                if (gStep > 34) {
                    gStep = 0;
                    char feet = TileAt(p, ear.x, p.pos.y + PHf - 4), under = TileAt(p, ear.x, p.pos.y + PHf + 4);
                    if (feet == '~') SfxAt(Sfx::Wade, ear, 0.8f);
                    else SfxAt(Sfx::Step, ear, underwaterLv ? 0.45f : 0.7f, SurfacePitch(p, under) * (0.93f + 0.14f * (float)GetRandomValue(0, 100) / 100));
                }
            } else gStep = 30;
        }
        AudioSlide(alive && p.pose == 1 ? std::min(1.0f, fabsf(p.vel.x) / 400) : 0);
        // a draught or a current: a vent's column under you, or water rushing past as you move
        float flow = 0;
        for (int k = 1; k <= 8; k++) if (TileAt(p, ear.x, p.pos.y + PHf + k * 32.0f) == 'v') { flow = 0.8f; break; }
        if (underwaterLv) flow = std::max(flow, std::min(0.5f, sqrtf(p.vel.x * p.vel.x + p.vel.y * p.vel.y) / 1400));
        if (p.level == PL_PIRATE || p.level == PL_ISLAND) flow = std::max(flow, std::clamp((p.startPos.y - p.pos.y) / 1200, 0.0f, 0.35f)); // the wind picks up the higher you climb
        AudioFlow(alive ? flow : 0);
        if (p.waterY > 0) { bool inWater = p.pos.y + PHf > p.waterY; if (inWater && !P.inWater && alive) SfxAt(Sfx::Splash, ear); P.inWater = inWater; }

        // ---- things fired, thrown and launched
        int cnt[8] = {};
        for (const auto& s : p.shots) if (s.kind >= 0 && s.kind < 8) cnt[s.kind]++;
        for (int k = 0; k < 8; k++) if (cnt[k] > P.shots[k]) {
            Vector2 at{0, 0};
            for (auto it = p.shots.rbegin(); it != p.shots.rend(); ++it) if (it->kind == k) { at = it->pos; break; }
            switch (k) {
            case 0: SfxAt(cnt[k] - P.shots[k] >= 5 ? Sfx::Blunderbuss : Sfx::Pistol, at); break;
            case 1: SfxAt(Sfx::BombThrow, at); break;
            case 2: SfxAt(Sfx::Blast, at); break;
            case 3: if (gRainT <= 0) { SfxAt(Sfx::Ink, at, 0.5f); gRainT = 0.25f; } break;
            case 4: SfxAt(Sfx::Torpedo, at); break;
            case 5: SfxAt(Sfx::Cannon, at); break;
            case 6: SfxAt(Sfx::Barrel, at); break;
            }
        }
        gRainT -= dt;
        // ---- the level's own enemies falling (musket balls cut down their own side)
        if (p.enemies.size() < P.enemies.size()) {
            for (const auto& e : P.enemies) {
                bool still = false;
                for (const auto& f : p.enemies) if (f.type == e.type && fabsf(f.home.x - e.home.x) < 1 && fabsf(f.home.y - e.home.y) < 1) { still = true; break; }
                if (still) continue;
                Vector2 at{e.pos.x + 10, e.pos.y + 12};
                if (e.type == 'p') SfxAt(Sfx::BirdSquawk, at);
                else if (e.type == 'P' || e.type == 'G') SfxAt(Sfx::PirateCry, at);
                else BeastSound(e.type == 'e' ? "Eel" : "Crab", 1, CUE_DEATH, at);
            }
        }
        if (p.crumbles.size() > P.crumbles) { const auto& c = p.crumbles.back(); SfxAt(Sfx::Crumble, {c.tx * 32.0f + 16, c.ty * 32.0f}); }

        // ---- the bosses
        const PlatBoss& b = p.boss;
        if (b.type == 'K') {
            float arenaX = (p.w - 24) * 32.0f;
            Vector2 lair{arenaX + 12 * 32.0f, ear.y + 200};
            for (int i = 0; i < 2; i++) {
                if (b.tentT[i] >= 0 && P.tentT[i] < 0) SfxAt(Sfx::KrakenWarn, {b.tentX[i], ear.y + 120}, 0.8f);
                if (b.tentT[i] >= 0.75f && P.tentT[i] < 0.75f && !b.tentFake[i]) SfxAt(Sfx::KrakenSwipe, {b.tentX[i], ear.y});
            }
            if (b.sweepT >= 0 && P.sweepT < 0) SfxAt(Sfx::KrakenWarn, {b.sweepDir > 0 ? arenaX : arenaX + 24 * 32.0f, ear.y});
            if (b.sweepT >= 0.9f && P.sweepT < 0.9f) SfxAt(Sfx::KrakenSwipe, ear, 1.2f);
            if (b.lungeT >= 0 && P.lungeT < 0) SfxAt(Sfx::KrakenSwipe, {b.lungeFromX, ear.y});
            if (b.inkT >= 0 && P.inkT < 0) SfxAt(Sfx::Ink, lair, 1.2f, 0.6f);
            if ((b.beak.active && !P.beak) || (b.reach.active && !P.reach)) SfxAt(Sfx::KrakenRoar, lair, 0.8f, 1.2f);
            if (b.state == 1 && P.bossState == 0 && !b.defeated) SfxAt(Sfx::KrakenRoar, lair);
            if (b.hp < P.bossHp) { SfxAt(Sfx::BossHit, ear); if (!b.defeated) SfxAt(Sfx::KrakenRoar, lair, 1, 1.4f); }
            if (b.defeated && !P.defeated) SfxAt(Sfx::KrakenDeath, lair);
        } else if (b.type == 'B') {
            Vector2 at{b.pos.x + 18, b.pos.y + 30};
            if (b.state != P.bossState && !b.defeated) {
                if (b.state == 1) SfxAt(Sfx::BBGrowl, at);
                else if (b.state == 2) SfxAt(Sfx::BBCharge, at);
                else if (b.state == 5) SfxAt(Sfx::BBCrash, at);
                else if (b.state == 6) SfxAt(Sfx::BBGrowl, at, 1, 0.85f);
            }
            if (b.hp < P.bossHp) { SfxAt(Sfx::BossHit, at); if (!b.defeated) SfxAt(Sfx::BBHurt, at); }
            if (b.defeated && !P.defeated) SfxAt(Sfx::BBDeath, at);
        }

        // ---- an apex near the diver (the director's megalodon, kraken, serpent, leviathan...) or a boss fight: the score tightens
        {
            float tension = 0;
            const BeastWorld& Wt = p.fauna;
            if (Wt.active && Wt.apexPos.x > -1e8f) { float d = sqrtf((Wt.apexPos.x - ear.x) * (Wt.apexPos.x - ear.x) + (Wt.apexPos.y - ear.y) * (Wt.apexPos.y - ear.y)); tension = std::clamp(1.3f - d / 900, 0.0f, 1.0f); }
            if (p.boss.type && !p.boss.defeated && p.pos.x > (p.w - 26) * 32.0f) tension = 1;
            AudioTension(alive ? tension : 0);
        }
        // ---- the beasts: calls, alarms, strikes, pain, deaths, grabs and chewing
        const BeastWorld& W = p.fauna;
        if (W.active) {
            for (size_t i = 0; i < W.beasts.size(); i++) {
                const Beast& bb = W.beasts[i];
                if (fabsf(bb.pos.x - ear.x) > 1500 || fabsf(bb.pos.y - ear.y) > 900) { gBP.erase(bb.id); continue; }
                const SpeciesDef& S = BeastSpecies(p.level, bb.species);
                float size = BeastSize(p.level, bb.species) * bb.scale * std::clamp(S.mass / 20.0f, 0.3f, 4.0f);
                auto it = gBP.find(bb.id);
                if (it == gBP.end()) { gBP[bb.id] = {bb.act, bb.life, bb.health, bb.carry, 0, 0}; continue; }
                BeastPrev& q = it->second;
                q.alarmT -= dt;
                if (q.life == BeastLife::Alive && bb.life == BeastLife::Corpse) {
                    BeastSound(S.name, size, CUE_DEATH, bb.pos);
                    for (const auto& o : W.beasts) // the killer's bite
                        if (o.life == BeastLife::Alive && o.target == (int)i && fabsf(o.pos.x - bb.pos.x) < 90 && fabsf(o.pos.y - bb.pos.y) < 90) {
                            BeastSound(BeastSpecies(p.level, o.species).name, BeastSize(p.level, o.species) * o.scale * std::clamp(BeastSpecies(p.level, o.species).mass / 20.0f, 0.3f, 4.0f), CUE_CHEW, o.pos);
                            break;
                        }
                } else if (bb.life == BeastLife::Alive) {
                    if (bb.act == BeastAct::Strike && q.act != BeastAct::Strike) BeastSound(S.name, size, CUE_STRIKE, bb.pos);
                    if (bb.act == BeastAct::Flee && q.act != BeastAct::Flee && bb.fear > 0.4f && q.alarmT <= 0 && !BeastIsSilent(S.name)) { BeastSound(S.name, size, CUE_ALARM, bb.pos, 0.8f); q.alarmT = 3; }
                    if (bb.health < q.health - 0.05f) BeastSound(S.name, size, CUE_PAIN, bb.pos);
                    if (bb.carry >= 0 && q.carry < 0) BeastSound(S.name, size, CUE_GRAB, bb.pos);
                    if (bb.act == BeastAct::Eat) { q.chewT -= dt; if (q.chewT <= 0) { BeastSound(S.name, size, CUE_CHEW, bb.pos, 0.55f); q.chewT = 0.7f + 0.6f * (float)GetRandomValue(0, 100) / 100; } }
                    if (!bb.hidden && bb.act != BeastAct::Eat && !BeastIsSilent(S.name) && (float)GetRandomValue(0, 100000) / 100000 < BeastCallRate(S.name) * dt) BeastSound(S.name, size, CUE_CALL, bb.pos);
                }
                q.act = bb.act; q.life = bb.life; q.health = bb.health; q.carry = bb.carry;
            }
            if (gBP.size() > W.beasts.size() * 2 + 64) gBP.clear();
        }
    }

    // ---- remember this frame
    P.valid = true; P.level = p.level; P.time = p.time; P.deaths = p.deaths;
    P.pos = p.pos; P.vel = p.vel; P.onGround = p.onGround; P.finished = p.finished; P.pose = p.pose; P.wallSide = p.wallSide; P.deathTimer = p.deathTimer;
    for (int k = 0; k < 8; k++) P.shots[k] = 0;
    for (const auto& s : p.shots) if (s.kind >= 0 && s.kind < 8) P.shots[s.kind]++;
    P.enemies = p.enemies; P.crumbles = p.crumbles.size();
    P.bossState = p.boss.state; P.bossHp = p.boss.hp; P.defeated = p.boss.defeated;
    P.tentT[0] = p.boss.tentT[0]; P.tentT[1] = p.boss.tentT[1]; P.sweepT = p.boss.sweepT; P.lungeT = p.boss.lungeT; P.inkT = p.boss.inkT;
    P.beak = p.boss.beak.active; P.reach = p.boss.reach.active;
}

// ---------------------------------------------------------------- the Abyss
namespace {
struct AbPrev { bool valid = false; bool dead = false, won = false, dashing = false; float iframe = 0, maw = 0; std::vector<int> states; };
AbPrev A;
}
void AbyssAudio(const AbyssState& a, float dt) {
    AudioLevel(PL_COUNT);
    if (!A.valid || a.creatures.size() < A.states.size() / 2 || a.time < 0.2f) { A = AbPrev{}; A.valid = true; }
    else {
        if (a.dead && !A.dead) Sfx2D(Sfx::DeathWater);
        if (a.won && !A.won) Sfx2D(Sfx::Win);
        if (a.isDashing && !A.dashing) Sfx2D(Sfx::Dash, 0.8f);
        if (a.hazardIFrame > A.iframe + 0.1f) Sfx2D(Sfx::Stun, 0.8f);
        if (a.mawT > 0 && A.maw <= 0) Sfx2D(Sfx::KrakenRoar, 1, 0.6f); // the Trench Maw, rising
        for (size_t i = 0; i < a.creatures.size() && i < A.states.size(); i++) {
            const AbyssCreature& c = a.creatures[i];
            float dx = c.pos.x - a.playerPos.x, dy = c.pos.y - a.playerPos.y, dz = c.pos.z - a.playerPos.z, d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d > 30) continue;
            float pan = std::clamp(dx / 8, -1.0f, 1.0f);
            int st = (int)c.state;
            if (st != A.states[i]) {
                if (c.state == AbyssCreatureState::Hunting || c.state == AbyssCreatureState::Lunging) AbyssSound((int)c.kind, CUE_STRIKE, d, pan);
                else if (c.state == AbyssCreatureState::Shattered) AbyssSound((int)c.kind, CUE_DEATH, d, pan);
                else if (c.state == AbyssCreatureState::Fleeing || c.state == AbyssCreatureState::Dislodged) AbyssSound((int)c.kind, CUE_ALARM, d, pan);
                else if (c.state == AbyssCreatureState::Passing) AbyssSound((int)c.kind, CUE_CALL, d * 0.5f, pan);
            } else if ((float)GetRandomValue(0, 100000) / 100000 < 0.02f * dt) AbyssSound((int)c.kind, CUE_CALL, d, pan);
        }
    }
    AudioTension(a.mawT > 0 ? 1.0f : 0.0f);
    AudioFlow(a.isGliding ? 0.6f : std::min(0.5f, sqrtf(a.playerVel.x * a.playerVel.x + a.playerVel.y * a.playerVel.y + a.playerVel.z * a.playerVel.z) / 20));
    AudioSlide(0);
    A.dead = a.dead; A.won = a.won; A.dashing = a.isDashing; A.iframe = a.hazardIFrame; A.maw = a.mawT;
    A.states.resize(a.creatures.size());
    for (size_t i = 0; i < a.creatures.size(); i++) A.states[i] = (int)a.creatures[i].state;
}

// ---------------------------------------------------------------- the volume sliders (on the Periscope)
static bool Slider(Rectangle r, const char* label, float& v) {
    Txt(label, r.x, r.y, 14, Pal::Ink);
    Rectangle bar{r.x + 90, r.y + 6, r.width - 130, 6};
    DrawRectangleRounded(bar, 1, 4, Color{150, 130, 100, 255});
    DrawRectangleRounded({bar.x, bar.y, bar.width * v, bar.height}, 1, 4, Pal::BrassDk);
    DrawCircleV({bar.x + bar.width * v, bar.y + 3}, 7, Pal::Brass);
    DrawCircleLinesV({bar.x + bar.width * v, bar.y + 3}, 7, Pal::Ink);
    Txt(TextFormat("%d", (int)roundf(v * 100)), bar.x + bar.width + 12, r.y, 14, Pal::Ink);
    Rectangle hit{bar.x - 8, r.y - 4, bar.width + 16, 22};
    static const float* dragging = nullptr;
    Vector2 m = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, hit)) dragging = &v;
    if (dragging == &v && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) Sfx2D(Sfx::Respawn, 0.7f); // a chime to hear the new level by
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dragging = nullptr;
    if (dragging == &v) { v = std::clamp((m.x - bar.x) / bar.width, 0.0f, 1.0f); return true; }
    return false;
}
void DrawVolumeSliders(Rectangle r) {
    AudioVolumes& V = Volumes();
    float w = (r.width - 20) / 2;
    Slider({r.x, r.y, w, 20}, "Master", V.master);
    Slider({r.x + w + 20, r.y, w, 20}, "Music", V.music);
    Slider({r.x, r.y + 26, w, 20}, "Effects", V.sfx);
    Slider({r.x + w + 20, r.y + 26, w, 20}, "Ambience", V.ambience);
}
