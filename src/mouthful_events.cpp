// Mouthful's dangers that aren't players (design doc pp. 11-12, stage 5): the boat (its net, hooks and chum line), the
// orca pod, the red tide, the whale fall, dusk and the eel garden. The reef and apex sharks and the leviathan are the
// web's own (mouthful.cpp); these are the designer's, on the round's clock (doc p. 13): the orca pod at a random minute
// between 5 and 10, dusk at 10:00, the whale fall at 11:00 (scaled to the round's length), high tide in the last minute.
#include "mouthful.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace mf {

int DeathKind(const std::string& c) { return c == "player" ? 0 : c == "leviathan" ? 3 : c == "npc" ? 1 : 2; }

void World::StepEvents(float dt) {
    const float L = roundLen;
    // ---- the boat: it crosses the shallows or the blue on a schedule, its shadow showing where it goes
    Boat& B = boat;
    if (!B.on) {
        B.nextT -= dt;
        if (B.nextT <= 0 && !HighTide()) {
            B.on = true;
            bool shallows = Rand() < 0.5f;
            B.dirZ = Rand() < 0.5f ? 1.0f : -1.0f;
            B.pos = {shallows ? Rand(-270, -190) : Rand(10, 110), 0, B.dirZ > 0 ? Z0 - 20 : Z1 + 20};
            // its gear this crossing: a net, a line of hooks, a chum line (one or two of them)
            int g = (int)(Rand() * 3) % 3; B.net = g == 0; B.hooks = g == 1; B.chum = g == 2;
            if (Rand() < 0.35f) { int h = (g + 1 + (int)(Rand() * 2) % 2) % 3; B.net |= h == 0; B.hooks |= h == 1; B.chum |= h == 2; }
            B.hookList.clear(); B.netted.clear(); B.nettedT.clear();
            if (B.hooks) for (int k = 0; k < 4; k++) { Hook hk; hk.pos = {B.pos.x - 6 + k * 4.0f, -4.0f - Rand() * 10, B.pos.z}; B.hookList.push_back(hk); }
            Say(std::string("A boat's engine overhead: ") + (B.net ? "a net" : "") + (B.net && (B.hooks || B.chum) ? " and " : "") + (B.hooks ? "hooks" : "") + (B.hooks && B.chum ? " and " : "") + (B.chum ? "a chum line" : "") + ".", Color{200, 220, 255, 255});
        }
    } else {
        B.pos.z += B.dirZ * B.speed * dt;
        // the hooks hang on their lines and drift with the boat
        for (auto& hk : B.hookList) {
            hk.pos.z += B.dirZ * B.speed * dt;
            if (hk.held >= 0) {
                Mouth& m = mouths[hk.held];
                if (!m.alive) { hk.held = -1; continue; }
                m.pos = hk.pos; m.vel = {0, 0, 0}; m.holdT = std::max(m.holdT, 0.2f);
                hk.heldT += dt;
                if (hk.heldT >= 3) { Hurt(m, 0.2f * m.mass, -1, -1, "a hook"); hk.held = -1; hk.gone = true; m.holdT = 0; if (!m.bot) Say("The hook tears free, and takes a fifth of you with it.", Color{255, 190, 160, 255}); }
            }
        }
        // the chum line: free mass (and blood in the water: a shark magnet) along the boat's wake
        if (B.chum) {
            Vector3 c{B.pos.x, -2, B.pos.z - B.dirZ * 8};
            eco.AddBlood(c, 6 * dt);
            for (auto& m : mouths) if (m.alive && m.tier <= 5) { Vector3 d = Vector3Subtract(m.pos, c); if (fabsf(d.x) < 4 && fabsf(d.y) < 4 && d.z * -B.dirZ > 0 && d.z * -B.dirZ < 30) Feed(m, 2 * dt, false); }
        }
        // the net: anything of tier 1-3 in it is hauled up when it surfaces (death); boosting at its edge slips out
        if (B.net) {
            Vector3 nc = B.NetCentre();
            for (auto& m : mouths) {
                if (!m.alive || m.tier > 3 || m.immuneT > 0) continue;
                bool in = fabsf(m.pos.x - nc.x) < 10 && m.pos.y > -13 && fabsf(m.pos.z - nc.z) < 3;
                int k = -1; for (int i = 0; i < (int)B.netted.size(); i++) if (B.netted[i] == m.id) k = i;
                if (in && k < 0) { B.netted.push_back(m.id); B.nettedT.push_back(0); if (!m.bot) Say("NETTED: boost to slip out under the edge!", Color{255, 170, 140, 255}); continue; }
                if (k < 0) continue;
                // held in the net, dragged along and up
                B.nettedT[k] += dt;
                m.pos = Vector3Lerp(m.pos, {std::clamp(m.pos.x, nc.x - 9, nc.x + 9), std::min(-0.5f, m.pos.y + 1.5f * dt), nc.z}, std::min(1.0f, dt * 3));
                m.vel = {0, 0, 0};
                bool slip = m.boosting || (m.in.boost && m.stamina > 0.05f);
                if (slip && Rand() < dt * 0.9f) { B.netted.erase(B.netted.begin() + k); B.nettedT.erase(B.nettedT.begin() + k); m.pos.y -= 3; if (!m.bot) Say("You slip out under the net's edge.", Color{180, 240, 200, 255}); continue; }
                if (B.nettedT[k] > 6) { B.netted.erase(B.netted.begin() + k); B.nettedT.erase(B.nettedT.begin() + k); KillMouth(m, -1, -1, "the boat's net"); }
            }
            // and it scoops the web's small fish too
            for (auto& a : eco.agents) if (a.alive && a.diver < 0 && fabsf(a.pos.x - nc.x) < 10 && a.pos.y > -13 && fabsf(a.pos.z - nc.z) < 3 && preyMass[a.sp] < 30) a.alive = false;
        }
        if (B.pos.z > Z1 + 25 || B.pos.z < Z0 - 25) {
            B.on = false; B.nextT = 120 + Rand() * 60;
            for (auto& hk : B.hookList) if (hk.held >= 0) mouths[hk.held].holdT = 0;
            B.hookList.clear(); B.netted.clear(); B.nettedT.clear();
        }
    }
    // ---- the orca pod: once a round it passes through the blue, hunting the three largest on the leaderboard for 90 s
    OrcaPod& O = orcas;
    if (!O.on && !O.done && time >= O.at) {
        O.on = true; O.t = 90; O.agents.clear();
        int sp = eco.map->SpeciesIndex("Orca");
        if (sp >= 0) for (int k = 0; k < 3; k++) {
            Vector3 p{120.0f, -30 - k * 6.0f, Z0 + 10 + k * 8.0f};
            int z = std::max(0, eco.ZoneAt(p));
            int ai = eco.Spawn(sp, p, z);
            if (ai >= 0) { O.agents.push_back(ai); eco.agents[ai].held = 1; }
        }
        if (deadT.size() < eco.agents.size()) deadT.resize(eco.agents.size(), 0);
        // the top three at its arrival: survive it while up there for the bonus
        O.top3 = 0; auto b = Board(); for (int k = 0; k < 3 && k < (int)b.size(); k++) O.top3 |= 1u << b[k];
        O.deathsAt.clear(); for (const auto& m : mouths) O.deathsAt.push_back(m.deaths);
        Say("Clicks in the blue: the orca pod has come for the biggest mouths.", Color{255, 150, 140, 255});
    }
    if (O.on) {
        O.t -= dt;
        auto b = Board();
        for (int k = 0; k < (int)O.agents.size(); k++) {
            rt::Agent& a = eco.agents[O.agents[k]];
            if (!a.alive) continue;
            a.held = 1;
            // each hunts one of the three largest (by mass) it can reach
            std::vector<int> big; for (const auto& m : mouths) if (m.alive && m.immuneT <= 0) big.push_back(m.id);
            std::sort(big.begin(), big.end(), [&](int x, int y) { return mouths[x].mass > mouths[y].mass; });
            int tgt = big.empty() ? -1 : big[std::min(k, (int)big.size() - 1)];
            Vector3 goal = O.t > 0 && tgt >= 0 ? mouths[tgt].pos : Vector3{X1 + 40, -30, a.pos.z};
            if (O.t <= 0) goal.x = X1 + 40;
            Vector3 to = Vector3Subtract(goal, a.pos); float d = Vector3Length(to);
            float sp = 8.5f;
            if (d > 0.2f) a.vel = Vector3Lerp(a.vel, Vector3Scale(to, sp / d), std::min(1.0f, dt * 2));
            a.pos = Vector3Add(a.pos, Vector3Scale(a.vel, dt));
            a.pos.y = std::clamp(a.pos.y, FloorY(a.pos.x, a.pos.z) + 2, -1.0f);
            a.pos.z = std::clamp(a.pos.z, Z0 - 30, Z1 + 30);
            a.zone = std::max(0, eco.ZoneAt(a.pos));
            a.cooldown = std::max(0.0f, a.cooldown - dt);
            if (O.t > 0 && tgt >= 0 && d < 3.5f && a.cooldown <= 0) { a.cooldown = 2.5f; OnDiverHit(mouths[tgt].agent, O.agents[k], 0); }
        }
        if (O.t <= 0) {
            bool gone = true;
            for (int ai : O.agents) { rt::Agent& a = eco.agents[ai]; if (a.alive && a.pos.x < X1 + 30) gone = false; else a.alive = false; }
            if (gone || O.t < -25) {
                O.on = false; O.done = true;
                for (int ai : O.agents) eco.agents[ai].alive = false;
                // the top three that came through it alive score the survivors' 100
                auto nb = Board(); uint32_t now = 0; for (int k = 0; k < 3 && k < (int)nb.size(); k++) now |= 1u << nb[k];
                for (auto& m : mouths) if (((O.top3 >> m.id) & 1) && ((now >> m.id) & 1) && m.id < (int)O.deathsAt.size() && m.deaths == O.deathsAt[m.id] && m.alive) { m.score += D().scoreOrca; Say(m.name + " survived the orca pod at the top of the chain.", Color{200, 230, 255, 255}); }
                Say("The orca pod moves on.", Color{200, 220, 230, 255});
            }
        }
    }
    // ---- the red tide: a bloom over the shallows or the reef for 60 s: it blinds everything in it and kills the prey schools
    Bloom& R = bloom;
    if (!R.on && !R.done && time >= bloomAt) {
        R.on = true; R.t = 60; R.pos = {Rand(-260, -80), -6, Rand(-90, 90)}; R.r = 40;
        Say("A red tide blooms over the reef: blinding, and the schools die in it.", Color{255, 140, 120, 255});
    }
    if (R.on) {
        R.t -= dt;
        for (auto& m : mouths) if (m.alive && Vector2Distance({m.pos.x, m.pos.z}, {R.pos.x, R.pos.z}) < R.r && m.pos.y > -40) m.blindT = std::max(m.blindT, 0.5f);
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            rt::Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0 || preyMass[a.sp] > 30) continue;
            if (Vector2Distance({a.pos.x, a.pos.z}, {R.pos.x, R.pos.z}) < R.r && a.pos.y > -40 && Rand() < dt * 0.08f) eco.Kill(i, -1);   // (free corpses)
        }
        if (R.t <= 0) { R.on = false; R.done = true; Say("The red tide clears.", Color{220, 200, 200, 255}); }
    }
    // ---- dusk: the light drops (LightAt), the bait rises
    if (!duskDone && time >= duskAt) {
        duskDone = true;
        for (auto& a : eco.agents) if (a.alive && a.diver < 0 && eco.map->species[a.sp].social == "school") a.home.y = std::min(-1.0f, a.home.y + 6);
        Say("Dusk. The light goes, the bait rises, and the ambushers' hour begins.", Color{200, 200, 255, 255});
    }
    // ---- the whale fall: 300 mass of feast on the trench floor; every tier 5+ and the apex sharks come; too long a melee wakes the leviathan
    WhaleFall& F = fall;
    if (!F.on && !F.done && time >= fallAt) {
        F.on = true; F.left = 300; F.t = 0;
        float x = 170 + Rand() * 40, z = Rand(-30, 30);
        F.pos = {x, FloorY(x, z) + 1.5f, z};
        Say("A whale falls into the trench: a feast for whoever dares the deep.", Color{255, 220, 160, 255});
    }
    if (F.on) {
        F.t += dt;
        if (fmodf(F.t, 2.0f) < dt) eco.AddBlood(F.pos, 25);   // (the smell carries)
        // the apex sharks are drawn to it
        for (auto& a : eco.agents) if (a.alive && a.diver < 0 && npcEats[a.sp] >= 6 && npcEats[a.sp] < 8 && eco.map->species[a.sp].name != "Orca" && a.st != rt::State::Hunt) { a.st = rt::State::Investigate; a.goal = F.pos; a.stateT = 0; }
        int feeding = 0; for (const auto& m : mouths) if (m.alive && Vector3Distance(m.pos, F.pos) < 15) feeding++;
        if (feeding >= 2) levNoise += dt * 0.25f * feeding;   // (a long melee down there wakes it)
        if (F.left <= 0) { F.on = false; F.done = true; Say("The whale fall is picked to the bone.", Color{220, 210, 200, 255}); }
    }
    // ---- the eel garden: a patch of the reef floor that bites anything tier 1-2 passing within a metre of it
    {
        float gy = FloorY(EEL_GARDEN.x, EEL_GARDEN.z);
        for (auto& m : mouths) {
            if (!m.alive || m.tier > 2 || m.immuneT > 0) continue;
            if (Vector2Distance({m.pos.x, m.pos.z}, {EEL_GARDEN.x, EEL_GARDEN.z}) < EEL_GARDEN_R && m.pos.y < gy + 1.0f && Rand() < dt * 1.5f) Hurt(m, 0.35f * m.mass, -1, -1, "the eel garden");
        }
    }
}

}  // namespace mf
