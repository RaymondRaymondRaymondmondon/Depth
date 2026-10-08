// Playing the bank (stage 8). Every sound is placed in the world and heard through what lies between it and the ear:
//   in the water, sound carries far and loses only its brightest edge; through her hull it's a dull thud; across the
//   surface (above water listening down, or below listening up) it's muffled hard. Each voice has a low-pass filter set
//   from the listener's medium and the sound's; the listener has a reverb for its space (the open sea, her rooms).
// Volumes (master, effects, ambience, interface, music) are kept in PlayerPrefs; F10 opens the sliders.
// A shared sound (Shared) is heard by the whole crew: the host passes it on (Net.cs).
using System.Collections.Generic;
using UnityEngine;

namespace Deep
{
    public enum Medium { Water, Aboard, Air }

    public class Sfx : MonoBehaviour
    {
        public static Sfx I;
        const int Voices = 40;
        class Voice { public AudioSource src; public AudioLowPassFilter lp; public Medium medium; public float started; public bool busy; public Bus bus; public float baseVol; }
        readonly List<Voice> voices = new List<Voice>();
        public readonly List<Loop> loops = new List<Loop>();
        AudioReverbFilter reverb; AudioListener listener;
        public static Medium Ear = Medium.Water;
        public static float Master = 0.9f, FxVol = 1f, AmbVol = 0.8f, UiVol = 0.8f, MusicVol = 0.6f;
        bool showPanel;

        public static Sfx Attach()
        {
            var go = new GameObject("Sound");
            I = go.AddComponent<Sfx>();
            Master = PlayerPrefs.GetFloat("deep.master", 0.8f); FxVol = PlayerPrefs.GetFloat("deep.fx", 1f);
            AmbVol = PlayerPrefs.GetFloat("deep.amb", 0.8f); UiVol = PlayerPrefs.GetFloat("deep.ui", 0.8f); MusicVol = PlayerPrefs.GetFloat("deep.music", 0.6f);
            for (int i = 0; i < Voices; i++) I.voices.Add(I.MakeVoice("voice " + i));
            Bank.BeginRender();
            return I;
        }

        Voice MakeVoice(string name)
        {
            var g = new GameObject(name); g.transform.SetParent(transform);
            var s = g.AddComponent<AudioSource>(); s.playOnAwake = false; s.dopplerLevel = 0; s.rolloffMode = AudioRolloffMode.Logarithmic;
            var lp = g.AddComponent<AudioLowPassFilter>(); lp.cutoffFrequency = 22000;
            return new Voice { src = s, lp = lp };
        }

        public static float BusVol(Bus b) => Master * (b == Bus.Fx ? FxVol : b == Bus.Amb ? AmbVol : b == Bus.Ui ? UiVol : MusicVol);

        // what the space between does to a sound: the low-pass cutoff (Hz)
        public static float Cutoff(Medium ear, Medium src)
        {
            if (ear == Medium.Water) return src == Medium.Water ? 6000f : src == Medium.Aboard ? 900f : 700f;
            if (ear == Medium.Aboard) return src == Medium.Aboard ? 22000f : src == Medium.Water ? 650f : 1400f;
            return src == Medium.Air ? 22000f : src == Medium.Aboard ? 1100f : 450f;
        }

        // where a point is (for a sound with no medium given)
        public static Medium At(Vector3 p) => p.y < Sea.SurfaceAt(p) - 0.2f ? Medium.Water : Medium.Air;

        // ---- one-shots ---------------------------------------------------------------------------------------------
        public static AudioSource Play(string cue, Vector3 pos, float vol = 1f, float pitch = 1f, Medium? medium = null)
        {
            if (I == null || !Bank.Ready || !Bank.Cues.TryGetValue(cue, out var c)) return null;
            var clip = Bank.Clip(cue); if (!clip) return null;
            var v = I.Free();
            var m = medium ?? At(pos);
            v.medium = m; v.bus = c.bus; v.baseVol = c.vol * vol; v.busy = true; v.started = Time.time;
            var s = v.src;
            s.transform.position = pos; s.clip = clip; s.loop = false; s.pitch = pitch * Random.Range(0.97f, 1.03f);
            s.spatialBlend = 1f; s.minDistance = c.near; s.maxDistance = c.far * (m == Medium.Water ? 1.4f : 1f);
            s.volume = v.baseVol * BusVol(c.bus); s.bypassListenerEffects = false;
            v.lp.cutoffFrequency = Cutoff(Ear, m);
            s.Play();
            return s;
        }
        // in the ear (the interface, the diver's own breath)
        public static AudioSource Play2D(string cue, float vol = 1f, float pitch = 1f)
        {
            if (I == null || !Bank.Ready || !Bank.Cues.TryGetValue(cue, out var c)) return null;
            var clip = Bank.Clip(cue); if (!clip) return null;
            var v = I.Free();
            v.medium = Ear; v.bus = c.bus; v.baseVol = c.vol * vol; v.busy = true; v.started = Time.time;
            var s = v.src; s.clip = clip; s.loop = false; s.pitch = pitch; s.spatialBlend = 0f;
            s.volume = v.baseVol * BusVol(c.bus); s.bypassListenerEffects = c.bus == Bus.Ui || c.bus == Bus.Music;
            v.lp.cutoffFrequency = 22000f;
            s.Play();
            return s;
        }
        // heard by the whole crew
        public static void Shared(string cue, Vector3 pos, float vol = 1f, float pitch = 1f, Medium? medium = null)
        {
            var m = medium ?? At(pos);
            Play(cue, pos, vol, pitch, m);
            Net.Sound(cue, pos, vol, pitch, (int)m);
        }
        public static void UI(string cue, float vol = 1f) => Play2D(cue, vol);

        Voice Free()
        {
            Voice oldest = null;
            foreach (var v in voices)
            {
                if (!v.src.isPlaying) return v;
                if (oldest == null || v.started < oldest.started) oldest = v;
            }
            oldest.src.Stop();
            return oldest;
        }

        // ---- loops (machinery, beds): kept going, the caller sets their volume, pitch and place each frame ------------
        public class Loop
        {
            public AudioSource src; public AudioLowPassFilter lp; public Cue cue;
            public float vol, pitch = 1f; public Vector3 pos; public Medium medium = Medium.Water; public bool spatial = true;
            float shown;
            public void Step(float dt)
            {
                if (src == null) return;
                shown = Mathf.MoveTowards(shown, vol, dt * 1.5f);
                if (shown <= 0.001f) { if (src.isPlaying) src.Pause(); return; }
                if (!src.isPlaying) { if (src.clip == null) src.clip = Bank.Clip(cue.name, 0); if (src.clip) { src.time = Random.Range(0, src.clip.length * 0.9f); src.Play(); } }
                src.volume = shown * cue.vol * BusVol(cue.bus);
                src.pitch = pitch;
                src.spatialBlend = spatial ? 1f : 0f;
                src.transform.position = pos;
                lp.cutoffFrequency = spatial ? Cutoff(Ear, medium) : 22000f;
            }
        }
        public static Loop MakeLoop(string cue)
        {
            if (I == null || !Bank.Cues.TryGetValue(cue, out var c)) return null;
            var g = new GameObject("loop " + cue); g.transform.SetParent(I.transform);
            var s = g.AddComponent<AudioSource>(); s.playOnAwake = false; s.loop = true; s.dopplerLevel = 0; s.rolloffMode = AudioRolloffMode.Logarithmic;
            s.minDistance = c.near; s.maxDistance = c.far; s.bypassListenerEffects = c.bus == Bus.Music;
            var lp = g.AddComponent<AudioLowPassFilter>();
            var l = new Loop { src = s, lp = lp, cue = c };
            I.loops.Add(l);
            return l;
        }

        // ---- every frame ---------------------------------------------------------------------------------------------
        void Update()
        {
            if (!Bank.Ready) Bank.Finish();
            var d = DeepBoot.I ? DeepBoot.I.diver : null;
            Ear = d && d.aboard && !d.HeadUnderAboard ? Medium.Aboard : d && d.piloting != null ? Medium.Aboard : UnderwaterLook.Underwater ? Medium.Water : Medium.Air;
            // the listener's space
            var cam = Camera.main;
            if (cam && (!listener || listener.gameObject != cam.gameObject))
            {
                listener = cam.GetComponent<AudioListener>();
                reverb = cam.GetComponent<AudioReverbFilter>();
                if (!reverb) reverb = cam.gameObject.AddComponent<AudioReverbFilter>();
                if (!cam.GetComponent<Limiter>()) cam.gameObject.AddComponent<Limiter>();
            }
            // the caves ring (the doc: echoes amplify every sound)
            bool caves = cam && Caverns.I != null && Caverns.I.UnderGround(cam.transform.position);
            if (reverb) reverb.reverbPreset = Ear == Medium.Aboard ? AudioReverbPreset.Hallway : caves ? (Ear == Medium.Air ? AudioReverbPreset.Cave : AudioReverbPreset.StoneCorridor) : Ear == Medium.Water ? AudioReverbPreset.Underwater : AudioReverbPreset.Plain;
            float dt = Time.deltaTime;
            foreach (var l in loops) l.Step(dt);
            // the medium can change under a playing sound (surfacing, coming aboard)
            foreach (var v in voices) if (v.src.isPlaying && v.src.spatialBlend > 0.5f) { v.lp.cutoffFrequency = Mathf.Lerp(v.lp.cutoffFrequency, Cutoff(Ear, v.medium), dt * 8f); v.src.volume = v.baseVol * BusVol(v.bus); }
            if (Input.GetKeyDown(KeyCode.F10)) { showPanel = !showPanel; if (d) d.uiOpen = showPanel; Cursor.lockState = showPanel ? CursorLockMode.None : CursorLockMode.Locked; Cursor.visible = showPanel; }
        }

        // ---- the volume sliders (F10) ----------------------------------------------------------------------------------
        void OnGUI()
        {
            if (!showPanel) return;
            var r = new Rect(Screen.width / 2 - 220, Screen.height / 2 - 150, 440, 280);
            GUI.color = new Color(0.06f, 0.05f, 0.04f, 0.92f); GUI.DrawTexture(r, Texture2D.whiteTexture); GUI.color = Color.white;
            var t = new GUIStyle(GUI.skin.label) { fontSize = 18, fontStyle = FontStyle.Bold }; t.normal.textColor = new Color(0.95f, 0.85f, 0.6f);
            var s = new GUIStyle(GUI.skin.label) { fontSize = 14 }; s.normal.textColor = new Color(0.9f, 0.88f, 0.8f);
            GUI.Label(new Rect(r.x + 20, r.y + 12, 400, 26), "Sound", t);
            float y = r.y + 50;
            float Row(string n, float v, string key)
            {
                GUI.Label(new Rect(r.x + 20, y, 120, 22), n, s);
                float nv = GUI.HorizontalSlider(new Rect(r.x + 140, y + 6, 220, 18), v, 0f, 1f);
                GUI.Label(new Rect(r.x + 370, y, 50, 22), $"{nv * 100:0}%", s);
                if (Mathf.Abs(nv - v) > 1e-4f) PlayerPrefs.SetFloat(key, nv);
                y += 36; return nv;
            }
            Master = Row("Master", Master, "deep.master");
            FxVol = Row("Effects", FxVol, "deep.fx");
            AmbVol = Row("Ambience", AmbVol, "deep.amb");
            UiVol = Row("Interface", UiVol, "deep.ui");
            MusicVol = Row("Music", MusicVol, "deep.music");
            GUI.Label(new Rect(r.x + 20, r.yMax - 30, 400, 22), "F10 to close", s);
        }
    }
}
