// The Flight, stage 4: war (design doc pp. 11-13, 22-24). Warrior roles, flocks with leaders, formations, altitude
// orders and stances; the five things that decide an air fight (altitude, wind, stamina, morale, blood); raids (caches,
// fishers, chicks); Watchers with nets, hedges and towers; the rival colonies as bots. Headless.
#include "flight.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>

namespace fl {

const char* FormationName(Formation f) { static const char* N[] = {"Chevron", "Wall", "Spiral", "Scatter", "Hammer", "Cover"}; return N[std::clamp((int)f, 0, 5)]; }
const char* StanceName(Stance s) { static const char* N[] = {"raid", "hold", "escort", "retreat at half"}; return N[std::clamp((int)s, 0, 3)]; }

// ---------------------------------------------------------------- the numbers (flight_war.json)
namespace {
struct War {
    float strong = 1.6f, weak = 0.6f, hit = 0.72f, engage = 70, reach = 2.5f, altBonus = 0.5f, aboveM = 8, climb = 2.5f, dive = 16;
    float windK = 0.3f, upwindCost = 0.6f, fightS = 25, recover = 0.5f;
    float mBase = 50, mFounder = 20, mFlockmaster = 10, mScreamer = 5, mScreamerMax = 15, mEnemyScreamer = -4, mWin = 10, mLoss = -40, mLeader = -30, mHungry = -15, mFed = 5, mRetreat = 30;
    float founderSpeed = 0.1f, fmSpeed = 0.05f;
    float chevSpeed = 0.1f, chevSide = 0.25f, wallDive = 0.25f, spiralClimb = 2, spiralThermal = 3, spiralAbove = 0.3f, scatterDodge = 0.2f, scatterVs = 0.2f, hammerAbove = 30, coverRedirect = 0.6f;
    float netHold = 3, netRange = 25, netCd = 6, watchSight = 60, towerMult = 1.5f;
    float hedgeTwigs = 10, towerTwigs = 20, towerShells = 5, bloodFall = 12, campaign = 1.6f, harassRange = 30, harassSpoil = 40;
};
const War& W() {
    static War w; static bool loaded = false;
    if (loaded) return w;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_war.json");
    if (!j.IsObj()) return w;
    auto F = [&](const char* k, float& v) { if (j.Has(k)) v = j[k].F(v); };
    F("counter_strong", w.strong); F("counter_weak", w.weak); F("hit_chance", w.hit); F("engage_m", w.engage); F("strike_reach_m", w.reach);
    F("altitude_bonus", w.altBonus); F("above_m", w.aboveM); F("climb_mps", w.climb); F("dive_mps", w.dive);
    F("wind_ground_k", w.windK); F("upwind_stamina_per_s", w.upwindCost); F("fight_stamina_s", w.fightS); F("recover_per_s", w.recover);
    F("morale_base", w.mBase); F("morale_founder", w.mFounder); F("morale_flockmaster", w.mFlockmaster); F("morale_screamer", w.mScreamer); F("morale_screamer_max", w.mScreamerMax);
    F("morale_enemy_screamer", w.mEnemyScreamer); F("morale_win", w.mWin); F("morale_loss_share", w.mLoss); F("morale_leader_killed", w.mLeader); F("morale_hungry", w.mHungry); F("morale_fed", w.mFed); F("morale_retreat", w.mRetreat);
    F("founder_speed", w.founderSpeed); F("flockmaster_speed", w.fmSpeed);
    const Json& f = j["formation"];
    if (f.IsObj()) {
        w.chevSpeed = f["chevron"]["speed"].F(w.chevSpeed); w.chevSide = f["chevron"]["side_hit"].F(w.chevSide); w.wallDive = f["wall"]["dive_hit"].F(w.wallDive);
        w.spiralClimb = f["spiral"]["climb"].F(w.spiralClimb); w.spiralThermal = f["spiral"]["thermal_climb"].F(w.spiralThermal); w.spiralAbove = f["spiral"]["hit_from_above"].F(w.spiralAbove);
        w.scatterDodge = f["scatter"]["dodge"].F(w.scatterDodge); w.scatterVs = f["scatter"]["organised_vs"].F(w.scatterVs); w.hammerAbove = f["hammer"]["striker_above_m"].F(w.hammerAbove); w.coverRedirect = f["cover"]["redirect"].F(w.coverRedirect);
    }
    F("net_hold_s", w.netHold); F("net_range_m", w.netRange); F("net_cooldown_s", w.netCd); F("watcher_sight_m", w.watchSight); F("tower_mult", w.towerMult);
    F("hedge_twigs", w.hedgeTwigs); F("tower_twigs", w.towerTwigs); F("tower_shells", w.towerShells); F("blood_on_fall", w.bloodFall); F("campaign_hunger", w.campaign);
    F("harass_range_m", w.harassRange); F("harass_spoil_s", w.harassSpoil);
    return w;
}
float Flat(Vector3 a, Vector3 b) { return Vector2Distance({a.x, a.z}, {b.x, b.z}); }
}  // namespace

int StructureTwigs(int kind) { return (int)(kind == 0 ? W().hedgeTwigs : W().towerTwigs); }
int StructureShells(int kind) { return kind == 0 ? 0 : (int)W().towerShells; }

// ---------------------------------------------------------------- sides: one player's fields, swapped in to step
void World::SwapSide(int i) {
    Side& s = sides[i];
    std::swap(col, s.col); std::swap(island, s.island); std::swap(home, s.home); std::swap(me, s.me); std::swap(fb, s.fb); std::swap(founderBot, s.founderBot);
    std::swap(lagoonZone, s.lagoonZone); std::swap(inshoreZone, s.inshoreZone); std::swap(dayAcc, s.dayAcc); std::swap(dayNum, s.dayNum);
    for (int k = 0; k < 16; k++) std::swap(caughtIn[k], s.caughtIn[k]);
    std::swap(know, s.know); std::swap(log, s.log); std::swap(human, s.human);
    cur = cur == 0 ? i + 1 : 0;
}
Colony& World::ColOf(int side) {
    if (side == cur) return col;
    if (side == 0) return sides[cur - 1].col;
    return sides[side - 1].col;
}
Founder& World::FounderOf(int side) {
    if (side == cur) return me;
    if (side == 0) return sides[cur - 1].me;
    return sides[side - 1].me;
}
const std::string& World::SideName(int side) const { return side <= 0 || side > (int)sides.size() ? name0 : sides[side - 1].name; }
Color World::SideColor(int side) const { return side <= 0 || side > (int)sides.size() ? Color{230, 200, 90, 255} : sides[side - 1].livery; }
Bird* World::FindBird(int side, int id) { for (auto& b : ColOf(side).birds) if (b.id == id && b.alive) return &b; return nullptr; }
Flock* World::FindFlock(int side, int id) { for (auto& f : ColOf(side).flocks) if (f.id == id) return &f; return nullptr; }

int World::MakeFlock(int side, const std::vector<int>& ids, Formation f, Alt a, Stance s) {
    Colony& C = ColOf(side);
    Flock fl; fl.id = C.nextFlock++; fl.form = f; fl.alt = a; fl.stance = s;
    for (int id : ids) {
        Bird* b = FindBird(side, id);
        if (!b || b->stage != BStage::Adult || !IsWarrior(b->role) || b->role == Role::Watcher || b->flock >= 0 || b->retrainT > 0) continue;
        b->flock = fl.id; b->struck = false; fl.members.push_back(id);
        if (b->role == Role::Flockmaster && fl.leader == -1) fl.leader = id;
    }
    if (fl.members.empty()) return -1;
    fl.startSize = (int)fl.members.size();
    fl.name = TextFormat("Flock %d", fl.id);
    Vector3 c{}; for (int id : fl.members) c = Vector3Add(c, FindBird(side, id)->pos);
    fl.pos = Vector3Scale(c, 1.0f / fl.members.size());
    fl.morale = W().mBase;
    C.flocks.push_back(fl);
    return fl.id;
}
void World::OrderFlock(int side, int flock, Target t, int tSide, int tIsle, int tZone, int tFlock, Vector3 at) {
    Flock* f = FindFlock(side, flock);
    if (!f) return;
    f->target = t; f->tSide = tSide; f->tIsle = tIsle; f->tZone = tZone; f->tFlock = tFlock; f->tAt = at;
    f->retreating = false;
    for (int id : f->members) if (Bird* b = FindBird(side, id)) b->struck = false;
}

// ---------------------------------------------------------------- morale (doc p23)
float World::Morale(int side, const Flock& f) const {
    const War& w = W();
    World& W_ = const_cast<World&>(*this);
    float m = w.mBase;
    if (f.leader == -2) m += w.mFounder; else if (f.leader >= 0) m += w.mFlockmaster;
    int screamers = 0;
    for (int id : f.members) if (Bird* b = W_.FindBird(side, id)) screamers += b->role == Role::Screamer;
    m += std::min(w.mScreamerMax, screamers * w.mScreamer);
    m += std::min(2.0f, f.wins) * w.mWin;
    if (f.startSize > 0) m += w.mLoss * (float)f.lost / f.startSize;
    if (f.leaderDead) m += w.mLeader;
    // the colony's feed: a hungry colony's flocks fight poorly
    float food = 0, mouths = 1;
    { Colony& C = W_.ColOf(side); for (const auto& c : C.caches) for (const auto& x : c.fish) food += x.size; mouths = 3; for (const auto& b : C.birds) if (b.alive && b.stage != BStage::Egg) mouths += b.stage == BStage::Chick ? 1 : 2.5f; }
    float days = food / mouths;
    m += days < 0.5f ? w.mHungry : days > 2 ? w.mFed : 0;
    // enemy Screamers within earshot
    for (int s = 0; s <= (int)sides.size(); s++) {
        if (s == side) continue;
        for (const auto& b : W_.ColOf(s).birds) if (b.alive && b.role == Role::Screamer && b.stage == BStage::Adult && Flat(b.pos, f.pos) < 60) m += w.mEnemyScreamer;
    }
    return std::clamp(m, 0.0f, 100.0f);
}

// ---------------------------------------------------------------- the war's step
namespace {
struct Fighter { int side; Bird* b; Founder* f; int flock; };
}
void World::StepWar(float dt) {
    static bool tr = getenv("DEPTH_WARTRACE") != nullptr; static int stepN = 0; if (tr && ++stepN % 200 == 0) printf("    [war step %d]\n", stepN);
    const War& w = W();
    const Economy& E = Econ();
    int nSides = (int)sides.size() + 1;
    Vector2 wind = WindAt();
    float windSp = Vector2Length(wind);
    // ---- every fighter on the map: warriors, working birds out on the water, Founders
    std::vector<Fighter> all;
    for (int s = 0; s < nSides; s++) {
        Colony& C = ColOf(s);
        for (auto& b : C.birds) if (b.alive && (b.stage == BStage::Adult)) all.push_back({s, &b, nullptr, b.flock});
        Founder& f = FounderOf(s);
        if (f.st != FState::Dead) all.push_back({s, nullptr, &f, -1});
    }
    auto posOf = [&](const Fighter& x) { return x.b ? x.b->pos : x.f->pos; };
    auto roleOf = [&](const Fighter& x) { return x.b ? x.b->role : Role::Flockmaster; };
    auto hpOf = [&](const Fighter& x) -> float& { return x.b ? x.b->hp : x.f->hp; };
    // ---- a death: blood if over water, morale for its flock, tallies, news
    auto kill = [&](Fighter& v, int bySide, Role byRole) {
        Vector3 p = posOf(v);
        if (warFx.size() > 400) { warFxBase += 200; warFx.erase(warFx.begin(), warFx.begin() + 200); }
        warFx.push_back({p, 1, v.side, v.b ? v.b->role : Role::Flockmaster, v.b ? v.b->yaw : v.f->yaw});
        std::string cause = std::string("killed by ") + (bySide == 0 ? "your " : SideName(bySide) + "'s ") + RoleName(byRole);
        if (!LandAt(p.x, p.z)) eco.AddBlood({p.x, -0.5f, p.z}, w.bloodFall);   // (the fall calls the sharks)
        ColOf(v.side).losses++; ColOf(bySide).kills++;
        warLog.push_back(TextFormat("%.0f: %s's %s %s", time, SideName(v.side).c_str(), v.b ? RoleName(v.b->role) : "Founder", cause.c_str()));
        if (v.b) {
            Bird& b = *v.b;
            b.alive = false; b.cause = cause; b.hp = 0;
            Colony& C = ColOf(v.side);
            bool counted = false;
            for (auto& d : C.deaths) if (d.first == cause) { d.second++; counted = true; }
            if (!counted) C.deaths.push_back({cause, 1});
            if (Flock* f = FindFlock(v.side, b.flock)) { f->lost++; if (f->leader == b.id) { f->leaderDead = true; f->leader = -1; } }
            SayTo(v.side, std::string("Your ") + RoleName(b.role) + " was " + cause + ".");
        } else {
            Founder& f = *v.f;
            for (auto& fl : ColOf(v.side).flocks) if (fl.leader == -2) { fl.leaderDead = true; fl.leader = -1; }
            if (HumanOf(v.side)) WithSide(v.side, [&] { Kill(cause); });
            else { f.st = FState::Dead; f.respawnT = 30; f.deaths++; f.lastCause = cause; }
        }
    };
    // ---- flocks: tidy, morale, where they're going, how fast, their shapes
    for (int s = 0; s < nSides; s++) {
        Colony& C = ColOf(s);
        Founder& F = FounderOf(s);
        for (auto& fl : C.flocks) {
            fl.members.erase(std::remove_if(fl.members.begin(), fl.members.end(), [&](int id) { return !FindBird(s, id); }), fl.members.end());
            if (fl.members.empty()) continue;
            Vector3 c{}; int n = 0;
            for (int id : fl.members) if (Bird* b = FindBird(s, id)) { c = Vector3Add(c, b->pos); n++; }
            if (n == 0) continue;
            fl.pos = Vector3Scale(c, 1.0f / n);
            if (fl.leader == -2 && (F.st == FState::Dead || Flat(F.pos, fl.pos) > 80)) fl.leader = -1;   // (the Founder has left it)
            if (fl.leader == -1) for (int id : fl.members) { Bird* b = FindBird(s, id); if (b && b->role == Role::Flockmaster) { fl.leader = id; break; } }
            fl.morale = Morale(s, fl);
            // a rout: at 30 it goes home on its own; at 0 it scatters (every bird for itself, and it's eaten)
            if (!fl.retreating && (fl.morale <= w.mRetreat || (fl.stance == Stance::RetreatHalf && fl.lost * 2 >= fl.startSize))) {
                fl.retreating = true; fl.target = Target::Home;
                SayTo(s, fl.name + " breaks off and flies home.");
                warLog.push_back(TextFormat("%.0f: %s's %s retreats (morale %.0f, %d of %d lost)", time, SideName(s).c_str(), fl.name.c_str(), fl.morale, fl.lost, fl.startSize));
            }
            if (fl.morale <= 0 && !fl.scattered) { fl.scattered = true; fl.form = Formation::Scatter; }
        }
        C.flocks.erase(std::remove_if(C.flocks.begin(), C.flocks.end(), [&](const Flock& f) {
            if (!f.members.empty() && !(f.retreating && Flat(f.pos, ColOf(s).caches.empty() ? f.pos : ColOf(s).caches[0].pos) < 40 && f.engagedT <= 0)) return false;
            for (int id : f.members) if (Bird* b = const_cast<World*>(this)->FindBird(s, id)) b->flock = -1;
            return true;   // (empty, or home again after a retreat: disbanded)
        }), C.flocks.end());
    }
    // ---- the fights: every fighter in a flock (and a home guard) picks an enemy in reach and closes; a Founder that the
    // player flies strikes what it dives onto
    auto hostile = [&](int a, int b) { return a != b; };
    auto inFlock = [&](const Fighter& x) -> Flock* { return x.b && x.b->flock >= 0 ? FindFlock(x.side, x.b->flock) : nullptr; };
    auto strike = [&](Fighter& att, Fighter& vic, bool diving) {
        Bird* a = att.b; Role ar = roleOf(att), vr = roleOf(vic);
        const RoleDef& R = RoleOf(ar);
        Flock* af = inFlock(att); Flock* vf = inFlock(vic);
        float dmg = att.f ? Founders()[att.f->def].attack * (att.f->chick ? 0.5f : 1.0f) : R.attack;
        if (R.strong & (1u << (int)vr)) dmg *= w.strong;
        if (R.weak & (1u << (int)vr)) dmg *= w.weak;
        // altitude: the first strike from above, diving, +50%
        Vector3 ap = posOf(att), vp = posOf(vic);
        bool above = ap.y - vp.y > w.aboveM;
        if (above && diving && a && !a->struck) { dmg *= 1 + w.altBonus; a->struck = true; }
        else if (above && diving) dmg *= 1 + w.altBonus * 0.5f;   // (a re-dive after zooming back up)
        if (vp.y - ap.y > w.aboveM) dmg *= 0.6f;                  // (striking upward: a flock below can only climb or run)
        if (above && diving && att.f) dmg *= 1 + w.altBonus;
        // formations
        if (vf) {
            if (vf->form == Formation::Wall && above && ar == Role::Striker) dmg *= 1 + w.wallDive;
            if (vf->form == Formation::Spiral && above) dmg *= 1 + w.spiralAbove;
            if (vf->form == Formation::Chevron && Vector2Length({vf->vel.x, vf->vel.z}) > 1) {
                Vector2 h = Vector2Normalize({vf->vel.x, vf->vel.z}), d = Vector2Normalize({vp.x - ap.x, vp.z - ap.z});
                if (fabsf(Vector2DotProduct(h, d)) < 0.5f) dmg *= 1 + w.chevSide;   // (from the side)
            }
            if (vf->form == Formation::Scatter && af && af->form != Formation::Scatter) dmg *= 1 + w.scatterVs;
        }
        float morale = af ? af->morale : 60;
        dmg *= 0.8f + 0.4f * morale / 100;
        if (a && a->fight <= 0) dmg *= 0.7f;
        float hit = w.hit - (vf && vf->form == Formation::Scatter ? w.scatterDodge : 0);
        if (vic.b && vic.b->netT > 0) hit = 1;
        if (Rand() > hit) return;
        // cover: a Tank by the escorted bird takes the blow
        if (vf && vf->form == Formation::Cover && vr != Role::Tank && Rand() < w.coverRedirect)
            for (int id : vf->members) { Bird* t = FindBird(vic.side, id); if (t && t->role == Role::Tank && Vector3Distance(t->pos, vp) < 15) { t->hp -= dmg; if (t->hp <= 0) { Fighter tv{vic.side, t, nullptr, vf->id}; kill(tv, att.side, ar); } return; } }
        float& hp = hpOf(vic);
        hp -= dmg;
        warFx.push_back({vp, 0, vic.side, vr, 0});
        if (hp <= 0) kill(vic, att.side, ar);
    };
    for (auto& x : all) {
        if (x.f) continue;
        Bird& b = *x.b;
        if (!b.alive) continue;
        Flock* fl = inFlock(x);
        bool guardian = b.role == Role::Watcher;
        if (!fl && !guardian) continue;
        const RoleDef& R = RoleOf(b.role);
        b.atkCd -= dt;
        if (b.netT > 0) { b.netT -= dt; b.vel = {0, 0, 0}; continue; }   // (held in a net)
        // the nearest enemy in reach (a Watcher looks from its post)
        float sight = guardian ? w.watchSight * (b.post.y > 15 ? w.towerMult : 1.0f) : w.engage;
        Vector3 from = guardian ? b.post : b.pos;
        Fighter* best = nullptr; float bd = sight;
        for (auto& y : all) {
            if (!hostile(x.side, y.side)) continue;
            if (y.b && !y.b->alive) continue;
            if (y.f && y.f->st == FState::Dead) continue;
            Vector3 yp = posOf(y);
            float d = Vector3Distance(from, yp);
            if (guardian && yp.y - from.y > 50) continue;   // (a Watcher only sees what flies mid or low)
            float pref = (R.strong & (1u << (int)roleOf(y))) ? 0.7f : 1.0f;
            // a Wall's Tanks stand in front: an attacker not diving from above must go through them
            if (Flock* yf = inFlock(y); yf && yf->form == Formation::Wall && roleOf(y) != Role::Tank && b.pos.y - yp.y < w.aboveM) pref *= 1.6f;
            if (d * pref < bd) { bd = d * pref; best = &y; }
        }
        if (fl && fl->retreating) best = nullptr;   // (a retreat doesn't stop to fight)
        if (!best) { b.tgtSide = -1; continue; }
        b.tgtSide = best->side; b.tgtId = best->b ? best->b->id : 0;
        if (fl) fl->engagedT = 2;
        Vector3 tp = posOf(*best);
        // Watchers: a net at what comes in range; a peck at what comes close
        if (guardian) {
            float range = w.netRange * (b.post.y > 15 ? w.towerMult : 1.0f);
            if (b.atkCd <= 0 && Vector3Distance(b.post, tp) < range && best->b) { best->b->netT = w.netHold; b.atkCd = w.netCd; warFx.push_back({tp, 2, best->side, best->b->role, 0}); SayTo(x.side, "A Watcher nets a raider."); SayTo(best->side, "A Watcher's net holds one of your birds!"); }
            if (Vector3Distance(b.pos, tp) < w.reach * 2 && b.atkCd <= w.netCd - 1.4f) { strike(x, *best, false); }
            continue;
        }
        // close and strike: a dive when above (fast), a slow climb when below
        float d = Vector3Distance(b.pos, tp);
        if (d < w.reach && b.atkCd <= 0) {
            bool diving = b.vel.y < -3 || b.pos.y - tp.y > w.aboveM;
            strike(x, *best, diving);
            b.atkCd = R.cooldown;
            if (diving) b.zoomT = 2.5f;              // (the dive's speed turns back into height: hit, climb, hit again)
            b.vel = Vector3Add(b.vel, {0, 6, 0});
        }
    }
    // a Founder a person flies: a dive onto an enemy bird strikes it
    for (int s = 0; s < nSides; s++) {
        Founder& F = FounderOf(s);
        if (!HumanOf(s) || BotFlown(s) || F.st != FState::Fly) continue;
        F.strikeCd -= dt;
        for (auto& y : all) {
            if (y.side == s || !y.b || !y.b->alive || F.strikeCd > 0) continue;
            if (Vector3Distance(F.pos, y.b->pos) < w.reach + 0.5f && Vector3Length(F.vel) > 9) {
                Fighter mine{s, nullptr, &F, -1};
                strike(mine, y, F.vel.y < -3);
                F.strikeCd = 0.8f;
                SayTo(s, y.b->alive ? TextFormat("You strike %s's %s.", SideName(y.side).c_str(), RoleName(y.b->role)) : TextFormat("You kill %s's %s.", SideName(y.side).c_str(), RoleName(y.b->role)));
            }
        }
    }
    // ---- moving the flocks and their birds: speed by the slowest, the leader, the formation and the wind; climbs are slow
    for (int s = 0; s < nSides; s++) {
        Colony& C = ColOf(s);
        Founder& F = FounderOf(s);
        Vector3 homeAt = C.caches.empty() ? Vector3{} : C.caches[0].pos;
        for (auto& fl : C.flocks) {
            if (fl.members.empty()) continue;
            // the goal
            Vector3 goal = homeAt;
            switch (fl.target) {
            case Target::Home: goal = Vector3Add(homeAt, {0, 0, 0}); break;
            case Target::Cache: { Colony& T = ColOf(fl.tSide); if (!T.caches.empty()) { int best = 0; for (int k = 0; k < (int)T.caches.size(); k++) if (T.caches[k].fish.size() > T.caches[best].fish.size()) best = k; goal = T.caches[best].pos; } } break;
            case Target::Nests: { Colony& T = ColOf(fl.tSide); for (const auto& n : T.nests) { goal = n.pos; bool chicks = false; for (const auto& b : T.birds) if (b.alive && b.stage == BStage::Chick && b.nest == (int)(&n - &T.nests[0])) chicks = true; if (chicks) break; } } break;
            case Target::Ground: if (fl.tZone >= 0) goal = eco.map->zones[fl.tZone].Center(); break;
            case Target::Flock: { Flock* tf = FindFlock(fl.tSide, fl.tFlock); goal = tf ? tf->pos : fl.tAt; if (!tf) fl.target = Target::Point; } break;
            case Target::Point: goal = fl.tAt; break;
            default: break;
            }
            if (fl.leader == -2) goal = F.pos;   // (the Founder leads: the flock follows it)
            float altY = AltHeight(fl.alt);
            if (fl.target == Target::Ground && fl.alt == Alt::Low) altY = 8;
            bool atTarget = Flat(fl.pos, goal) < 25;
            // speed: the slowest member, the leader, the chevron, the wind along the way
            float sp = 99; for (int id : fl.members) { Bird* b = FindBird(s, id); if (!b) continue; sp = std::min(sp, RoleOf(b->role).speed * (b->fight <= 0 ? 0.5f : 1.0f)); }
            if (sp > 98) continue;   // (every member fell this step)
            sp *= 1 + (fl.leader == -2 ? w.founderSpeed : fl.leader >= 0 ? w.fmSpeed : 0) + (fl.form == Formation::Chevron ? w.chevSpeed : 0);
            Vector2 dir = Flat(goal, fl.pos) > 1 ? Vector2Normalize({goal.x - fl.pos.x, goal.z - fl.pos.z}) : Vector2{0, 0};
            float along = windSp > 0.1f ? Vector2DotProduct(dir, Vector2Scale(wind, 1 / windSp)) : 0;
            float gs = sp * (1 + w.windK * along * std::min(1.0f, windSp / 8));
            fl.vel = {dir.x * gs, 0, dir.y * gs};
            // the formation's slots round the flock's heading
            Vector2 fw = Vector2Length(dir) > 0 ? dir : Vector2{1, 0}, rt{-fw.y, fw.x};
            int k = 0;
            for (int id : fl.members) {
                Bird* b = FindBird(s, id);
                if (!b || b->netT > 0) continue;   // (fallen this step, or held in a net)
                if (b->tgtSide >= 0 && !fl.retreating) {   // fighting: the bird goes for its target
                    Fighter* tgt = nullptr; for (auto& y : all) if (y.side == b->tgtSide && ((y.b && y.b->id == b->tgtId && y.b->alive) || (!y.b && b->tgtId == 0))) { tgt = &y; break; }
                    if (tgt) {
                        Vector3 tp = posOf(*tgt);
                        // the first move in any fight: get above them (a Striker climbs before it dives)
                        Vector3 aim = tp;
                        if (b->role == Role::Striker && b->pos.y - tp.y < w.aboveM && !b->struck && Vector3Distance(b->pos, tp) > 20 && b->fight > 0) aim = {b->pos.x, tp.y + w.aboveM + 6, b->pos.z};
                        if (b->zoomT > 0) aim = {b->pos.x + b->vel.x * 0.3f, std::max(tp.y + w.aboveM + 10, b->pos.y), b->pos.z + b->vel.z * 0.3f};   // (zooming back above it)
                        Vector3 d = Vector3Subtract(aim, b->pos); float dl = Vector3Length(d);
                        float spB = RoleOf(b->role).speed * (b->fight <= 0 ? 0.5f : 1.2f) * E.workPace * 0.6f;
                        Vector3 v = dl > 0.1f ? Vector3Scale(d, spB / dl) : Vector3{0, 0, 0};
                        float climb = w.climb * (fl.form == Formation::Spiral ? (Thermal(b->pos) > 0.5f ? w.spiralThermal : w.spiralClimb) : 1.0f);
                        if (b->zoomT > 0) { b->zoomT -= dt; climb = 9; }
                        if (b->fight <= 0) climb = 0;
                        v.y = std::clamp(v.y, -w.dive, climb);
                        b->vel = Vector3Lerp(b->vel, v, std::min(1.0f, dt * 4));
                        b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
                        // flapping in a fight costs breath; upwind costs more; diving is free
                        float cost = b->vel.y < -2 ? 0 : 1;
                        if (Vector2DotProduct({b->vel.x, b->vel.z}, wind) < 0) cost += w.upwindCost * std::min(1.0f, windSp / 8);
                        b->fight = std::max(0.0f, b->fight - cost * dt);
                        if (Vector2Length({b->vel.x, b->vel.z}) > 0.3f) b->yaw = atan2f(b->vel.z, b->vel.x);
                        b->flapPh += dt * 2 * PI * 3.2f; b->task = Task::Fly;
                        float g = std::max(0.0f, HeightAt(b->pos.x, b->pos.z));
                        if (b->pos.y < g + 0.5f) b->pos.y = g + 0.5f;
                        k++;
                        continue;
                    }
                }
                // in formation
                Vector3 slot = goal;
                float ky = 0;
                int row = (k + 1) / 2, sideK = k % 2 ? 1 : -1;
                switch (fl.form) {
                case Formation::Chevron: slot = Vector3Add(fl.pos, {fw.x * (-3.0f * row) + rt.x * sideK * 3.0f * row, 0, fw.y * (-3.0f * row) + rt.y * sideK * 3.0f * row}); break;
                case Formation::Wall: slot = Vector3Add(fl.pos, {rt.x * (k - (int)fl.members.size() / 2) * 3.5f - (b->role == Role::Tank ? 0 : fw.x * 5), 0, rt.y * (k - (int)fl.members.size() / 2) * 3.5f - (b->role == Role::Tank ? 0 : fw.y * 5)}); break;
                case Formation::Spiral: { float a = time * 0.8f + k * 2 * PI / fl.members.size(); slot = Vector3Add(fl.pos, {cosf(a) * 12, 0, sinf(a) * 12}); } break;
                case Formation::Scatter: slot = Vector3Add(fl.pos, {sinf(b->id * 3.1f + time * 0.4f) * 15, 0, cosf(b->id * 1.7f + time * 0.3f) * 15}); break;
                case Formation::Hammer: slot = Vector3Add(fl.pos, {rt.x * sideK * 4.0f * row, 0, rt.y * sideK * 4.0f * row}); ky = b->role == Role::Striker ? w.hammerAbove : b->role == Role::Skirmisher ? -10.0f : 0; break;
                case Formation::Cover: slot = Vector3Add(fl.pos, {rt.x * sideK * 3.0f * row, 0, rt.y * sideK * 3.0f * row}); ky = b->role == Role::Tank ? 6.0f : 0; break;
                default: break;
                }
                // the flock heads for its goal; its slots ride along with it
                if (!atTarget || fl.target == Target::Flock) slot = Vector3Add(slot, Vector3Scale(fl.vel, 1.5f));
                slot.y = std::max(altY + ky, std::max(0.0f, HeightAt(slot.x, slot.z)) + 6);
                // raids: at the target the raiders go down to it
                if (atTarget && (fl.target == Target::Cache || fl.target == Target::Nests) && (b->role == Role::Skirmisher || b->role == Role::Striker) && b->carrySp < 0) slot = Vector3Add(goal, {0, 0.8f, 0});
                if (b->carrySp >= 0) slot = Vector3Add(homeAt, {0, 1, 0});   // (a stolen fish goes home)
                Vector3 d = Vector3Subtract(slot, b->pos); float dl = Vector3Length(d);
                float spB = (Vector3Length(fl.vel) > 0.5f ? Vector3Length(fl.vel) * 1.15f : RoleOf(b->role).speed * 0.6f) * E.workPace;
                if (dl < 4) spB *= dl / 4;
                Vector3 v = dl > 0.05f ? Vector3Scale(d, std::min(spB, dl * 3) / dl) : Vector3{0, 0, 0};
                float climb = w.climb * (fl.form == Formation::Spiral ? (Thermal(b->pos) > 0.5f ? w.spiralThermal : w.spiralClimb) : 1.0f) * E.workPace;
                if (b->fight <= 0) climb = 0;
                v.y = std::clamp(v.y, -w.dive, climb);
                b->vel = Vector3Lerp(b->vel, v, std::min(1.0f, dt * 3));
                b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));
                float g = std::max(0.0f, HeightAt(b->pos.x, b->pos.z));
                if (b->pos.y < g + 0.4f) b->pos.y = g + 0.4f;
                if (Vector2Length({b->vel.x, b->vel.z}) > 0.3f) b->yaw = atan2f(b->vel.z, b->vel.x);
                b->flapPh += dt * 2 * PI * (b->vel.y > 0.3f ? 3.0f : 1.6f);
                b->task = Task::Fly;
                // breath: travel at the flock's pace barely costs; climbing and upwind do; gliding and diving give it back
                float cost = (b->vel.y > 0.5f ? 1.0f : 0.0f) + (Vector2DotProduct({b->vel.x, b->vel.z}, wind) < 0 ? w.upwindCost * std::min(1.0f, windSp / 8) : 0);
                b->fight = cost > 0 ? std::max(0.0f, b->fight - cost * dt) : std::min(w.fightS, b->fight + w.recover * dt);
                // campaign: a flying army eats
                b->hunger -= dt / DAY * (w.campaign - 1);
                k++;
            }
            // over water too long: the shark pit
            bool water = !LandAt(fl.pos.x, fl.pos.z);
            if (fl.engagedT > 0) { fl.engagedT -= dt; fl.overWaterT += water ? dt : 0; if (fl.overWaterT > 60 && fmodf(fl.overWaterT, 1.0f) < dt) eco.AddBlood({fl.pos.x, -0.5f, fl.pos.z}, 2); }
        }
    }
    // a won engagement: the side whose flock still holds the air when the other has routed or died
    for (int s = 0; s < nSides; s++) for (auto& fl : ColOf(s).flocks) if (fl.engagedT > 0 && fl.engagedT <= dt * 1.5f && !fl.retreating) fl.wins += 1;
    // ---- raids: a Skirmisher at an enemy cache takes a fish (a hedge keeps it out); a Striker at a nest takes a chick
    // (unless a Watcher is near); Skirmishers over a ground make the enemy's fishers drop their catch and flee
    for (int s = 0; s < nSides; s++) {
        Colony& C = ColOf(s);
        for (auto& fl : C.flocks) {
            if (fl.retreating) continue;
            for (int id : fl.members) {
                Bird* b = FindBird(s, id);
                if (!b || b->netT > 0) continue;
                if (fl.target == Target::Cache && b->role == Role::Skirmisher && b->carrySp < 0) {
                    Colony& T = ColOf(fl.tSide);
                    for (auto& ca : T.caches) {
                        if (ca.fish.empty() || Vector3Distance(b->pos, ca.pos) > 3) continue;
                        bool hedged = false; for (const auto& h : T.builds) if (h.kind == 0 && h.built && Flat(h.pos, ca.pos) < 8) hedged = true;
                        if (hedged) continue;
                        size_t k = 0; for (size_t i = 1; i < ca.fish.size(); i++) if (ca.fish[i].size < ca.fish[k].size) k = i;
                        b->carrySp = ca.fish[k].sp; b->carrySize = ca.fish[k].size; ca.fish.erase(ca.fish.begin() + k);
                        C.stolen++; T.lostToRaids++;
                        SayTo(fl.tSide, SideName(s) + "'s Skirmishers are robbing your cache!");
                        break;
                    }
                }
                if (b->carrySp >= 0 && !C.caches.empty() && Flat(b->pos, C.caches[0].pos) < 4) {   // (home with it)
                    if ((int)C.caches[0].fish.size() < E.cacheCap) C.caches[0].fish.push_back({b->carrySp, b->carrySize, 0});
                    b->carrySp = -1; b->carrySize = 0;
                }
                if (fl.target == Target::Nests && b->role == Role::Striker) {
                    Colony& T = ColOf(fl.tSide);
                    for (auto& c : T.birds) {
                        if (!c.alive || c.stage != BStage::Chick || Vector3Distance(b->pos, c.pos) > 3) continue;
                        bool guarded = false; for (const auto& wt : T.birds) if (wt.alive && wt.role == Role::Watcher && wt.stage == BStage::Adult && Flat(wt.pos, c.pos) < 40) guarded = true;
                        if (guarded) continue;
                        c.alive = false; c.cause = "taken by " + SideName(s) + "'s Striker";
                        bool counted = false; for (auto& d : T.deaths) if (d.first == c.cause) { d.second++; counted = true; }
                        if (!counted) T.deaths.push_back({c.cause, 1});
                        SayTo(fl.tSide, SideName(s) + "'s Striker has taken one of your chicks!");
                        break;
                    }
                }
                if (b->role == Role::Skirmisher && (fl.target == Target::Ground || fl.target == Target::Cache || fl.target == Target::Point)) {
                    for (int t = 0; t < nSides; t++) {
                        if (t == s) continue;
                        for (auto& fsh : ColOf(t).birds) {
                            if (!fsh.alive || fsh.stage != BStage::Adult || fsh.role != Role::Fisher || Vector3Distance(fsh.pos, b->pos) > w.harassRange || fsh.fleeT > 0) continue;
                            if (fsh.carrySp >= 0) { eco.AddBlood({fsh.pos.x, -0.4f, fsh.pos.z}, 3); fsh.carrySp = -1; fsh.carrySize = 0; }   // (dropped: it bleeds)
                            fsh.fleeT = w.harassSpoil; fsh.task = Task::Idle;
                            SayTo(t, SideName(s) + "'s Skirmishers chase your fishers off their ground!");
                        }
                    }
                }
            }
        }
    }
    // ---- home guards: idle warriors rise against an enemy flock over their island (every colony does, yours too)
    for (int s = 0; s < nSides; s++) {
        Colony& C = ColOf(s);
        if (C.caches.empty()) continue;
        Vector3 homeAt = C.caches[0].pos;
        Flock* threat = nullptr; int threatSide = -1;
        for (int t = 0; t < nSides && !threat; t++) { if (t == s) continue; for (auto& fl : ColOf(t).flocks) if (!fl.retreating && Flat(fl.pos, homeAt) < 220) { threat = &fl; threatSide = t; break; } }
        if (!threat) continue;
        bool guarding = false; for (const auto& fl : C.flocks) if (fl.name == "Home guard") guarding = true;
        if (guarding) continue;
        std::vector<int> ids;
        for (const auto& b : C.birds) if (b.alive && b.stage == BStage::Adult && IsWarrior(b.role) && b.role != Role::Watcher && b.flock < 0 && b.retrainT <= 0) ids.push_back(b.id);
        if (ids.size() < 2) continue;
        int f = MakeFlock(s, ids, Formation::Hammer, Alt::Mid, Stance::Hold);
        if (Flock* fl = FindFlock(s, f)) { fl->name = "Home guard"; OrderFlock(s, f, Target::Flock, threatSide, -1, -1, threat->id, threat->pos); }
        SayTo(s, TextFormat("%s's flock is over your island: your %d warriors rise to meet it.", SideName(threatSide).c_str(), (int)ids.size()));
    }
    (void)E;
}

// ---------------------------------------------------------------- a bot's colony panel and its war
void World::BotGovern(float dt) {
    // (on the colony swapped in) a careful player: nests just ahead of the mates, the plan by the feed, warriors once
    // the colony can feed them, idle hands retrained, a hedge round home and a tower when it's big enough
    int mates = Count(BStage::Mate), alive = Alive();
    col.nestsWanted = std::min((int)col.sites.size(), std::max(2, mates + 1));
    float fpd = FeedPerDayEstimate(), mouths = MouthsPerDay();
    float fish = fpd < mouths * 1.15f ? 0.8f : 0.6f;
    for (int r = 0; r < (int)Role::COUNT; r++) col.plan[r] = 0;
    col.plan[(int)Role::Fisher] = fish; col.plan[(int)Role::Feeder] = 0.12f; col.plan[(int)Role::Builder] = 0.08f;
    if (alive >= 10 && DaysOfFood() > 0.8f) {
        col.plan[(int)Role::Fisher] = std::max(0.5f, fish - 0.2f);
        col.plan[(int)Role::Skirmisher] = 0.1f; col.plan[(int)Role::Striker] = 0.05f; col.plan[(int)Role::Tank] = 0.04f; col.plan[(int)Role::Watcher] = Count(BStage::Adult, Role::Watcher) < 2 ? 0.04f : 0;
        if (alive >= 18) { col.plan[(int)Role::Screamer] = 0.02f; col.plan[(int)Role::Flockmaster] = Count(BStage::Adult, Role::Flockmaster) < 1 ? 0.03f : 0; }
    }
    col.restBelow = 0.45f;
    if (fmodf(time, DAY * 0.5f) < dt) {
        int under = 0; for (const auto& n : col.nests) under += !n.built;
        int bld = Count(BStage::Adult, Role::Builder), fdr = Count(BStage::Adult, Role::Feeder), wk = Count(BStage::Adult) - fdr;
        if (bld > 1 + 2 * under) Retrain(Role::Fisher, Role::Builder);
        else if (fdr > 1 + wk / Econ().feederCover) Retrain(Role::Fisher, Role::Feeder);
    }
    // defences: a hedge round home at 12 birds, a tower at 20
    bool hedge = false, tower = false; for (const auto& s : col.builds) { hedge |= s.kind == 0; tower |= s.kind == 1; }
    if (!hedge && alive >= 12) { Structure s; s.kind = 0; s.pos = col.caches[0].pos; col.builds.push_back(s); }
    if (!tower && alive >= 20) { Structure s; s.kind = 1; s.pos = Vector3Add(col.caches[0].pos, {6, 0, 4}); s.pos = GroundAt(s.pos.x, s.pos.z); col.builds.push_back(s); }
}
void World::BotWar(int side, float dt) {
    Colony& C = ColOf(side);
    C.warT -= dt;
    if (C.warT > 0) return;
    C.warT = 20;
    // a raid when the colony has the warriors and the feed: its nearest neighbour, the five rules in mind
    std::vector<int> idle; int skirm = 0, strk = 0;
    for (const auto& b : C.birds) if (b.alive && b.stage == BStage::Adult && IsWarrior(b.role) && b.role != Role::Watcher && b.flock < 0 && b.retrainT <= 0 && b.hunger > 0.5f) { idle.push_back(b.id); skirm += b.role == Role::Skirmisher; strk += b.role == Role::Striker; }
    if ((int)idle.size() < 4) return;
    float food = 0; for (const auto& c : C.caches) for (const auto& x : c.fish) food += x.size;
    if (food < 8) return;
    Vector3 homeAt = C.caches[0].pos;
    int tgt = -1; float bd = 1e9f;
    for (int s = 0; s <= (int)sides.size(); s++) { if (s == side || ColOf(s).caches.empty()) continue; float d = Flat(ColOf(s).caches[0].pos, homeAt); if (d < bd) { bd = d; tgt = s; } }
    if (tgt < 0) return;
    // wind: a raid that would fly into a headwind waits for the wind to turn (the classic mistake is not to)
    Vector3 to = ColOf(tgt).caches[0].pos;
    Vector2 dir = Vector2Normalize({to.x - homeAt.x, to.z - homeAt.z}), wv = WindAt();
    if (Vector2Length(wv) > 3 && Vector2DotProduct(dir, Vector2Normalize(wv)) < -0.4f && Rand() < 0.75f) return;
    Target t = skirm >= strk ? Target::Cache : Target::Nests;
    int f = MakeFlock(side, idle, strk > 0 && skirm > 0 ? Formation::Hammer : Formation::Chevron, strk > 0 ? Alt::High : Alt::Mid, Stance::RetreatHalf);
    if (f < 0) return;
    OrderFlock(side, f, t, tgt, -1, -1, -1, to);
    if (Flock* fl = FindFlock(side, f)) fl->name = t == Target::Cache ? "Cache raiders" : "Chick snatchers";
    SayTo(tgt, TextFormat("%s's flock (%d) leaves its island, heading your way.", SideName(side).c_str(), (int)idle.size()));
}

// ---------------------------------------------------------------- --flight-war [scenario|all] [runs]: the five rules, raids, the gate
namespace {
struct FlockSpec { int strikers = 2, skirm = 3, tanks = 2, screamers = 0, masters = 0; Formation form = Formation::Chevron; Alt alt = Alt::Mid; float startY = 40; bool tired = false; };
struct Arena { World w; Vector3 mid{}; int fa = -1, fb = -1; };
int AddWarrior(World& v, int side, Role r, Vector3 p) {
    Colony& C = v.ColOf(side);
    Bird b; b.id = C.nextId++; b.stage = BStage::Adult; b.role = r; b.hp = RoleOf(r).hp; b.fight = 25; b.hunger = 1; b.pos = p;
    C.birds.push_back(b);
    return b.id;
}
void SetupArena(Arena& a, uint32_t seed) {
    MapOpts o; o.players = 2; o.arr = Arrangement::Archipelago; o.home = IsleType::Tropical;
    a.w.Init("taloned", seed, o);
    Vector3 other{}; for (const auto& is : a.w.isles) if (is.start == 1) other = is.c;
    a.mid = Vector3Scale(Vector3Add(a.w.island.c, other), 0.5f);
    a.w.me.st = FState::Perched; a.w.me.pos = a.w.island.nest; a.w.me.hunger = 1;
    a.w.wind.speed = a.w.wind.nextSpeed = 0.01f; a.w.wind.shiftT = 0; a.w.wind.shiftLen = 1e9f;
    for (int s = 0; s < 2; s++) for (int k = 0; k < 30; k++) a.w.ColOf(s).caches[0].fish.push_back({a.w.eco.map->SpeciesIndex("Mullet"), 2, 0});   // (fed colonies: no hunger in their morale)
}
int Spawn(Arena& a, int side, const FlockSpec& s, Vector3 at) {
    std::vector<int> ids; int k = 0;
    auto put = [&](Role r, int n) { for (int i = 0; i < n; i++, k++) ids.push_back(AddWarrior(a.w, side, r, {at.x + (k % 4) * 3.0f, s.startY + (r == Role::Striker && s.form == Formation::Hammer ? 20.0f : 0.0f), at.z + (k / 4) * 3.0f})); };
    put(Role::Striker, s.strikers); put(Role::Skirmisher, s.skirm); put(Role::Tank, s.tanks); put(Role::Screamer, s.screamers); put(Role::Flockmaster, s.masters);
    int f = a.w.MakeFlock(side, ids, s.form, s.alt, Stance::Raid);
    if (s.tired) for (int id : ids) a.w.FindBird(side, id)->fight = 0;
    return f;
}
// the fight: both flocks ordered onto each other; the winner holds the air while the other has routed or died
int Fight(Arena& a, float maxS = 150, float* lossA = nullptr, float* lossB = nullptr) {
    World& v = a.w;
    Flock* A = v.FindFlock(0, a.fa); Flock* B = v.FindFlock(1, a.fb);
    v.OrderFlock(0, a.fa, Target::Flock, 1, -1, -1, a.fb, B->pos);
    v.OrderFlock(1, a.fb, Target::Flock, 0, -1, -1, a.fa, A->pos);
    int nA = (int)A->members.size(), nB = (int)B->members.size();
    for (float t = 0; t < maxS; t += 0.05f) {
        v.Step(0.05f, FounderInput{});
        Flock* fa = v.FindFlock(0, a.fa); Flock* fb = v.FindFlock(1, a.fb);
        bool aOut = !fa || fa->members.empty() || fa->retreating, bOut = !fb || fb->members.empty() || fb->retreating;
        if (aOut || bOut) {
            int la = 0, lb = 0; for (const auto& b : v.ColOf(0).birds) la += !b.alive && IsWarrior(b.role); for (const auto& b : v.ColOf(1).birds) lb += !b.alive && IsWarrior(b.role);
            if (lossA) *lossA = (float)la / nA; if (lossB) *lossB = (float)lb / nB;
            return aOut && bOut ? 0 : bOut ? 1 : 2;
        }
    }
    return 0;
}
}  // namespace

int RunFlightWar(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string which = argc > 2 ? argv[2] : "all";
    int runs = argc > 3 ? std::max(1, atoi(argv[3])) : 20;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, stage 4: war (doc pp. 22-24: the five things that decide an air fight)\n");
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    auto want = [&](const char* s) { return which == "all" || which == s; };
    // a series of fights between two flocks set up by a lambda; how often A wins
    auto series = [&](auto setup, float* avgLossA = nullptr) {
        int wins = 0, losses = 0; float la = 0;
        for (int r = 0; r < runs; r++) {
            Arena a; if (getenv("DEPTH_WARTRACE")) printf("    [setup %d]\n", r); SetupArena(a, 100 + r * 17);
            if (getenv("DEPTH_WARTRACE")) printf("    [spawn]\n");
            setup(a);
            if (getenv("DEPTH_WARTRACE")) printf("    [fight %d %d]\n", a.fa, a.fb);
            float lA = 0, lB = 0;
            int res = Fight(a, 150, &lA, &lB);
            wins += res == 1; losses += res == 2; la += lA;
        }
        if (avgLossA) *avgLossA = la / runs;
        return std::make_pair(wins, losses);
    };
    Vector2 east{1, 0};
    if (want("altitude")) {
        auto r = series([&](Arena& a) { FlockSpec hi; hi.alt = Alt::High; hi.startY = 120; FlockSpec lo; lo.alt = Alt::Low; lo.startY = 15;
            a.fa = Spawn(a, 0, hi, Vector3Add(a.mid, {-60, 0, 0})); a.fb = Spawn(a, 1, lo, Vector3Add(a.mid, {60, 0, 0})); });
        check(r.first >= runs * 0.75f, TextFormat("altitude: a flock 100 m above an equal one wins %d of %d (it lost %d)", r.first, runs, r.second));
    }
    if (want("wind")) {
        auto r = series([&](Arena& a) { a.w.wind.dir = a.w.wind.nextDir = east; a.w.wind.speed = a.w.wind.nextSpeed = 9;
            FlockSpec s; a.fa = Spawn(a, 0, s, Vector3Add(a.mid, {-260, 0, 0})); a.fb = Spawn(a, 1, s, Vector3Add(a.mid, {260, 0, 0})); });
        check(r.first >= runs * 0.65f, TextFormat("wind: an equal flock attacking downwind (B flies into a 9 m/s headwind) wins %d of %d (lost %d)", r.first, runs, r.second));
    }
    if (want("stamina")) {
        auto r = series([&](Arena& a) { FlockSpec s; FlockSpec tired; tired.tired = true;
            a.fa = Spawn(a, 0, s, Vector3Add(a.mid, {-60, 0, 0})); a.fb = Spawn(a, 1, tired, Vector3Add(a.mid, {60, 0, 0})); });
        check(r.first >= runs * 0.75f, TextFormat("stamina: a fresh flock against an exhausted one (half speed, no climb) wins %d of %d (lost %d)", r.first, runs, r.second));
    }
    if (want("morale")) {
        auto r = series([&](Arena& a) { a.w.ColOf(1).caches[0].fish.clear(); FlockSpec s;
            a.fa = Spawn(a, 0, s, Vector3Add(a.mid, {-60, 0, 0})); a.fb = Spawn(a, 1, s, Vector3Add(a.mid, {60, 0, 0})); });
        check(r.first >= runs * 0.7f, TextFormat("morale: equal flocks, one from a fed colony and one from a hungry one (-15): the fed flock wins %d of %d (lost %d)", r.first, runs, r.second));
        auto r2 = series([&](Arena& a) { FlockSpec led; led.masters = 1; led.screamers = 1; FlockSpec plain; plain.skirm = 4; plain.tanks = 2;
            a.fa = Spawn(a, 0, led, Vector3Add(a.mid, {-60, 0, 0})); a.fb = Spawn(a, 1, plain, Vector3Add(a.mid, {60, 0, 0})); });
        check(r2.first >= r2.second, TextFormat("morale: a flock with a Flockmaster and a Screamer against one with a Skirmisher more holds its own: wins %d of %d (lost %d)", r2.first, runs, r2.second));
        Arena a; SetupArena(a, 7);
        FlockSpec led; led.masters = 1; a.fa = Spawn(a, 0, led, a.mid);
        Flock* f = a.w.FindFlock(0, a.fa);
        float before = a.w.Morale(0, *f);
        for (int id : f->members) if (Bird* b = a.w.FindBird(0, id); b && b->role == Role::Flockmaster) { b->alive = false; f->leaderDead = true; f->leader = -1; }
        float after = a.w.Morale(0, *f);
        check(before - after >= 39 && before - after <= 41, TextFormat("killing the leader: morale %.0f -> %.0f (-10 for the Flockmaster, -30 for its death)", before, after));
        f->lost = f->startSize / 2 + 1; f->startSize = std::max(f->startSize, 2);
        a.w.Step(0.05f, FounderInput{});
        f = a.w.FindFlock(0, a.fa);
        check(!f || f->retreating, TextFormat("a flock under %.0f morale retreats home on its own", 30.0f));
    }
    if (want("blood")) {
        Arena a; SetupArena(a, 11);
        FlockSpec s; a.fa = Spawn(a, 0, s, Vector3Add(a.mid, {-40, 0, 0})); a.fb = Spawn(a, 1, s, Vector3Add(a.mid, {40, 0, 0}));
        int sp = a.w.eco.map->SpeciesIndex("Reef Shark");
        int z = a.w.eco.ZoneAt({a.mid.x, -5, a.mid.z});
        Vector3 sharkAt{a.mid.x + 40, -6, a.mid.z};
        int sh = SpawnFishSlot(a.w.eco, sp, sharkAt, std::max(0, a.w.eco.ZoneAt(sharkAt)));
        a.w.eco.agents[sh].hunger = 1;
        float d0 = Vector3Distance(a.w.eco.agents[sh].pos, a.mid);
        float blood0 = a.w.eco.BloodNear({a.mid.x, -0.5f, a.mid.z}, 40);
        float d1 = d0;
        auto track = [&]() { if (a.w.eco.agents[sh].alive) d1 = std::min(d1, Vector3Distance(a.w.eco.agents[sh].pos, a.mid)); };
        { World& v = a.w; v.OrderFlock(0, a.fa, Target::Flock, 1, -1, -1, a.fb, {}); v.OrderFlock(1, a.fb, Target::Flock, 0, -1, -1, a.fa, {});
          for (float t = 0; t < 90; t += 0.05f) { v.eco.agents[sh].hunger = 1; v.Step(0.05f, FounderInput{}); track(); } }
        for (float t = 0; t < 30; t += 0.05f) { a.w.eco.agents[sh].hunger = 1; a.w.Step(0.05f, FounderInput{}); track(); }
        float blood1 = a.w.eco.BloodNear({a.mid.x, -0.5f, a.mid.z}, 60);
        int dead = 0; for (int s2 = 0; s2 < 2; s2++) for (const auto& b : a.w.ColOf(s2).birds) dead += !b.alive && IsWarrior(b.role);
        check(z >= 0 && dead > 0 && blood1 > blood0 + 1 && d1 < d0 - 20, TextFormat("blood: %d birds fell into the sea; the blood (%.1f) drew a hungry shark from %.0f m to within %.0f m", dead, blood1, d0, d1));
    }
    if (want("formation")) {
        auto r = series([&](Arena& a) { FlockSpec h; h.form = Formation::Hammer; FlockSpec sc; sc.form = Formation::Scatter;
            a.fa = Spawn(a, 0, h, Vector3Add(a.mid, {-60, 0, 0})); a.fb = Spawn(a, 1, sc, Vector3Add(a.mid, {60, 0, 0})); });
        check(r.first > r.second, TextFormat("formation: a Hammer (Strikers high) against an equal Scatter wins %d of %d (lost %d)", r.first, runs, r.second));
    }
    if (want("raids")) {
        // a cache raid; then with a Watcher on the cache; then with a hedge round it
        auto raid = [&](int watchers, bool hedge, int* netted) {
            Arena a; SetupArena(a, 21);
            Colony& B = a.w.ColOf(1);
            for (int k = 0; k < 10; k++) B.caches[0].fish.push_back({a.w.eco.map->SpeciesIndex("Sardine"), 1, 0});
            for (int k = 0; k < watchers; k++) { int id = AddWarrior(a.w, 1, Role::Watcher, B.caches[0].pos); (void)id; }
            if (hedge) { Structure s; s.kind = 0; s.pos = B.caches[0].pos; s.built = true; B.builds.push_back(s); }
            FlockSpec sk; sk.strikers = 0; sk.tanks = 0; sk.skirm = 4;
            Vector3 near = Vector3Add(B.caches[0].pos, {-140, 0, 0});
            int f = Spawn(a, 0, sk, near);
            a.w.OrderFlock(0, f, Target::Cache, 1, -1, -1, -1, B.caches[0].pos);
            int net = 0;
            for (float t = 0; t < 60; t += 0.05f) { a.w.Step(0.05f, FounderInput{}); for (const auto& b : a.w.ColOf(0).birds) if (b.alive && b.netT > 2.9f) net++; }
            if (netted) *netted = net;
            return a.w.ColOf(0).stolen;
        };
        int n0 = 0, n1 = 0, n2 = 0;
        int s0 = raid(0, false, &n0), s1 = raid(1, false, &n1), s2 = raid(0, true, &n2);
        check(s0 >= 3, TextFormat("a cache raid: four Skirmishers take %d fish from an unguarded cache", s0));
        check(n1 > 0, TextFormat("a Watcher on the cache nets a raider (%d nets; %d fish still taken)", n1, s1));
        check(s2 == 0, TextFormat("a hedge round the cache keeps Skirmishers out (%d taken)", s2));
        // a chick snatch: a Striker takes a chick from an unguarded nest, not from one a Watcher guards
        auto snatch = [&](bool guarded) {
            Arena a; SetupArena(a, 23);
            Colony& B = a.w.ColOf(1);
            for (int k = 0; k < 2; k++) { Bird c; c.id = B.nextId++; c.stage = BStage::Chick; c.nest = 0; c.pos = B.nests[0].pos; c.hunger = 1; B.birds.push_back(c); }
            int wid = guarded ? AddWarrior(a.w, 1, Role::Watcher, B.nests[0].pos) : -1;
            FlockSpec st; st.strikers = 2; st.skirm = 0; st.tanks = 0;
            int f = Spawn(a, 0, st, Vector3Add(B.nests[0].pos, {-120, 0, 0}));
            a.w.OrderFlock(0, f, Target::Nests, 1, -1, -1, -1, B.nests[0].pos);
            int takenWhileGuarded = 0;
            for (float t = 0; t < 60; t += 0.05f) {
                int before = 0; for (const auto& b : a.w.ColOf(1).birds) before += b.alive && b.stage == BStage::Chick;
                bool watcher = wid >= 0 && a.w.FindBird(1, wid);
                a.w.Step(0.05f, FounderInput{});
                int after = 0; for (const auto& b : a.w.ColOf(1).birds) after += b.alive && b.stage == BStage::Chick;
                if (watcher && a.w.FindBird(1, wid)) takenWhileGuarded += before - after;
            }
            int chicks = 0; for (const auto& b : a.w.ColOf(1).birds) chicks += b.alive && b.stage == BStage::Chick;
            return guarded ? takenWhileGuarded : 2 - chicks;
        };
        int t0 = snatch(false), t1 = snatch(true);
        check(t0 >= 1 && t1 == 0, TextFormat("a chick snatch: Strikers take %d chicks from an unguarded nest, %d while a Watcher guards it (they must kill the Watcher first)", t0, t1));
        // fisher harassment: their fisher drops its catch and flees the ground
        {
            Arena a; SetupArena(a, 25);
            Colony& B = a.w.ColOf(1);
            Bird f; f.id = B.nextId++; f.stage = BStage::Adult; f.role = Role::Fisher; f.hp = 60; f.hunger = 1; f.pos = Vector3Add(a.mid, {0, 10, 0}); f.carrySp = a.w.eco.map->SpeciesIndex("Sardine"); f.carrySize = 1; f.task = Task::Deliver;
            B.birds.push_back(f); int fid = f.id;
            FlockSpec sk; sk.strikers = 0; sk.tanks = 0; sk.skirm = 2;
            int fl = Spawn(a, 0, sk, Vector3Add(a.mid, {-80, 0, 0}));
            a.w.OrderFlock(0, fl, Target::Point, -1, -1, -1, -1, a.mid);
            bool fled = false, dropped = false;
            for (float t = 0; t < 30 && !fled; t += 0.05f) { a.w.Step(0.05f, FounderInput{}); Bird* b = a.w.FindBird(1, fid); if (b && b->fleeT > 0) { fled = true; dropped = b->carrySp < 0; } }
            check(fled && dropped, "fisher harassment: a Skirmisher pair over the water makes their fisher drop its catch and flee");
        }
    }
    if (want("gate")) {
        // the stage's gate: two bot colonies' flocks meet; the one higher and downwind wins
        auto r = series([&](Arena& a) { a.w.wind.dir = a.w.wind.nextDir = east; a.w.wind.speed = a.w.wind.nextSpeed = 7;
            FlockSpec up; up.form = Formation::Hammer; up.alt = Alt::High; up.startY = 110; FlockSpec down; down.form = Formation::Hammer; down.alt = Alt::Mid; down.startY = 30;
            a.fa = Spawn(a, 0, up, Vector3Add(a.mid, {-250, 0, 0})); a.fb = Spawn(a, 1, down, Vector3Add(a.mid, {250, 0, 0})); });
        check(r.first >= runs * 0.8f, TextFormat("GATE: two bot colonies fight; the higher, downwind flock wins %d of %d (lost %d)", r.first, runs, r.second));
    }
    if (which == "homes") {   // (a balance probe: a careful bot colony on each starting island type, ten days)
        for (int ty = 0; ty < 4; ty++) {
            MapOpts o; o.players = 2; o.home = (IsleType)ty; World v; v.Init("taloned", 9, o);
            v.founderBot = true; v.me.st = FState::Fly; v.me.pos = Vector3Add(v.island.nest, {0, 2, 0}); v.me.hunger = 1;
            std::string row; float taskT[16] = {};
            for (float t = 0; t < World::DAY * 10; t += 0.1f) {
                v.BotGovern(0.1f);
                v.Step(0.1f, FounderInput{});
                for (const auto& b : v.col.birds) if (b.alive && b.stage == BStage::Adult && b.role == Role::Fisher) taskT[(int)b.task] += 0.1f;
                if (fmodf(t, World::DAY * 2) < 0.1f && t > 1) row += TextFormat(" %3d", v.Alive());
            }
            int st = 0; for (const auto& d : v.col.deaths) if (d.first == "starved") st += d.second;
            { static const char* TN[] = {"idle", "fly", "search", "dive", "deliver", "eat", "gather", "build", "sit", "fetch", "feed"}; float tot = 0; for (int k = 0; k < 11; k++) tot += taskT[k]; printf("    fisher time:"); for (int k = 0; k < 11; k++) if (taskT[k] > tot * 0.02f) printf(" %s %.0f%%", TN[k], 100 * taskT[k] / tot); printf("\n"); }
            int caught = 0; float feed = 0; for (const auto& d : v.col.days) { caught += d.caught; feed += d.feedCaught; }
            int fishers = v.Count(BStage::Adult, Role::Fisher), mates = v.Count(BStage::Mate), nests = 0; for (const auto& n : v.col.nests) nests += n.built;
            printf("  %-16s birds every 2 days:%s   (starved %d, inshore stock %.0f%%; caught %d (feed %.0f), %d fishers, %d mates, %d nests, founder fish %d)\n", IsleTypeName((IsleType)ty), row.c_str(), st, v.StockOf(v.inshoreZone) * 100, caught, feed, fishers, mates, nests, v.fishCaught);
        }
    }    if (which == "rival") {   // (a trace of one rival colony's first days)
        MapOpts o; o.players = 2; World v; v.Init("taloned", 5, o);
        for (float t = 0; t < World::DAY * 5; t += 0.1f) {
            v.Step(0.1f, FounderInput{});
            if (fmodf(t, World::DAY * 0.25f) < 0.1f) {
                Colony& C = v.ColOf(1); Founder& F = v.FounderOf(1);
                int birds = 0, built = 0, fish = 0; for (const auto& b : C.birds) birds += b.alive && b.stage != BStage::Egg; for (const auto& n : C.nests) built += n.built; for (const auto& c : C.caches) fish += (int)c.fish.size();
                printf("    day %.2f: rival founder %s hunger %.2f pos %.0f %.0f %.0f carry %d; %d birds, %d nests (bowl %d/%d, mateT %.0f), %d fish stored\n", t / World::DAY, FStateName(F.st), F.hunger, F.pos.x, F.pos.y, F.pos.z, v.sides[0].fb.carrySp,
                       birds, built, C.nests[0].bowl, C.nests[0].bowlNeed, C.nests[0].mateT, fish);
            }
        }
    }    if (want("bots")) {
        // the bots on their own: two colonies grow, raise warriors and raid each other
        MapOpts o; o.players = 2; o.arr = Arrangement::Archipelago; World v; v.Init("taloned", 5, o);
        v.founderBot = true; v.me.st = FState::Fly; v.me.pos = Vector3Add(v.island.nest, {0, 2, 0}); v.me.hunger = 1;
        auto t0 = std::chrono::steady_clock::now();
        int flocks = 0; float lastFlock = -1;
        for (float t = 0; t < World::DAY * 14; t += 0.1f) {
            v.BotGovern(0.1f); v.BotWar(0, 0.1f);
            v.Step(0.1f, FounderInput{});
            for (int s = 0; s < 2; s++) for (const auto& f : v.ColOf(s).flocks) if (f.name != "Home guard" && v.time - lastFlock > 30) { flocks++; lastFlock = v.time; }
        }
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        int warriors[2] = {}, alive[2] = {};
        for (int s = 0; s < 2; s++) for (const auto& b : v.ColOf(s).birds) { alive[s] += b.alive && b.stage != BStage::Egg; warriors[s] += b.alive && b.stage == BStage::Adult && IsWarrior(b.role); }
        printf("  (14 days of two bot colonies in %.0f s: %d and %d birds, %d and %d warriors, %d raids flown, kills %d/%d, fish stolen %d/%d)\n", secs, alive[0], alive[1], warriors[0], warriors[1], flocks,
               v.ColOf(0).kills, v.ColOf(1).kills, v.ColOf(0).stolen, v.ColOf(1).stolen);
        check(warriors[0] + warriors[1] > 0 && flocks > 0, "the bots raise warriors and send raids");
    }
    printf(fails ? "flight-war: %d check(s) failed\n" : "flight-war: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace fl