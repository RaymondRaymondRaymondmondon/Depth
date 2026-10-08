// The music (stage 8): generative, from the bank's instruments (one sampled note each, played at other pitches), timed on
// the audio clock (PlayScheduled) so it never drifts. Like the sea itself it comes and goes: a piece plays for two or
// three minutes, then there's only the water for a minute or two. What it plays follows where you are:
//   the Shallows by day   - D lydian: a warm pad, harp and celesta arpeggios drifting high, now and then a piano phrase
//   the Kelp, and night   - A dorian / C# minor: a darker pad, a low drone, a slow cello line, sparse celesta
//   aboard her            - Captain Nemo's organ: a slow chorale in B flat, four voices moving under a held top line
//   the storm (the raft)  - a drone and cello swells, no tune
//   danger                - at once, whatever was playing: a sting, then a low drone, a cello ostinato of two notes a
//                           semitone apart and piano clusters, until things are calm for ten seconds
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public class Score : MonoBehaviour
    {
        public static Score I;
        public enum Mood { None, Shallows, Dark, Aboard, Storm, Danger }
        public Mood mood = Mood.None, playing = Mood.None;
        readonly List<AudioSource> pool = new List<AudioSource>();
        int next;
        double barAt; int bar; float pieceLeft, restLeft = 20f, dangerCalm;
        Diver d;
        static readonly Dictionary<string, int> Base = new Dictionary<string, int> { { "ins_organ", 48 }, { "ins_pad", 48 }, { "ins_celesta", 72 }, { "ins_harp", 60 }, { "ins_cello", 48 }, { "ins_drone", 36 }, { "ins_piano", 60 }, { "sting_danger", 36 } };
        System.Random rnd = new System.Random();

        public static Score Attach(Diver d)
        {
            var s = new GameObject("Score").AddComponent<Score>(); I = s; s.d = d;
            for (int i = 0; i < 24; i++)
            {
                var g = new GameObject("music " + i); g.transform.SetParent(s.transform);
                var a = g.AddComponent<AudioSource>(); a.playOnAwake = false; a.spatialBlend = 0; a.bypassListenerEffects = true; a.bypassReverbZones = true;
                s.pool.Add(a);
            }
            return s;
        }

        // (the harness: start a piece now)
        public void Kick() { restLeft = 0; if (playing != Mood.Danger) playing = Mood.None; }

        // one note: an instrument at a MIDI pitch, at a moment on the audio clock
        void Note(string ins, int midi, double at, float vol)
        {
            var clip = Bank.Clip(ins, 0); if (!clip) return;
            var a = pool[next]; next = (next + 1) % pool.Count;
            a.Stop(); a.clip = clip;
            a.pitch = Mathf.Pow(2f, (midi - Base[ins]) / 12f);
            a.volume = vol * Sfx.BusVol(Bus.Music);
            a.PlayScheduled(at);
        }

        // ---- choosing the mood --------------------------------------------------------------------------------------
        Mood Want()
        {
            if (Danger()) return Mood.Danger;
            if (d.onRaft != null || (Opening.Active && !d.aboard && Sfx.Ear == Medium.Air)) return Mood.Storm;
            if (d.aboard) return Mood.Aboard;
            var clock = DeepBoot.I.clock;
            return d.Depth > 55f || clock.Night ? Mood.Dark : Mood.Shallows;
        }

        // something is coming for this diver: a hunter in its attack within 40 m, or a leviathan roused within 150 m
        bool Danger()
        {
            var life = Life.I; if (life == null || d.aboard) return false;
            var eye = d.EyeWorld;
            foreach (var c in life.live)
            {
                if (!c.alive || c.sp.level < 2) continue;
                bool attack = c.state == CState.Hunting || c.state == CState.Frenzy || c.state == CState.Territorial;
                if (!attack) continue;
                float r = c.persistent || c.size > 8f ? 150f : 40f;
                if ((c.pos - eye).sqrMagnitude < r * r && c.size > 1f) return true;
            }
            return false;
        }

        // ---- the scales and progressions ----------------------------------------------------------------------------
        static readonly int[] Lydian = { 0, 2, 4, 6, 7, 9, 11 }, Dorian = { 0, 2, 3, 5, 7, 9, 10 }, Minor = { 0, 2, 3, 5, 7, 8, 10 }, Major = { 0, 2, 4, 5, 7, 9, 11 };
        static int Deg(int root, int[] scale, int degree) { int o = Mathf.FloorToInt(degree / 7f); int k = ((degree % 7) + 7) % 7; return root + scale[k] + 12 * o; }
        int R(int n) => rnd.Next(n);

        void Update()
        {
            if (!Bank.Ready || d == null) return;
            double now = AudioSettings.dspTime;
            float dt = Time.deltaTime;
            var want = Want();
            // danger takes over at once, and lets go after ten calm seconds
            if (want == Mood.Danger) { dangerCalm = 10f; if (playing != Mood.Danger) { Note("sting_danger", 36, now + 0.05, 0.55f); playing = Mood.Danger; barAt = now + 1.2; bar = 0; pieceLeft = 9999; } }
            else if (playing == Mood.Danger) { dangerCalm -= dt; if (dangerCalm <= 0) { playing = Mood.None; restLeft = 12f; } }
            if (playing != Mood.Danger)
            {
                // a piece, then a rest; a change of place ends the piece at its next bar
                if (playing == Mood.None)
                {
                    restLeft -= dt;
                    if (restLeft <= 0 && want != Mood.None) { playing = want; pieceLeft = want == Mood.Aboard ? 150f : 120f + R(80); barAt = now + 0.2; bar = 0; }
                }
                else
                {
                    pieceLeft -= dt;
                    if (pieceLeft <= 0 || want != playing) { playing = Mood.None; restLeft = want != playing && pieceLeft > 0 ? 6f : 60f + R(90); }
                }
            }
            mood = playing;
            if (playing == Mood.None) return;
            // write the next bar when the current one is nearly done
            while (barAt - now < 0.6) { Bar(barAt); barAt += BarLen(); bar++; }
        }

        double BarLen() => playing == Mood.Aboard ? 4.8 : playing == Mood.Danger ? 2.0 : playing == Mood.Storm ? 6.0 : 4.0;

        void Bar(double at)
        {
            double len = BarLen();
            switch (playing)
            {
                case Mood.Shallows:
                {
                    int root = 50;                                   // D
                    int[] prog = { 0, 1, 5, 3 }; int ch = prog[(bar / 2) % prog.Length];
                    if (bar % 2 == 0) foreach (int k in new[] { 0, 2, 4 }) Note("ins_pad", Deg(root, Lydian, ch + k), at, 0.16f);
                    int n = 3 + R(4);
                    for (int i = 0; i < n; i++) Note(R(3) == 0 ? "ins_celesta" : "ins_harp", Deg(root + 12, Lydian, ch + new[] { 0, 2, 4, 7, 9 }[R(5)]), at + len * R(8) / 8.0, 0.18f + 0.08f * (float)rnd.NextDouble());
                    if (bar % 8 == 5) for (int i = 0; i < 4; i++) Note("ins_piano", Deg(root + 12, Lydian, ch + 4 - i), at + i * len / 4, 0.22f);
                    break;
                }
                case Mood.Dark:
                {
                    var clock = DeepBoot.I.clock;
                    int root = clock.Night ? 49 : 45; var scale = clock.Night ? Minor : Dorian;
                    int[] prog = { 0, 5, 3, 4 }; int ch = prog[(bar / 2) % prog.Length];
                    if (bar % 4 == 0) Note("ins_drone", root - 12, at, 0.18f);
                    if (bar % 2 == 0) foreach (int k in new[] { 0, 2, 4 }) Note("ins_pad", Deg(root, scale, ch + k), at, 0.12f);
                    if (bar % 2 == 1) Note("ins_cello", Deg(root - 12, scale, ch + new[] { 0, 2, 4 }[R(3)]), at, 0.22f);
                    if (R(2) == 0) Note("ins_celesta", Deg(root + 12, scale, ch + new[] { 0, 2, 4, 6 }[R(4)]), at + len * R(4) / 4.0, 0.14f);
                    break;
                }
                case Mood.Aboard:
                {
                    // the organ: a chorale in B flat, I vi IV V I iii IV V, voiced in four parts
                    int root = 46;
                    int[] prog = { 0, 5, 3, 4, 0, 2, 3, 4 }; int ch = prog[bar % prog.Length];
                    float vol = d.ship && d.ship.power ? 0.2f : 0.12f;
                    Note("ins_organ", Deg(root - 12, Major, ch), at, vol);
                    Note("ins_organ", Deg(root, Major, ch + 2), at, vol * 0.8f);
                    Note("ins_organ", Deg(root, Major, ch + 4), at, vol * 0.8f);
                    Note("ins_organ", Deg(root + 12, Major, ch + (bar % 3 == 0 ? 0 : 2)), at, vol * 0.7f);
                    if (bar % 2 == 1) Note("ins_organ", Deg(root + 12, Major, ch + 4), at + len / 2, vol * 0.6f);
                    break;
                }
                case Mood.Storm:
                {
                    int root = 38;
                    if (bar % 2 == 0) Note("ins_drone", root - 2, at, 0.2f);
                    Note("ins_cello", root + new[] { 0, 1, 3, 0 }[bar % 4], at + 0.3, 0.2f);
                    break;
                }
                case Mood.Danger:
                {
                    int root = 38;
                    if (bar % 4 == 0) Note("ins_drone", root - 2, at, 0.25f);
                    for (int i = 0; i < 4; i++) Note("ins_cello", root + (i % 2), at + i * len / 4, 0.22f);
                    if (bar % 2 == 0) { Note("ins_piano", root - 12, at, 0.3f); Note("ins_piano", root - 11, at, 0.25f); }
                    break;
                }
            }
        }
    }
}
