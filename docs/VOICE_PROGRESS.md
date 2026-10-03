# Voice chat (the Deep Arcade)

The Trawl's design doc makes talking the core skill ("the game carries it, not a third-party app"): proximity voice
through the shared arcade layer, walkies, a speaking tube, ghosts. Red Tide's divers talk over it too ("the players
supply the rest over voice chat"; the mouth moves with voice chat). Built in steps:

1. **The core (done, 2026-10-02):** capture, codec, gate, relay, playback, settings, the "who's talking" strip.
2. **The Trawl's proximity voice:** 12 m on deck, halved by heavy rain or the engine at full, muffled behind walls,
   bubbling for a hand in the water, walkies (bought, a channel, batteries), the speaking tube, ghosts' cold reverb.
3. **Red Tide:** the helmet radio, the divers' mouths.

## Step 1: the core
- **Files:** `voice.h/.cpp` (no raylib: miniaudio's header pulls in windows.h; the miniaudio functions are raylib's own,
  built by raudio.c with the same switches), `voice_test.cpp`, the glue in `arcade.cpp` (`ArcadeVoiceFrame`,
  `DrawVoiceHud`), the session's `M_VOICE` (`arcade_session.*`), the mixer's voice bus (`sound.cpp`), the menu's Voice
  chat page (`menu.cpp`), the Talk action and the settings (`input.*`).
- **Codec:** IMA ADPCM at 16 kHz mono, 4 bits a sample, 20 ms frames of 164 bytes (about 66 kbit/s per speaker, fine
  on a LAN or ZeroTier). Each frame carries its first sample and step index, so a lost packet costs only its own 20 ms.
  Speech-like test signal: 23 dB SNR. (Opus would be a quarter of the size; there's no library for it here yet.)
- **The microphone** opens only while voice is on and there's a table (lobby or match), or during the menu's mic test.
- **Sending:** push-to-talk (the Talk action: Caps lock or the backtick, rebindable in Controls) or an open mic behind
  a gate (the sensitivity sets the threshold, -45 to -15 dBFS; it holds 300 ms after the voice drops). Frames go to the
  host on the unreliable channel; the host hears them and passes them to every other seat, never back to the speaker;
  the host's own voice goes to every guest. Speakers are lobby seats.
- **Playing:** a jitter buffer per speaker (60 ms before the first word; a missing frame repeated quieter, up to three;
  more than 240 ms behind drops the oldest to catch up; the 16-bit sequence wraps), resampled 16 kHz -> 44.1 kHz into
  the voice bus, which ducks the rest 4 dB while anyone is heard. `voice::Hearing` shapes each speaker: gain, pan,
  muffle (a low-pass 7 kHz -> 400 Hz), radio (a band-limited, crushed, crackling walkie), bubble (a voice in water),
  ghost (a hollow ringing delay). A game sets it every frame; it lapses to plain after half a second (the lobby).
  `voice::Speaking`/`Level` read the voice before its gain, so a mouth moves even out of earshot.
- **Settings** (settings.txt `voice` and the fifth `audio` value): microphone on/off, push-to-talk or open mic,
  sensitivity with a live meter and the gate's mark, the voices' volume, "Test the mic" (you hear yourself as the
  table will). Shot `menu_voice`.
- **Check:** `depth.exe --voice-test` (the codec, the gate, in-order/lost/reordered frames, the wrap, out of earshot,
  each shaping, and the session relay with a host and two guests in memory). The session's protocol changed (a new
  message), so friends need the same build, as always.
