#pragma once
#include <string>
#include <utility>
#include <vector>
// The parkour section's sound, all synthesized in code (sound.cpp): one stereo stream mixing a pool of voices
// (oscillators, noise, formant filters, pitch and filter sweeps), a generative ambient score and an ambience bed
// per level, a reverb send, and an underwater low-pass. Nothing is loaded from disk.
#include "raylib.h"

enum class Sfx {
    Jump, Land, HardLand, Step, WallJump, Dash, Roll, Stun, Grab, Backflip, Glide, Splash, Wade,
    Death, DeathWater, Respawn, Win, Launch, SoftLand,
    Pistol, Blunderbuss, Cannon, Torpedo, BombThrow, Blast, Ink, Barrel, Crumble,
    BossHit, KrakenRoar, KrakenWarn, KrakenSwipe, KrakenDeath, BBGrowl, BBCharge, BBCrash, BBHurt, BBDeath,
    Bubbles, PirateCry, BirdSquawk, Clank,
    COUNT
};
enum BeastCue { CUE_CALL, CUE_ALARM, CUE_STRIKE, CUE_PAIN, CUE_DEATH, CUE_CHEW, CUE_GRAB };

void SetAudioSuppressed(bool on);        // no cues while on (a silent simulation)
struct AudioVolumes { float master = 0.8f, music = 0.6f, sfx = 0.8f, ambience = 0.65f, voice = 0.9f; };   // (voice: the arcade's voice chat)
AudioVolumes& Volumes();

void AudioInit();                       // after InitAudioDevice; a no-op without a device
void AudioClose();
void AudioFrame(float dt, bool parkour); // keep the stream fed; outside the parkour section the score and ambience fade out
void AudioLevel(int level);             // which score and ambience (a PL_ level, or PL_COUNT for the Abyss)
void AudioListener(Vector2 at);         // where the diver's ears are (world px)
void AudioDay(float daylight);          // 0 night .. 1 noon (the Island's crickets and flutes come out at night)
void AudioFlow(float amount);           // a draught or current around the diver, 0..1 (a subtle whoosh)
void AudioSlide(float amount);          // the scrape of a slide, 0..1
void AudioTension(float amount);        // an apex predator is near: the score tightens, 0..1
void SfxAt(Sfx s, Vector2 at, float vol = 1, float pitch = 1);
void Sfx2D(Sfx s, float vol = 1, float pitch = 1, float pan = 0);
void BeastSound(const char* name, float size, int cue, Vector2 at, float vol = 1); // every species gets its own voice from its name and size
float BeastCallRate(const char* name);  // how often it calls, per second, when nothing is happening
bool BeastIsSilent(const char* name);    // plants and the like: no calls
void AbyssSound(int kind, int cue, float dist, float pan); // the Abyss's creatures (AbyssCreatureKind)
bool AudioSelfTest(const char* wavPath); // depth.exe --audio-test [out.wav]

// ---------------------------------------------------------------- the whole game's sound (Master Reference, "Sound design")
// Buses: music, ambience, effects, UI, voice. Effects duck the music 3 dB on combat impacts; voice ducks everything 4 dB.
enum CueBus { CB_SFX, CB_MUSIC, CB_AMB, CB_UI, CB_VOICE };
// How a registered cue is synthesized (sound.cpp builds each from these recipes and the cue's numbers).
enum CueRecipe {
    CR_TICK, CR_CLICK, CR_CONFIRM, CR_CANCEL, CR_ERROR, CR_WHOOSH, CR_LATCH, CR_PLAQUE, CR_CREAK, CR_SONAR, CR_ORGAN2,
    CR_FLARE, CR_JELLYHUM, CR_GAUGE, CR_WHEEL, CR_RUNGS, CR_SPOOL, CR_TILT, CR_CLOCK, CR_STEP, CR_GEAR, CR_DOOR,
    CR_LADDER, CR_FANFARE, CR_CHALK, CR_PLUCK, CR_BELL, CR_THUD, CR_TOKEN, CR_DRUM,
    // expeditions (stage 8): footsteps by surface, the diver's helmet, the torch, the scope's static; attack families,
    // impacts by material, combat statuses, the crew's cries, and the music's stingers
    CR_STEP_STONE, CR_STEP_SAND, CR_STEP_KELP, CR_STEP_MARBLE, CR_BREATH, CR_TORCH, CR_STATIC,
    CR_HIT_SLASH, CR_HIT_THRUST, CR_HIT_BLUNT, CR_HIT_SHOT, CR_HIT_THROW, CR_HIT_CAST, CR_HIT_SONG,
    CR_IMP_FLESH, CR_IMP_SHELL, CR_IMP_METAL, CR_IMP_STONE, CR_CRACK, CR_DRIPS, CR_BUBBLING, CR_CHIME, CR_RIPPLE, CR_HEART,
    CR_GRUNT, CR_FALL, CR_STAB, CR_GONG, CR_SWELL, CR_PHASE,
    CR_COUNT
};
// One named cue: its bus, its recipe and base numbers, how it varies (random pitch +-pitchVar, and `variants`
// filter/length variants), its priority (low ones are dropped first when the 24 effect voices are full), how many of
// it may sound at once, and whether it ducks the music.
struct CueDef { const char* name; int bus, recipe; float freq, dur, gain, pitchVar; int variants, priority, maxSim; bool duck; };
const CueDef* CueTable(int& count);     // data.cpp: the registry
int CueIndex(const char* name);         // -1 if unknown
void PlayCue(const char* name, float vol = 1, float pan = 0);
// Outside the parkour section: the salon's waltz and ambience bed. station: the station screen that is open
// (a Scene as int, -1 for none) - it adds its motif; mourning: a crew member died, the organ plays alone.
void AudioHub(bool on, int station, bool mourning);
void AudioStudy(bool on);                // the Study's soundscape bus (crossfades with the salon over 1.5 s)
enum ReverbRoom { RR_SALON, RR_CAVE, RR_KELP, RR_HALL, RR_OPENSEA, RR_COUNT };
void AudioRoom(int room);               // the generated convolution reverb's room

// ---------------------------------------------------------------- expeditions (stage 8)
// What the expedition's music and ambience need to know, set every frame by the Dungeon scene (dungeon.cpp
// ExpeditionAudioState). mode: 0 walking, the chart and events; 1 combat; 2 the results (quiet).
struct ExpAudio {
    bool on = false; int loc = 0, mode = 0;
    float light = 1, bossNear = 0;    // light 0..1; how near the boss room is, 0..1
    int bossType = -1; bool phase2 = false;   // a boss fight: its EnemyType, and whether it's past its turn
    bool winning = false, danger = false, door = false;
};
void AudioExpedition(const ExpAudio& a);
void CombatVoice(int enemyType, float size, int cue, float pan);   // an enemy's voice (CUE_ALARM), pain (CUE_PAIN) or death (CUE_DEATH)
float AudioBeat();          // the expedition music's beat, 1 on it and fading (the Cave's mould glows in time)
void AudioReact(int kind);  // 0: the Island's chant rises (its Shaman heals); 1: whale calls answer the Siren

// ---------------------------------------------------------------- Red Tide (the Deep Arcade's shooter, stage 9)
// Set every frame by the Red Tide scene (redtide_game.cpp); main.cpp turns it off elsewhere.
// map: 0 ship, 1 cave, 2 reef, 3 atlantis, 4 void. mode: 0 the calm, 1 a tide, 2 a Hunt, 3 the match is over.
struct RtAudio {
    bool on = false; int map = 0, mode = 0, tide = 1;
    float quota = 0;            // 0..1 of the tide's quota taken
    float scent = 0;            // 0..1 blood in the water around the diver
    float predator = 0, predatorPan = 0;   // 0..1 how near an apex predator is, and which side
    bool boss = false; int bossPhase = 1;
    bool downed = false; float hp = 1;     // the listening diver
    float countdown = 0;        // the calm: seconds before the next tide
};
enum RtCue { RTC_SHOT, RTC_HARPOON, RTC_HIT, RTC_WALL, RTC_BLAST, RTC_ARC, RTC_MELEE, RTC_CRATE, RTC_PICKUP, RTC_TONIC, RTC_DROP, RTC_ARRIVAL,
             RTC_HAZARD, RTC_BELL, RTC_CLEAR, RTC_KILL, RTC_DOWN, RTC_REVIVE, RTC_EMPTY, RTC_RELOAD, RTC_COUNT };
void AudioRedTide(const RtAudio& a);
void RedTideCue(int kind, float vol, float pan, float dist);              // an effect from `dist` metres, panned
void RedTideBeast(const char* species, float size, int cue, float dist, float pan);   // a species' voice (BeastCue); muffled past 40 m
void RedTideQuip(int voice, int syllables, float pan);                    // a diver's babble (0 Diver, 1 Whaler, 2 Stowaway, 3 Mechanic)
void RedTideSpeciesForAudio(int map, std::vector<std::pair<std::string, int>>& out);   // redtide_game.cpp: --audio-test's species

// ---------------------------------------------------------------- the Trawl (the Deep Arcade's fishing game; design doc "Sound design")
// Set every frame by the Trawl scene (trawl.cpp TrawlAudioFrame); main.cpp turns it off elsewhere.
// mode: 0 the dock, 1 sailing out or home, 2 the night, 3 the deadline met, 4 the deadline missed.
struct TwAudio {
    bool on = false; int mode = 0, ground = 0, verse = 0;   // verse: deadlines met (the shanty gains a verse each)
    float homeward = 0;         // 1 when running for harbour at the night's end (the refrain slows)
    float tension = 0;          // 0..1 the biggest fight's line tension against its rating (a fish over 20 kg)
    bool fishOn = false;        // a fish over 20 kg on a line
    float threat = 0;           // 0..1 a Wake-sized threat within 60 m: the music goes silent
    bool bigThree = false;      // the Great White, the Ghost Ship or the Kraken engaged
    int telegraph = 0;          // 0 stop .. 3 full, -1 astern
    float roll = 0, bilge = 0;  // degrees of roll (hull creaks), 0..1 water in her (slosh)
    int weather = 0;            // Weather as int (wind and rain in the bed)
    float gulls = 0, barracuda = 0;   // 0..1 a flock overhead; a pack ticking on the hull mic
    int canoe = 0;              // 1 drums on the water, 2 the canoe alongside
    float clock = 0;            // 0..1 through the night (gulls at dusk, the refrain slower at the end)
    bool moored = false;
    float siren = 0, sirenPan = 0;   // (the Weeds) a Siren singing: 0..1 by nearness, and from which side
    bool mermen = false;        // splashing at the cod end
    bool tangled = false;       // a hand in a Kelp Wraith's grip
};
enum TwCue { TWC_REEL, TWC_DRAG, TWC_HUM, TWC_SNAP, TWC_CREAK, TWC_SPLASH, TWC_GAFF, TWC_FLOP, TWC_BITE, TWC_STRIKE,
             TWC_TELEGRAPH, TWC_VALVE, TWC_HULL, TWC_PUMP, TWC_WINCH, TWC_WARP, TWC_SNAG, TWC_CODEND,
             TWC_RIFLE, TWC_SHOTGUN, TWC_SPEAR, TWC_HARPOON, TWC_CHARGE, TWC_FLARE,
             TWC_BUMP, TWC_TICKS, TWC_GULL, TWC_OVERBOARD, TWC_RING, TWC_BELL, TWC_TAPE, TWC_SELL, TWC_FANFARE, TWC_CHURCH, TWC_CANOE, TWC_DEATH,
             TWC_THUNDER, TWC_CASE,
             TWC_COUNT };
void AudioTrawl(const TwAudio& a);
void TrawlCue(int kind, float vol, float pan, float pitch = 1);   // an effect; pitch scales its frequencies (the reel's ratchet by tension)

// ---------------------------------------------------------------- the Flight (the Deep Arcade's bird RTS; doc p32 "Sound")
// Set every frame by the Flight scene (flight_game.cpp FlightAudioFrame); main.cpp turns it off elsewhere.
struct FlAudio {
    bool on = false;
    float altitude = 0, speed = 0, wind = 0;   // the Founder's height (m) and airspeed; the wind (m/s): the wind bed rises with height
    float dayPhase = 0.3f;      // 0..1 through the game day (a dawn chorus, a dusk hush, the night)
    float colony = 0;           // your colony's birds (the theme layers with it)
    float chorus = 0;           // 0..1 a colony's chorus near you (its size and nearness: how a scout hears one first)
    float hungry = 0;           // 0..1 your chicks' hunger near you (peeps before the panel flashes)
    float surf = 0;             // 0..1 nearness to a shore; surfType 0 sand, 1 cliff, 2 reef
    int surfType = 0;
    float town = 0;             // 0..1 nearness to a fishing town (rigging on its boats)
    float cove = 0;             // 0..1 nearness to the kraken's cove while it sleeps (its slow breathing)
    int kraken = 0;             // 0 none near, 1 awake near, 2 surfaced near (its theme)
    float war = 0;              // 0..1 flocks engaged near you (the war motif)
    bool storm = false, fog = false;
    bool underwater = false;    // the Founder in the water (a strike, a struggle): everything muffled
    int over = 0;               // 1 a won match, 2 a lost one
    int founderVoice = 0;       // the founder's species (its signature call)
};
enum FlCue { FLC_FLAP, FLC_DIVE, FLC_SPLASH, FLC_STRUGGLE, FLC_SLAP, FLC_CALL, FLC_PEEP, FLC_WINGBEATS, FLC_SHRIEK, FLC_HIT,
             FLC_FALL, FLC_NET, FLC_BOMB, FLC_BURN, FLC_SCREAM, FLC_ROUT, FLC_ROAR, FLC_REPORT, FLC_MAP, FLC_PEARL,
             FLC_ERUPT, FLC_THUNDER, FLC_ROCK, FLC_BELL, FLC_LAND, FLC_HATCH, FLC_EGG, FLC_DEATH, FLC_COUNT };
void AudioFlight(const FlAudio& a);
void FlightCue(int kind, float vol, float pan, float pitch = 1);
void FlightSong(uint32_t seed, float pitch, float vol, float pan);
// ---------------------------------------------------------------- Mouthful (the Deep Arcade's eat-and-grow arena; doc pp. 19-20)
// Set every frame by the Mouthful scene (mouthful_game.cpp MouthfulAudioFrame); main.cpp turns it off elsewhere.
struct MfAudio {
    bool on = false;
    int band = 0; float depth = 0;   // where the listening mouth is (0 shallows .. 4 trench) and how deep
    int tier = 1;                    // the music quickens with it
    float dusk = 0;                  // 0..1 the night layer
    float highTide = 0;              // seconds left in the final minute (0: not yet)
    float apex = 0, apexPan = 0;     // 0..1 an apex shark within 40 m, and which side
    float orcas = 0;                 // 0..1 the orca pod near
    float boat = 0;                  // 0..1 a boat overhead
    bool crown = false;              // someone wears the crown (a slow drum)
    bool blobfish = false;           // a blobfish on screen (its four notes)
    bool dead = false;
    int over = 0;                    // 1 you won, 2 the round ended otherwise
};
enum MfCue { MFC_SNAP, MFC_CRUNCH, MFC_GULP, MFC_CHOMP, MFC_DASH, MFC_INK, MFC_FRENZY, MFC_CLAW, MFC_SLAM, MFC_INTAKE, MFC_POP,
             MFC_HOOK, MFC_NET, MFC_LEVIATHAN, MFC_FANFARE, MFC_CRASH, MFC_RESPAWN, MFC_FORK, MFC_SWIM, MFC_COUNT };
void AudioMouthful(const MfAudio& a);
void MouthfulCue(int kind, float vol, float pan, float pitch = 1);   // (the Long Flight: a colony's song)   // pitch: the species' voice for calls, a bigger bird lower
