// What you hear (stage 8), worked out every frame from the state of things, so it's the same on the host and on a
// crewmate's PC (whose world is a mirror): the sea's beds by biome and depth (the reef's crackle, the kelp's creaks,
// the deep's hum, the swell under the surface), the surface (waves, wind and rain with the storm, thunder after the
// lightning), the Nautilus (her engine's beat, the battery hum, the screw's whir, creaks and groans with depth and way,
// the crush groans, breaches, the flood and the jet, the pumps, the alarm bell, the telegraph, the switchboard,
// grounding), the diver (the regulator's breath and bubbles, strokes, footsteps on teak, iron or a rug, the ladder,
// splashes in and out, the low-air beep, the heartbeat), the tools while they work, and the animals (leviathans'
// calls carrying hundreds of metres, a hunter's growl as it turns on you, schools rushing past).
// One-off events (a bite, a kill, a sonar ping, a hull strike, a tool's hit) are played where they happen (Sfx.Shared).
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public class Soundscape : MonoBehaviour
    {
        public static Soundscape I;
        Diver d; Nautilus ship; Hands hands;
        Sfx.Loop shallows, kelp, deep, aboardBed, underside, waves, wind, rain, engine, battery, whir, pumps, flood, jet, drill, blade, kite, caves, bats;
        bool made;

        public static Soundscape Attach(Diver d, Nautilus ship)
        {
            var s = new GameObject("Soundscape").AddComponent<Soundscape>(); I = s;
            s.d = d; s.ship = ship; s.hands = d.GetComponent<Hands>();
            return s;
        }

        void Make()
        {
            made = true;
            shallows = Sfx.MakeLoop("amb_shallows"); kelp = Sfx.MakeLoop("amb_kelp"); deep = Sfx.MakeLoop("amb_deep"); aboardBed = Sfx.MakeLoop("amb_aboard");
            underside = Sfx.MakeLoop("underside_loop"); waves = Sfx.MakeLoop("waves_loop"); wind = Sfx.MakeLoop("wind_loop"); rain = Sfx.MakeLoop("rain_loop");
            caves = Sfx.MakeLoop("amb_caves"); bats = Sfx.MakeLoop("bats_loop");
            foreach (var l in new[] { shallows, kelp, deep, aboardBed, underside, waves, wind, rain, caves, bats }) l.spatial = false;
            engine = Sfx.MakeLoop("engine_loop"); battery = Sfx.MakeLoop("battery_loop"); whir = Sfx.MakeLoop("whir_loop"); pumps = Sfx.MakeLoop("pump_loop");
            flood = Sfx.MakeLoop("flood_loop"); jet = Sfx.MakeLoop("jet_loop"); drill = Sfx.MakeLoop("drill_loop"); blade = Sfx.MakeLoop("blade_loop"); kite = Sfx.MakeLoop("kite_loop");
            foreach (var l in new[] { engine, battery, whir, pumps, flood, jet }) l.medium = Medium.Aboard;
        }

        // ---- state we watch for changes ---------------------------------------------------------------------------
        Medium lastEar = Medium.Water; int lastTele = -1; PowerState lastPower; bool lastTripped = true; int lastBreaches; float lastHealth = 100;
        float lastFlash, breathT, strokeT, beepT, heartT, creakT = 6f, groanT = 8f, alarmT, stepDist, ladderDist, raftCreakT = 4f, rushT;
        bool breathIn; Vector3 lastFeet; bool lastGrounded; bool lastKiteDocked = true;
        readonly Dictionary<int, CState> seen = new Dictionary<int, CState>();
        readonly Dictionary<int, float> callAt = new Dictionary<int, float>();
        readonly List<(float at, Vector3 pos, float vol)> thunder = new List<(float, Vector3, float)>();

        Vector3 Ship(float gx, float gy = 0f, float gz = 0f) => ship.WorldPoint(Nautilus.G(gx, gy, gz));

        void Update()
        {
            if (!Bank.Ready || d == null) return;
            if (!made) Make();
            float dt = Time.deltaTime;
            var ear = Sfx.Ear;
            var cam = Camera.main ? Camera.main.transform.position : d.EyeWorld;
            float depth = Mathf.Max(0, -cam.y);
            float storm = Weather.Storm;

            // ---- the beds ----
            bool water = ear == Medium.Water, air = ear == Medium.Air, inside = ear == Medium.Aboard;
            float sh = water ? 1f - Mathf.InverseLerp(35f, 65f, depth) : 0f, kp = water ? Mathf.InverseLerp(35f, 65f, depth) * (1f - Mathf.InverseLerp(130f, 170f, depth)) : 0f, dp = water ? Mathf.InverseLerp(130f, 170f, depth) : 0f;
            var clock = DeepBoot.I.clock;
            shallows.vol = sh * (clock.Night ? 0.75f : 1f); kelp.vol = kp; deep.vol = dp + (inside ? 0.35f : 0f);
            underside.vol = water ? 1f - Mathf.InverseLerp(2f, 14f, depth) : 0f;
            aboardBed.vol = inside ? 1f : 0f;
            // the caves: their own still, dripping bed; the roosts' bats in the air pockets
            bool underground = Caverns.I != null && Caverns.I.UnderGround(cam);
            if (underground) { shallows.vol = kelp.vol = 0; deep.vol *= 0.3f; underside.vol = 0; }
            caves.vol = underground ? 1f : 0f;
            bats.vol = underground && UnderwaterLook.InPocket ? 1f : underground && Caverns.I.PocketSurface(cam) < float.PositiveInfinity ? 0.25f : 0f;
            waves.vol = air ? 0.6f + 0.6f * storm : inside && ship.Depth < 6f ? 0.25f : 0f; waves.pitch = 1f - 0.15f * storm;
            wind.vol = air ? 0.15f + 0.85f * storm : 0f;
            rain.vol = air ? storm * storm : 0f;

            // ---- the ship ----
            var sy = ship.sys;
            if (sy != null)
            {
                float spd = Mathf.Abs(ship.speed);
                engine.pos = Ship(-21f); engine.vol = sy.state == PowerState.Engine ? 0.9f : 0f; engine.pitch = 0.7f + 0.55f * spd / 5.6f;
                battery.pos = Ship(-17.5f); battery.vol = sy.state != PowerState.Dead ? (inside ? 0.7f : 0.25f) : 0f;
                whir.pos = Ship(-33f, -1f); whir.vol = Mathf.Clamp01(spd / 2f) * (sy.state == PowerState.Silent ? 0.8f : 0.5f); whir.pitch = 0.8f + 0.4f * Mathf.Clamp01(spd / 5f);
                pumps.pos = Ship(-27f); pumps.vol = sy.pumpsRunning ? 0.8f : 0f;
                // the flood at the wettest room, the jet at the nearest breach
                float wet = 0; Vector3 wetAt = engine.pos;
                foreach (var r in sy.rooms) if (r.level > wet) { wet = r.level; wetAt = Ship((r.r.x0 + r.r.x1) / 2f, Nautilus.Floor + r.level); }
                flood.pos = wetAt; flood.vol = Mathf.Clamp01(wet / 0.6f);
                ShipSystems.Breach near = null; float nb = float.MaxValue;
                foreach (var b in sy.breaches) { float bd = (ship.WorldPoint(Nautilus.G(b.gen.x, b.gen.y, b.gen.z)) - cam).sqrMagnitude; if (bd < nb) { nb = bd; near = b; } }
                jet.vol = near != null ? 1f : 0f; if (near != null) jet.pos = ship.WorldPoint(Nautilus.G(near.gen.x, near.gen.y, near.gen.z));
                // changes: a new breach, the alarm, the telegraph, her power, the breakers
                if (sy.breaches.Count > lastBreaches && near != null) Sfx.Play("breach", ship.WorldPoint(Nautilus.G(sy.breaches[sy.breaches.Count - 1].gen.x, sy.breaches[sy.breaches.Count - 1].gen.y, sy.breaches[sy.breaches.Count - 1].gen.z)), 1f, 1f, Medium.Aboard);
                lastBreaches = sy.breaches.Count;
                alarmT -= dt;
                if (sy.breaches.Count > 0 && sy.state != PowerState.Dead && alarmT <= 0) { alarmT = 2.6f; Sfx.Play("alarm_bell", Ship(24f, 1.5f), 0.8f, 1f, Medium.Aboard); Sfx.Play("alarm_bell", Ship(-10f, 1.5f), 0.6f, 1f, Medium.Aboard); }
                if (lastTele >= 0 && ship.telegraph != lastTele) { NLStation tel = null; foreach (var s in ship.L.stations) if (s.kind == "telegraph") tel = s; Sfx.Play("telegraph", tel != null ? ship.WorldPoint(Nautilus.G(tel.pos)) + Vector3.up : Ship(25f), 1f, 1f, Medium.Aboard); }
                lastTele = ship.telegraph;
                if (sy.state != lastPower)
                {
                    Sfx.Play("switch", Ship(-17.5f, 0.5f), 1f, 1f, Medium.Aboard);
                    if (lastPower == PowerState.Dead) Sfx.Play("power_up", Ship(-17.5f), 1f, 1f, Medium.Aboard);
                    else if (sy.state == PowerState.Dead) Sfx.Play("power_down", Ship(-17.5f), 1f, 1f, Medium.Aboard);
                    lastPower = sy.state;
                }
                if (lastTripped && !sy.breakersTripped) Sfx.Play("switch", Ship(-17.5f, 0.5f), 1.2f, 0.8f, Medium.Aboard);
                lastTripped = sy.breakersTripped;
                // she creaks under way and deep; past her crush depth she groans and pops
                float stress = Mathf.Clamp01(ship.Depth / Mathf.Max(30f, ship.crushDepth));
                creakT -= dt * (0.4f + spd * 0.3f + stress * 1.2f);
                if (creakT <= 0) { creakT = Random.Range(6f, 16f); Sfx.Play("hull_creak", Ship(Random.Range(-30f, 40f), Random.Range(-2f, 2f), Random.Range(-3f, 3f)), 0.5f + stress * 0.6f, Random.Range(0.85f, 1.15f), Medium.Aboard); }
                if (stress > 0.85f)
                {
                    groanT -= dt * (stress > 1f ? 2.5f : 1f);
                    if (groanT <= 0) { groanT = Random.Range(5f, 11f); Sfx.Play("hull_groan", Ship(Random.Range(-25f, 35f)), 0.7f + 0.3f * Mathf.Clamp01((stress - 0.85f) * 6f), 1f, Medium.Aboard); if (stress > 1f) Sfx.Play("hull_pop", Ship(Random.Range(-25f, 35f), 1f), 1f, 1f, Medium.Aboard); }
                }
                bool grounded = ship.grounded > 0.05f && spd > 0.4f;
                if (grounded && !lastGrounded) Sfx.Play("ground", Ship(30f, -3f), 1f, 1f, Medium.Aboard);
                lastGrounded = grounded;
            }

            // ---- the diver ----
            Diver();
            // ---- the tools at work (this diver's) ----
            var tool = hands != null ? Hands.ToolOf(hands.Held) : "hand";
            var at = cam + (Camera.main ? Camera.main.transform.forward * 0.5f : Vector3.zero);
            drill.pos = at; drill.vol = hands != null && hands.useT > 0 && tool == "drill" ? 1f : 0f; drill.pitch = 1f + 0.05f * Mathf.Sin(Time.time * 9f); drill.medium = Sfx.At(at);
            blade.pos = at; blade.vol = hands != null && hands.useT > 0 && tool == "blade" ? 1f : 0f; blade.medium = Sfx.At(at);
            // ---- the Kite-Sub ----
            var k = KiteSub.I;
            if (k)
            {
                kite.pos = k.transform.position; kite.vol = !k.docked ? Mathf.Clamp01(k.vel.magnitude / 3f) * 0.9f + 0.1f : 0f; kite.pitch = 0.8f + 0.4f * Mathf.Clamp01(k.vel.magnitude / 6f); kite.medium = Medium.Water;
                if (k.docked != lastKiteDocked) Sfx.Play("dock", k.transform.position, 1f, 1f, Medium.Water);
                lastKiteDocked = k.docked;
            }
            else kite.vol = 0;
            // ---- the storm ----
            var wx = Weather.I;
            if (wx)
            {
                if (wx.flash > lastFlash + 0.4f) thunder.Add((Time.time + Random.Range(0.3f, 2.5f), cam + Quaternion.Euler(0, Random.Range(0, 360f), 0) * Vector3.forward * 300f + Vector3.up * 200f, 0.6f + 0.4f * storm));
                lastFlash = wx.flash;
            }
            for (int i = thunder.Count - 1; i >= 0; i--)
                if (Time.time >= thunder[i].at) { var th = thunder[i]; Sfx.Play("thunder", th.pos, th.vol * (air ? 1f : 0.6f), Random.Range(0.85f, 1.1f), Medium.Air); thunder.RemoveAt(i); }
            // ---- the raft ----
            if (d.onRaft != null) { raftCreakT -= dt * (1f + storm * 3f); if (raftCreakT <= 0) { raftCreakT = Random.Range(3f, 8f); Sfx.Play("raft_creak", d.onRaft.transform.position, 0.8f, 1f, Medium.Air); } }
            // ---- the animals ----
            Animals(cam, dt);
            lastEar = ear;
        }

        void Diver()
        {
            float dt = Time.deltaTime;
            var ear = Sfx.Ear;
            // in and out of the water
            if (lastEar == Medium.Air && ear == Medium.Water) Sfx.Play2D("splash_in", 0.8f);
            if (lastEar == Medium.Water && ear == Medium.Air) Sfx.Play2D("splash_out", 0.8f);
            // the regulator: in, then out with a burst of bubbles; quicker when working hard or short of air
            bool breathing = ear == Medium.Water && !d.aboard && d.piloting == null;
            float hard = Mathf.Clamp01((d.vel.magnitude - 2f) / 3f) + (d.oxygen < d.oxygenMax * 0.3f ? 0.6f : 0f);
            breathT -= dt;
            if (breathing && breathT <= 0)
            {
                breathIn = !breathIn;
                Sfx.Play2D(breathIn ? "breath_in" : "breath_out", 0.9f, Random.Range(0.96f, 1.04f));
                breathT = breathIn ? Mathf.Lerp(1.5f, 0.9f, hard) : Mathf.Lerp(2.4f, 1.2f, hard);
            }
            if (!breathing) breathT = Mathf.Min(breathT, 0.6f);
            // strokes
            strokeT -= dt;
            if (breathing && d.vel.magnitude > 1.2f && strokeT <= 0) { strokeT = Mathf.Lerp(1.1f, 0.6f, Mathf.Clamp01(d.vel.magnitude / 5f)); Sfx.Play2D("swim", 0.6f, Random.Range(0.9f, 1.1f)); }
            // the low-air beep and the heartbeat
            beepT -= dt;
            if (breathing && d.oxygen < 12f && beepT <= 0) { beepT = d.oxygen < 5f ? 0.7f : 1.4f; Sfx.Play2D("o2_low"); }
            heartT -= dt;
            if (d.health < 30f && heartT <= 0) { heartT = d.health < 15f ? 0.6f : 0.9f; Sfx.Play2D("heartbeat", 0.8f); }
            if (d.health < lastHealth - 2.5f) Sfx.Play2D("hurt", Mathf.Clamp01((lastHealth - d.health) / 30f) * 0.6f + 0.4f);
            lastHealth = d.health;
            // footsteps aboard: teak in the forward rooms, iron aft, a rug where one lies
            if (d.aboard && !d.climbing)
            {
                var feet = d.transform.position;
                var step = feet - lastFeet; step.y = 0;
                if (d.cc.isGrounded && step.magnitude < 1f) stepDist += step.magnitude;
                if (stepDist > 0.7f)
                {
                    stepDist = 0;
                    var g = Nautilus.FromLocal(ship.Proxy.InverseTransformPoint(feet));
                    string cue = g.x > -1f ? "step_wood" : "step_metal";
                    if (Physics.Raycast(feet, Vector3.down, out var hit, 1.2f) && hit.collider.GetComponent<DecorRef>() is DecorRef dr && Decor.I.placed.TryGetValue(dr.id, out var p) && p.item == "woven_kelp_rug") cue = "step_rug";
                    Sfx.Play(cue, ship.ToWorld(feet - Vector3.up * 0.8f), 1f, Random.Range(0.92f, 1.08f), Medium.Aboard);
                }
                lastFeet = feet;
            }
            else { lastFeet = d.transform.position; stepDist = 0; }
            if (d.aboard && d.climbing)
            {
                ladderDist += Mathf.Abs(d.vel.y) * dt;
                if (ladderDist > 0.45f) { ladderDist = 0; Sfx.Play("ladder", ship.ToWorld(d.transform.position), 1f, Random.Range(0.9f, 1.1f), Medium.Aboard); }
            }
        }

        // the animals near the listener: leviathans call now and then; a hunter turning to the attack growls
        float animT;
        void Animals(Vector3 cam, float dt)
        {
            var life = Life.I; if (life == null) return;
            animT -= dt; if (animT > 0) return; animT = 0.25f;
            rushT -= 0.25f;
            foreach (var c in life.live)
            {
                if (!c.alive) continue;
                float dist = (c.pos - cam).magnitude;
                bool big = c.persistent || c.size > 8f;
                if (big && dist < 900f)
                {
                    if (!callAt.TryGetValue(c.id, out float t)) callAt[c.id] = t = Time.time + Random.Range(8f, 40f);
                    if (Time.time > t) { callAt[c.id] = Time.time + Random.Range(35f, 80f); Sfx.Play("lev_call", c.pos, 1f, Mathf.Clamp(14f / c.size, 0.6f, 1.2f), Medium.Water); }
                }
                seen.TryGetValue(c.id, out var was);
                bool attack = c.state == CState.Hunting || c.state == CState.Frenzy || (c.state == CState.Territorial);
                bool wasAttack = was == CState.Hunting || was == CState.Frenzy || was == CState.Territorial;
                if (attack && !wasAttack && dist < (big ? 160f : 45f) && c.sp.level >= 2 && c.size > 1.2f)
                    Sfx.Play(big ? "lev_roar" : "hunter_growl", c.pos, big ? 1f : Mathf.Clamp01(c.size / 4f) * 0.6f + 0.4f, big ? 1f : Mathf.Clamp(2.5f / c.size, 0.7f, 1.4f), Medium.Water);
                seen[c.id] = c.state;
                // a school bolting past
                if (c.state == CState.Fleeing && c.group != 0 && dist < 5f && rushT <= 0) { rushT = 3f; Sfx.Play("school_rush", c.pos, 1f, 1f, Medium.Water); }
            }
            if (seen.Count > 2000) seen.Clear();
        }
    }
}
