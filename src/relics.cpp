// ============================================================================
//  DEPTH - relics: registry, equip rules, synergies, on-hit effects and the SVG sprite generator.
//
//  Every relic is a small tool with a job. Weapons ("hard weapons": at most one per hero) change how a hero
//  fights; engineering and support relics change what the party can survive; occult relics cost nerves for
//  power. Numbers are deliberately modest and each relic leans toward a kind of hero: armour-breakers for
//  the Mechanic and Whaler, healing tools for the Nurse, stress relief for the Captain, and so on.
// ============================================================================
#include "relics.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {
// ---------------------------------------------------------------- the icons, as SVG (64 x 64)
// Muted, weathered metals under a thick ink outline: steel, tarnished brass, rust, worn leather, bone.
// Every icon has a solid black shadow block on its lower right and scratches or rust across it.
#define INK "#0b0f14"
const char* SVG_WRENCH = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="14,52 20,58 46,32 40,26" fill="#5c6870" stroke="#0b0f14" stroke-width="3"/>
<polygon points="20,58 46,32 43,29 22,52" fill="#0b0f14"/>
<circle cx="46" cy="18" r="12" fill="#5c6870" stroke="#0b0f14" stroke-width="3"/>
<polygon points="45,18 52,4 62,12 52,24" fill="#0b0f14"/>
<polygon points="40,26 54,24 56,20 48,28" fill="#0b0f14"/>
<line x1="24" y1="44" x2="40" y2="29" stroke="#8c979d" stroke-width="1.5"/>
<line x1="20" y1="10" x2="30" y2="10" stroke="#0b0f14" stroke-width="1"/>
<circle cx="17" cy="54" r="2.4" fill="#7a4a2e"/><circle cx="35" cy="34" r="1.8" fill="#7a4a2e"/>
<line x1="38" y1="12" x2="42" y2="8" stroke="#2c353c" stroke-width="1.2"/>
</svg>)SVG";

const char* SVG_PIPE_WRENCH = R"SVG(<svg viewBox="0 0 64 64">
<rect x="28" y="24" width="9" height="36" fill="#5c6870" stroke="#0b0f14" stroke-width="3" transform="rotate(35 32 32)"/>
<rect x="34" y="26" width="3" height="34" fill="#0b0f14" transform="rotate(35 32 32)"/>
<polygon points="20,6 44,6 44,28 34,28 34,19 27,19 27,28 20,28" fill="#6a757c" stroke="#0b0f14" stroke-width="3" transform="rotate(35 32 32)"/>
<polygon points="34,6 44,6 44,28 40,28 40,10" fill="#0b0f14" transform="rotate(35 32 32)"/>
<polyline points="27,19 29,15 31,19 33,15" fill="none" stroke="#0b0f14" stroke-width="1.6" transform="rotate(35 32 32)"/>
<circle cx="32" cy="34" r="5" fill="#a2843a" stroke="#0b0f14" stroke-width="2.4" transform="rotate(35 32 32)"/>
<line x1="31" y1="42" x2="31" y2="56" stroke="#8c979d" stroke-width="1.4" transform="rotate(35 32 32)"/>
<circle cx="32" cy="52" r="2.2" fill="#7a4a2e" transform="rotate(35 32 32)"/>
<circle cx="24" cy="12" r="1.8" fill="#7a4a2e" transform="rotate(35 32 32)"/>
</svg>)SVG";

const char* SVG_MONKEY = R"SVG(<svg viewBox="0 0 64 64">
<rect x="26" y="26" width="9" height="34" fill="#5c6870" stroke="#0b0f14" stroke-width="3" transform="rotate(-38 32 32)"/>
<polygon points="20,4 44,4 44,24 34,24 34,14 30,14 30,24 20,24" fill="#6a757c" stroke="#0b0f14" stroke-width="3" transform="rotate(-38 32 32)"/>
<rect x="30" y="16" width="14" height="6" fill="#a2843a" stroke="#0b0f14" stroke-width="2" transform="rotate(-38 32 32)"/>
<line x1="31" y1="18" x2="43" y2="18" stroke="#0b0f14" stroke-width="1.2" transform="rotate(-38 32 32)"/>
<circle cx="31" cy="30" r="3.4" fill="#a2843a" stroke="#0b0f14" stroke-width="2" transform="rotate(-38 32 32)"/>
<polygon points="34,26 35,60 31,58 32,26" fill="#0b0f14" transform="rotate(-38 32 32)"/>
<circle cx="30" cy="54" r="2" fill="#7a4a2e" transform="rotate(-38 32 32)"/>
</svg>)SVG";

const char* SVG_RIVET_GUN = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="6,22 44,22 44,34 6,34" fill="#5c6870" stroke="#0b0f14" stroke-width="3"/>
<polygon points="6,30 44,30 44,34 6,34" fill="#0b0f14"/>
<rect x="44" y="24" width="16" height="8" fill="#2c353c" stroke="#0b0f14" stroke-width="2.5"/>
<polygon points="14,34 26,34 30,58 18,58" fill="#4b3626" stroke="#0b0f14" stroke-width="3"/>
<polygon points="24,34 26,34 30,58 27,58" fill="#0b0f14"/>
<rect x="10" y="14" width="20" height="8" fill="#a2843a" stroke="#0b0f14" stroke-width="2.5"/>
<circle cx="14" cy="28" r="1.6" fill="#8c979d"/><circle cx="22" cy="28" r="1.6" fill="#8c979d"/><circle cx="36" cy="28" r="1.6" fill="#8c979d"/>
<polygon points="34,34 40,34 40,42 36,42" fill="#0b0f14"/>
<circle cx="52" cy="28" r="1.6" fill="#7a4a2e"/>
</svg>)SVG";

const char* SVG_SWORD = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="32,3 38,10 36,42 28,42 26,10" fill="#8c979d" stroke="#0b0f14" stroke-width="3" transform="rotate(35 32 32)"/>
<polygon points="32,3 38,10 36,42 32,42" fill="#0b0f14" transform="rotate(35 32 32)"/>
<line x1="30" y1="12" x2="30" y2="38" stroke="#0b0f14" stroke-width="1.2" transform="rotate(35 32 32)"/>
<rect x="19" y="42" width="26" height="6" fill="#a2843a" stroke="#0b0f14" stroke-width="2.5" transform="rotate(35 32 32)"/>
<rect x="29" y="48" width="6" height="12" fill="#4b3626" stroke="#0b0f14" stroke-width="2.5" transform="rotate(35 32 32)"/>
<circle cx="32" cy="62" r="3" fill="#a2843a" stroke="#0b0f14" stroke-width="2" transform="rotate(35 32 32)"/>
<circle cx="33" cy="30" r="1.4" fill="#7a4a2e" transform="rotate(35 32 32)"/>
</svg>)SVG";

const char* SVG_POWER_SWORD = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="32,2 39,10 37,42 27,42 25,10" fill="#4f95a5" stroke="#0b0f14" stroke-width="3" transform="rotate(35 32 32)"/>
<polygon points="32,2 39,10 37,42 32,42" fill="#0b0f14" transform="rotate(35 32 32)"/>
<line x1="30" y1="10" x2="30" y2="40" stroke="#b8d6d8" stroke-width="2" transform="rotate(35 32 32)"/>
<polyline points="27,16 33,22 27,28 33,34" fill="none" stroke="#0b0f14" stroke-width="1.4" transform="rotate(35 32 32)"/>
<rect x="18" y="42" width="28" height="7" fill="#6b5426" stroke="#0b0f14" stroke-width="2.5" transform="rotate(35 32 32)"/>
<circle cx="32" cy="45" r="3" fill="#4f95a5" stroke="#0b0f14" stroke-width="1.5" transform="rotate(35 32 32)"/>
<rect x="29" y="49" width="6" height="12" fill="#2c353c" stroke="#0b0f14" stroke-width="2.5" transform="rotate(35 32 32)"/>
</svg>)SVG";

const char* SVG_CLEAVER = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="8,14 46,10 50,36 10,40" fill="#8c979d" stroke="#0b0f14" stroke-width="3" transform="rotate(20 32 32)"/>
<polygon points="10,34 50,32 50,36 10,40" fill="#0b0f14" transform="rotate(20 32 32)"/>
<circle cx="17" cy="20" r="3" fill="#0b0f14" transform="rotate(20 32 32)"/>
<rect x="46" y="18" width="16" height="9" fill="#4b3626" stroke="#0b0f14" stroke-width="3" transform="rotate(20 32 32)"/>
<polygon points="12,16 44,13 44,16 12,19" fill="#c8ccc4" transform="rotate(20 32 32)"/>
<line x1="24" y1="26" x2="34" y2="25" stroke="#7d2c2c" stroke-width="2" transform="rotate(20 32 32)"/>
<circle cx="52" cy="22" r="1.6" fill="#a2843a" transform="rotate(20 32 32)"/>
</svg>)SVG";

const char* SVG_PICKAXE = R"SVG(<svg viewBox="0 0 64 64">
<rect x="29" y="12" width="7" height="50" fill="#6b4e36" stroke="#0b0f14" stroke-width="3" transform="rotate(30 32 32)"/>
<rect x="33" y="14" width="3" height="46" fill="#0b0f14" transform="rotate(30 32 32)"/>
<path d="M4,20 C16,4 48,4 60,20 L52,24 C44,14 20,14 12,24 Z" fill="#5c6870" stroke="#0b0f14" stroke-width="3" transform="rotate(30 32 32)"/>
<path d="M32,8 C44,8 54,14 60,20 L52,24 C48,20 40,16 32,15 Z" fill="#0b0f14" transform="rotate(30 32 32)"/>
<rect x="27" y="14" width="11" height="9" fill="#2c353c" stroke="#0b0f14" stroke-width="2.5" transform="rotate(30 32 32)"/>
<circle cx="8" cy="20" r="1.8" fill="#7a4a2e" transform="rotate(30 32 32)"/>
</svg>)SVG";

const char* SVG_FLINTLOCK = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="4,22 40,20 40,29 4,30" fill="#5c6870" stroke="#0b0f14" stroke-width="3"/>
<polygon points="4,27 40,26 40,29 4,30" fill="#0b0f14"/>
<polygon points="1,19 8,19 8,33 1,33" fill="#2c353c" stroke="#0b0f14" stroke-width="2"/>
<path d="M38,22 L56,26 C60,40 54,54 44,58 L36,52 C42,46 44,38 40,30 Z" fill="#6b4e36" stroke="#0b0f14" stroke-width="3"/>
<path d="M46,32 C52,40 50,50 44,58 L48,56 C56,50 58,38 52,28 Z" fill="#0b0f14"/>
<polygon points="34,20 40,10 44,12 40,22" fill="#8c979d" stroke="#0b0f14" stroke-width="2"/>
<rect x="30" y="24" width="8" height="6" fill="#a2843a" stroke="#0b0f14" stroke-width="2"/>
<path d="M30,30 C30,38 34,40 38,38" fill="none" stroke="#0b0f14" stroke-width="2"/>
</svg>)SVG";

const char* SVG_SIX_SHOOTER = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="14,20 56,18 56,26 14,28" fill="#5c6870" stroke="#0b0f14" stroke-width="3"/>
<polygon points="14,25 56,23 56,26 14,28" fill="#0b0f14"/>
<circle cx="24" cy="30" r="10" fill="#6a757c" stroke="#0b0f14" stroke-width="3"/>
<circle cx="20" cy="27" r="2" fill="#0b0f14"/><circle cx="28" cy="27" r="2" fill="#0b0f14"/><circle cx="20" cy="34" r="2" fill="#0b0f14"/><circle cx="28" cy="34" r="2" fill="#0b0f14"/><circle cx="24" cy="30" r="2" fill="#a2843a"/>
<path d="M12,34 L22,40 C22,50 16,58 8,60 L4,50 C8,46 10,40 10,36 Z" fill="#6b4e36" stroke="#0b0f14" stroke-width="3"/>
<path d="M14,44 C16,52 12,56 8,60 L4,50 Z" fill="#0b0f14"/>
<polygon points="10,16 14,10 18,12 14,20" fill="#8c979d" stroke="#0b0f14" stroke-width="2"/>
</svg>)SVG";

const char* SVG_STAFF = R"SVG(<svg viewBox="0 0 64 64">
<path d="M26,60 C30,44 36,30 34,18" fill="none" stroke="#0b0f14" stroke-width="7"/>
<path d="M26,60 C30,44 36,30 34,18" fill="none" stroke="#6b4e36" stroke-width="3.6"/>
<path d="M34,20 C20,16 18,4 30,4 C22,6 26,12 36,12 C44,12 46,2 38,2" fill="none" stroke="#0b0f14" stroke-width="6"/>
<path d="M34,20 C20,16 18,4 30,4" fill="none" stroke="#4e5a3a" stroke-width="2.4"/>
<circle cx="40" cy="14" r="8" fill="#7d2c2c" stroke="#0b0f14" stroke-width="3"/>
<circle cx="40" cy="14" r="3" fill="#c9a24d"/>
<path d="M32,24 C26,26 24,30 28,32" fill="none" stroke="#0b0f14" stroke-width="2.4"/>
<polygon points="36,38 40,36 38,44" fill="#b3aa8c" stroke="#0b0f14" stroke-width="1.6"/>
</svg>)SVG";

const char* SVG_TESLA_GUN = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="6,26 42,24 42,36 6,38" fill="#5c6870" stroke="#0b0f14" stroke-width="3"/>
<polygon points="6,34 42,32 42,36 6,38" fill="#0b0f14"/>
<circle cx="16" cy="32" r="8" fill="#2c353c" stroke="#0b0f14" stroke-width="3"/>
<circle cx="16" cy="32" r="3" fill="#4f95a5"/>
<rect x="42" y="27" width="4" height="10" fill="#a2843a" stroke="#0b0f14" stroke-width="2"/>
<rect x="48" y="26" width="4" height="12" fill="#a2843a" stroke="#0b0f14" stroke-width="2"/>
<rect x="54" y="25" width="4" height="14" fill="#a2843a" stroke="#0b0f14" stroke-width="2"/>
<polyline points="58,28 62,20 58,24 63,14" fill="none" stroke="#4f95a5" stroke-width="2"/>
<path d="M14,38 L24,38 C24,48 20,56 12,58 L8,50 C12,46 14,42 14,38 Z" fill="#4b3626" stroke="#0b0f14" stroke-width="3"/>
<polyline points="24,26 28,20 30,24 34,18" fill="none" stroke="#0b0f14" stroke-width="1.6"/>
<circle cx="36" cy="30" r="1.6" fill="#7a4a2e"/>
</svg>)SVG";

const char* SVG_DYNAMITE = R"SVG(<svg viewBox="0 0 64 64">
<rect x="10" y="20" width="12" height="38" fill="#7d2c2c" stroke="#0b0f14" stroke-width="3" transform="rotate(-12 32 40)"/>
<rect x="26" y="18" width="12" height="40" fill="#8a3434" stroke="#0b0f14" stroke-width="3"/>
<rect x="42" y="20" width="12" height="38" fill="#7d2c2c" stroke="#0b0f14" stroke-width="3" transform="rotate(12 32 40)"/>
<rect x="32" y="18" width="6" height="40" fill="#0b0f14"/>
<rect x="24" y="30" width="16" height="7" fill="#b8a67c" stroke="#0b0f14" stroke-width="2"/>
<rect x="24" y="44" width="16" height="4" fill="#4b3626"/>
<path d="M32,18 C32,10 40,8 42,2" fill="none" stroke="#0b0f14" stroke-width="3.4"/>
<path d="M32,18 C32,10 40,8 42,2" fill="none" stroke="#b3aa8c" stroke-width="1.4"/>
<polygon points="42,2 46,-1 45,6 49,5 43,10 44,5" fill="#c9a24d" stroke="#0b0f14" stroke-width="1"/>
</svg>)SVG";

const char* SVG_LANTERN = R"SVG(<svg viewBox="0 0 64 64">
<path d="M22,14 C22,2 42,2 42,14" fill="none" stroke="#0b0f14" stroke-width="4"/>
<path d="M22,14 C22,4 42,4 42,14" fill="none" stroke="#6b5426" stroke-width="1.8"/>
<polygon points="20,14 44,14 42,20 22,20" fill="#6b5426" stroke="#0b0f14" stroke-width="3"/>
<polygon points="22,20 42,20 46,50 18,50" fill="#c9a24d" stroke="#0b0f14" stroke-width="3"/>
<polygon points="36,20 42,20 46,50 38,50" fill="#0b0f14" opacity="0.55"/>
<ellipse cx="32" cy="36" rx="5.5" ry="7" fill="#f0d68a" stroke="#0b0f14" stroke-width="1.6"/>
<circle cx="32" cy="36" r="2.6" fill="#0b0f14"/>
<line x1="27" y1="20" x2="25" y2="50" stroke="#0b0f14" stroke-width="2"/><line x1="37" y1="20" x2="39" y2="50" stroke="#0b0f14" stroke-width="2"/>
<polygon points="16,50 48,50 46,58 18,58" fill="#5c6870" stroke="#0b0f14" stroke-width="3"/>
<line x1="24" y1="26" x2="30" y2="32" stroke="#0b0f14" stroke-width="1.2"/>
<circle cx="20" cy="54" r="1.6" fill="#7a4a2e"/>
</svg>)SVG";

const char* SVG_MEDKIT = R"SVG(<svg viewBox="0 0 64 64">
<path d="M22,18 C22,8 42,8 42,18" fill="none" stroke="#0b0f14" stroke-width="5"/>
<path d="M22,18 C22,10 42,10 42,18" fill="none" stroke="#4b3626" stroke-width="2"/>
<rect x="6" y="18" width="52" height="38" rx="4" fill="#6b4e36" stroke="#0b0f14" stroke-width="3"/>
<polygon points="40,18 58,18 58,56 44,56" fill="#0b0f14" opacity="0.6"/>
<rect x="20" y="26" width="24" height="22" fill="#b8a67c" stroke="#0b0f14" stroke-width="2"/>
<polygon points="29,29 35,29 35,34 40,34 40,40 35,40 35,45 29,45 29,40 24,40 24,34 29,34" fill="#7d2c2c"/>
<rect x="6" y="30" width="52" height="4" fill="#a2843a"/>
<circle cx="12" cy="24" r="1.8" fill="#a2843a"/><circle cx="52" cy="24" r="1.8" fill="#a2843a"/>
<line x1="8" y1="46" x2="18" y2="52" stroke="#0b0f14" stroke-width="1.2"/>
</svg>)SVG";

const char* SVG_SYRINGE = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="10,44 14,40 46,12 52,18 20,50 14,52" fill="#4e7a78" stroke="#0b0f14" stroke-width="3" opacity="0.95" transform="rotate(0 32 32)"/>
<polygon points="16,46 44,14 47,17 20,49" fill="#c9a24d"/>
<polygon points="20,50 52,18 50,20 22,52" fill="#0b0f14"/>
<polygon points="46,12 52,18 58,12 52,6" fill="#5c6870" stroke="#0b0f14" stroke-width="2.5"/>
<line x1="54" y1="4" x2="62" y2="12" stroke="#0b0f14" stroke-width="3.4"/>
<line x1="12" y1="52" x2="2" y2="62" stroke="#0b0f14" stroke-width="3.4"/><line x1="12" y1="52" x2="2" y2="62" stroke="#8c979d" stroke-width="1.2"/>
<line x1="26" y1="38" x2="30" y2="42" stroke="#0b0f14" stroke-width="1.2"/><line x1="32" y1="32" x2="36" y2="36" stroke="#0b0f14" stroke-width="1.2"/>
<circle cx="18" cy="49" r="1.6" fill="#7a4a2e"/>
</svg>)SVG";

const char* SVG_PLIERS = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="10,58 16,60 34,34 28,30" fill="#7d2c2c" stroke="#0b0f14" stroke-width="3"/>
<polygon points="54,58 48,60 30,34 36,30" fill="#7d2c2c" stroke="#0b0f14" stroke-width="3"/>
<polygon points="16,60 34,34 30,32 12,58" fill="#0b0f14"/>
<path d="M28,30 C24,22 26,10 30,4 L34,8 C34,16 34,24 32,32 Z" fill="#5c6870" stroke="#0b0f14" stroke-width="3"/>
<path d="M36,30 C40,22 38,10 34,4 L30,8 C30,16 30,24 32,32 Z" fill="#6a757c" stroke="#0b0f14" stroke-width="3"/>
<circle cx="32" cy="32" r="4" fill="#a2843a" stroke="#0b0f14" stroke-width="2.4"/>
<line x1="30" y1="10" x2="34" y2="10" stroke="#0b0f14" stroke-width="1.4"/><line x1="30" y1="14" x2="34" y2="14" stroke="#0b0f14" stroke-width="1.4"/>
<circle cx="14" cy="56" r="1.6" fill="#7a4a2e"/>
</svg>)SVG";

const char* SVG_CRANK = R"SVG(<svg viewBox="0 0 64 64">
<circle cx="32" cy="36" r="18" fill="#5c6870" stroke="#0b0f14" stroke-width="3"/>
<circle cx="32" cy="36" r="10" fill="#2c353c" stroke="#0b0f14" stroke-width="2.4"/>
<circle cx="32" cy="36" r="4" fill="#a2843a" stroke="#0b0f14" stroke-width="2"/>
<polygon points="34,8 40,8 40,30 34,30" fill="#a2843a" stroke="#0b0f14" stroke-width="3"/>
<rect x="30" y="4" width="16" height="8" fill="#4b3626" stroke="#0b0f14" stroke-width="2.6" transform="rotate(0 32 8)"/>
<polygon points="34,38 50,50 44,56 26,42" fill="#0b0f14" opacity="0.7"/>
<polyline points="16,32 20,26 24,32 28,26" fill="none" stroke="#4f95a5" stroke-width="2"/>
<circle cx="18" cy="46" r="1.8" fill="#7a4a2e"/>
</svg>)SVG";

const char* SVG_CHEMKIT = R"SVG(<svg viewBox="0 0 64 64">
<polygon points="20,8 32,8 32,22 42,46 42,56 10,56 10,46 20,22" fill="#4e7a78" stroke="#0b0f14" stroke-width="3" opacity="0.96"/>
<polygon points="14,40 38,40 42,46 42,56 10,56 10,46" fill="#4e5a3a"/>
<polygon points="34,30 42,46 42,56 32,56" fill="#0b0f14" opacity="0.6"/>
<rect x="18" y="4" width="16" height="6" fill="#6b4e36" stroke="#0b0f14" stroke-width="2.4"/>
<circle cx="24" cy="36" r="2" fill="#b3aa8c"/><circle cx="30" cy="30" r="1.6" fill="#b3aa8c"/>
<rect x="46" y="22" width="8" height="34" fill="#4e7a78" stroke="#0b0f14" stroke-width="2.6"/>
<rect x="47" y="38" width="6" height="18" fill="#7d2c2c"/>
<rect x="56" y="30" width="7" height="26" fill="#4e7a78" stroke="#0b0f14" stroke-width="2.4"/>
<rect x="57" y="42" width="5" height="14" fill="#c9a24d"/>
<line x1="10" y1="58" x2="62" y2="58" stroke="#0b0f14" stroke-width="3"/>
</svg>)SVG";

const char* SVG_BACKPACK = R"SVG(<svg viewBox="0 0 64 64">
<path d="M18,14 C18,4 46,4 46,14" fill="none" stroke="#0b0f14" stroke-width="5"/>
<path d="M12,20 C12,10 52,10 52,20 L56,56 C56,60 8,60 8,56 Z" fill="#6b4e36" stroke="#0b0f14" stroke-width="3"/>
<path d="M36,12 C50,12 52,20 52,20 L56,56 C56,60 44,60 40,60 Z" fill="#0b0f14" opacity="0.6"/>
<rect x="16" y="34" width="32" height="20" rx="3" fill="#4b3626" stroke="#0b0f14" stroke-width="2.6"/>
<rect x="28" y="38" width="8" height="8" fill="#a2843a" stroke="#0b0f14" stroke-width="2"/>
<line x1="16" y1="26" x2="48" y2="26" stroke="#0b0f14" stroke-width="2.4"/>
<rect x="22" y="22" width="5" height="9" fill="#a2843a" stroke="#0b0f14" stroke-width="1.6"/>
<line x1="12" y1="46" x2="18" y2="42" stroke="#b3aa8c" stroke-width="1.4"/>
<line x1="44" y1="30" x2="50" y2="28" stroke="#0b0f14" stroke-width="1.2"/>
</svg>)SVG";

// ---------------------------------------------------------------- a small SVG rasteriser
// Reads the subset the icons use and draws it with raylib into a render texture. Fills are ear-clipped, strokes are
// round-jointed lines.
struct Poly { std::vector<Vector2> pts; bool closed = true; };

Color ParseColor(const std::string& v, float opacity) {
    if (v.empty() || v == "none") return {0, 0, 0, 0};
    unsigned r = 0, g = 0, b = 0;
    if (v[0] == '#' && v.size() >= 7) { sscanf(v.c_str() + 1, "%2x%2x%2x", &r, &g, &b); }
    else if (v[0] == '#' && v.size() >= 4) { sscanf(v.c_str() + 1, "%1x%1x%1x", &r, &g, &b); r *= 17; g *= 17; b *= 17; }
    else if (v == "white") { r = g = b = 255; }
    return {(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)(std::clamp(opacity, 0.0f, 1.0f) * 255)};
}

std::string AttrOf(const std::string& tag, const char* key) {
    std::string k = std::string(" ") + key + "=\"";
    size_t p = tag.find(k);
    if (p == std::string::npos) return "";
    p += k.size();
    size_t e = tag.find('"', p);
    return e == std::string::npos ? "" : tag.substr(p, e - p);
}

float NumOf(const std::string& tag, const char* key, float def = 0) {
    std::string v = AttrOf(tag, key);
    return v.empty() ? def : (float)atof(v.c_str());
}

std::vector<float> Numbers(const std::string& s) {
    std::vector<float> out;
    const char* p = s.c_str();
    while (*p) {
        while (*p && !(isdigit((unsigned char)*p) || *p == '-' || *p == '.')) p++;
        if (!*p) break;
        char* end;
        out.push_back((float)strtod(p, &end));
        if (end == p) break;
        p = end;
    }
    return out;
}

void Bezier(std::vector<Vector2>& pts, Vector2 a, Vector2 b, Vector2 c, Vector2 d) {
    for (int i = 1; i <= 10; i++) {
        float t = i / 10.0f, u = 1 - t;
        pts.push_back({u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                       u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y});
    }
}

std::vector<Poly> ParsePath(const std::string& d) {
    std::vector<Poly> out;
    Poly cur;
    Vector2 pos{0, 0}, start{0, 0};
    size_t i = 0;
    auto flush = [&](bool closed) { if (cur.pts.size() >= 2) { cur.closed = closed; out.push_back(cur); } cur = Poly{}; };
    while (i < d.size()) {
        char c = d[i];
        if (!isalpha((unsigned char)c)) { i++; continue; }
        size_t j = i + 1;
        while (j < d.size() && !isalpha((unsigned char)d[j])) j++;
        std::vector<float> n = Numbers(d.substr(i + 1, j - i - 1));
        bool rel = islower((unsigned char)c);
        char C = (char)toupper(c);
        if (C == 'M' && n.size() >= 2) { flush(true); pos = {n[0] + (rel ? pos.x : 0), n[1] + (rel ? pos.y : 0)}; start = pos; cur.pts.push_back(pos); for (size_t k = 2; k + 1 < n.size(); k += 2) { pos = {n[k] + (rel ? pos.x : 0), n[k + 1] + (rel ? pos.y : 0)}; cur.pts.push_back(pos); } }
        else if (C == 'L') for (size_t k = 0; k + 1 < n.size(); k += 2) { pos = {n[k] + (rel ? pos.x : 0), n[k + 1] + (rel ? pos.y : 0)}; cur.pts.push_back(pos); }
        else if (C == 'H') for (float v : n) { pos.x = v + (rel ? pos.x : 0); cur.pts.push_back(pos); }
        else if (C == 'V') for (float v : n) { pos.y = v + (rel ? pos.y : 0); cur.pts.push_back(pos); }
        else if (C == 'C') for (size_t k = 0; k + 5 < n.size(); k += 6) {
            Vector2 o = rel ? pos : Vector2{0, 0}, b1{n[k] + o.x, n[k + 1] + o.y}, b2{n[k + 2] + o.x, n[k + 3] + o.y}, e{n[k + 4] + o.x, n[k + 5] + o.y};
            Bezier(cur.pts, pos, b1, b2, e);
            pos = e;
        }
        else if (C == 'Z') { pos = start; flush(true); cur.pts.push_back(pos); }
        i = j;
    }
    flush(false);
    return out;
}

float Cross(Vector2 a, Vector2 b, Vector2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

void FillPoly(const std::vector<Vector2>& p, Color col) {
    int n = (int)p.size();
    if (n < 3 || col.a == 0) return;
    std::vector<int> idx(n);
    for (int i = 0; i < n; i++) idx[i] = i;
    float area = 0;
    for (int i = 0; i < n; i++) area += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
    float sgn = area > 0 ? 1.0f : -1.0f;
    int guard = 0;
    while (idx.size() > 3 && guard++ < 2000) { // ear clipping
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); i++) {
            Vector2 a = p[idx[(i + idx.size() - 1) % idx.size()]], b = p[idx[i]], c = p[idx[(i + 1) % idx.size()]];
            if (Cross(a, b, c) * sgn <= 0) continue;
            bool inside = false;
            for (int k : idx) {
                Vector2 q = p[k];
                if ((q.x == a.x && q.y == a.y) || (q.x == b.x && q.y == b.y) || (q.x == c.x && q.y == c.y)) continue;
                if (Cross(a, b, q) * sgn >= 0 && Cross(b, c, q) * sgn >= 0 && Cross(c, a, q) * sgn >= 0) { inside = true; break; }
            }
            if (inside) continue;
            DrawTriangle(a, b, c, col); DrawTriangle(a, c, b, col);
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) { DrawTriangle(p[idx[0]], p[idx[1]], p[idx[2]], col); DrawTriangle(p[idx[0]], p[idx[2]], p[idx[1]], col); }
}

void StrokePoly(const std::vector<Vector2>& p, bool closed, float w, Color col) {
    if (col.a == 0 || w <= 0 || p.size() < 2) return;
    size_t n = p.size();
    for (size_t i = 0; i + 1 < n; i++) DrawLineEx(p[i], p[i + 1], w, col);
    if (closed && n > 2) DrawLineEx(p[n - 1], p[0], w, col);
    for (auto& v : p) DrawCircleV(v, w / 2, col);
}

Texture2D Render(const std::string& svg, int px, RenderTexture2D& outRt) {
    outRt = LoadRenderTexture(px, px);
    float scale = px / 64.0f;
    BeginTextureMode(outRt);
    ClearBackground(BLANK);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    size_t pos = 0;
    while ((pos = svg.find('<', pos)) != std::string::npos) {
        size_t end = svg.find('>', pos);
        if (end == std::string::npos) break;
        std::string tag = svg.substr(pos, end - pos);
        pos = end + 1;
        if (tag.size() < 2 || tag[1] == '/' || tag[1] == '?') continue;
        size_t ne = tag.find_first_of(" />", 1);
        std::string name = tag.substr(1, ne == std::string::npos ? std::string::npos : ne - 1);
        tag += " ";
        float op = NumOf(tag, "opacity", 1.0f);
        Color fill = ParseColor(AttrOf(tag, "fill"), op), stroke = ParseColor(AttrOf(tag, "stroke"), op);
        if (AttrOf(tag, "fill").empty() && name != "line" && name != "polyline") fill = ParseColor("#000000", op);
        float sw = NumOf(tag, "stroke-width", 1.0f) * scale;
        std::vector<Poly> shapes;
        if (name == "rect") {
            float x = NumOf(tag, "x"), y = NumOf(tag, "y"), w = NumOf(tag, "width"), h = NumOf(tag, "height");
            shapes.push_back({{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, true});
        } else if (name == "circle" || name == "ellipse") {
            float cx = NumOf(tag, "cx"), cy = NumOf(tag, "cy"), rx = name == "circle" ? NumOf(tag, "r") : NumOf(tag, "rx"), ry = name == "circle" ? rx : NumOf(tag, "ry");
            Poly p;
            for (int i = 0; i < 28; i++) p.pts.push_back({cx + cosf(i * 2 * PI / 28) * rx, cy + sinf(i * 2 * PI / 28) * ry});
            shapes.push_back(p);
        } else if (name == "line") {
            shapes.push_back({{{NumOf(tag, "x1"), NumOf(tag, "y1")}, {NumOf(tag, "x2"), NumOf(tag, "y2")}}, false});
            fill = {0, 0, 0, 0};
        } else if (name == "polygon" || name == "polyline") {
            std::vector<float> n = Numbers(AttrOf(tag, "points"));
            Poly p;
            for (size_t k = 0; k + 1 < n.size(); k += 2) p.pts.push_back({n[k], n[k + 1]});
            p.closed = name == "polygon";
            if (name == "polyline" && AttrOf(tag, "fill").empty()) fill = {0, 0, 0, 0};
            shapes.push_back(p);
        } else if (name == "path") {
            shapes = ParsePath(AttrOf(tag, "d"));
        } else continue;
        // transform="rotate(a cx cy)"
        std::string tr = AttrOf(tag, "transform");
        float ang = 0, rcx = 32, rcy = 32;
        if (tr.rfind("rotate", 0) == 0) { std::vector<float> n = Numbers(tr); if (!n.empty()) ang = n[0] * DEG2RAD; if (n.size() >= 3) { rcx = n[1]; rcy = n[2]; } }
        for (auto& s : shapes) {
            for (auto& v : s.pts) {
                if (ang != 0) { float dx = v.x - rcx, dy = v.y - rcy; v = {rcx + dx * cosf(ang) - dy * sinf(ang), rcy + dx * sinf(ang) + dy * cosf(ang)}; }
                v = {v.x * scale, v.y * scale};
            }
            if (s.closed) FillPoly(s.pts, fill);
            StrokePoly(s.pts, s.closed, sw, stroke);
        }
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    EndTextureMode();
    SetTextureFilter(outRt.texture, TEXTURE_FILTER_BILINEAR);
    return outRt.texture;
}

// ---- the carried consumables, in the same weathered hand: a corroded brass cell, a stained linen roll, a rusted key
const char* SVG_BATTERY = R"SVG(<svg viewBox="0 0 64 64">
<rect x="22" y="6" width="20" height="8" fill="#5c6870" stroke="#0b0f14" stroke-width="3"/>
<rect x="16" y="12" width="32" height="46" rx="3" fill="#6b5426" stroke="#0b0f14" stroke-width="3"/>
<polygon points="34,12 48,12 48,58 38,58" fill="#0b0f14" opacity="0.65"/>
<rect x="20" y="24" width="20" height="24" fill="#b8a67c" stroke="#0b0f14" stroke-width="2"/>
<polygon points="28,28 32,28 32,33 37,33 37,37 32,37 32,42 28,42 28,37 23,37 23,33 28,33" fill="#0b0f14"/>
<circle cx="22" cy="16" r="2.2" fill="#4e5a3a"/><circle cx="42" cy="50" r="2" fill="#4e5a3a"/><circle cx="19" cy="52" r="1.8" fill="#7a4a2e"/>
<line x1="20" y1="20" x2="30" y2="20" stroke="#a2843a" stroke-width="1.6"/>
</svg>)SVG";

const char* SVG_BANDAGE = R"SVG(<svg viewBox="0 0 64 64">
<circle cx="30" cy="32" r="21" fill="#b8a67c" stroke="#0b0f14" stroke-width="3"/>
<path d="M34,14 C50,18 56,36 44,50 C48,40 46,26 34,14 Z" fill="#0b0f14" opacity="0.7"/>
<circle cx="30" cy="32" r="11" fill="#8a7a58" stroke="#0b0f14" stroke-width="2.4"/>
<polygon points="27,24 33,24 33,29 38,29 38,35 33,35 33,40 27,40 27,35 22,35 22,29 27,29" fill="#7d2c2c"/>
<polygon points="48,40 62,46 60,54 46,50" fill="#b8a67c" stroke="#0b0f14" stroke-width="2.6"/>
<circle cx="18" cy="22" r="3.6" fill="#7a4a2e" opacity="0.8"/><circle cx="40" cy="44" r="2.4" fill="#7a4a2e" opacity="0.8"/>
<line x1="14" y1="34" x2="20" y2="44" stroke="#0b0f14" stroke-width="1.2"/><line x1="24" y1="14" x2="34" y2="16" stroke="#0b0f14" stroke-width="1.2"/>
</svg>)SVG";

const char* SVG_KEY = R"SVG(<svg viewBox="0 0 64 64">
<circle cx="20" cy="20" r="14" fill="none" stroke="#0b0f14" stroke-width="10"/>
<circle cx="20" cy="20" r="14" fill="none" stroke="#a2843a" stroke-width="4.6"/>
<polygon points="27,28 34,22 58,46 52,52" fill="#a2843a" stroke="#0b0f14" stroke-width="3"/>
<polygon points="34,22 58,46 55,48 30,26" fill="#0b0f14"/>
<polygon points="46,44 52,38 58,44 52,50" fill="#a2843a" stroke="#0b0f14" stroke-width="2.6"/>
<polygon points="52,52 58,46 62,50 56,58" fill="#a2843a" stroke="#0b0f14" stroke-width="2.6"/>
<circle cx="14" cy="14" r="2" fill="#7a4a2e"/><circle cx="40" cy="36" r="1.8" fill="#7a4a2e"/>
<line x1="12" y1="26" x2="18" y2="30" stroke="#0b0f14" stroke-width="1.4"/>
</svg>)SVG";

std::vector<RenderTexture2D> gItemSprites;std::vector<RenderTexture2D> gSprites;
bool gReady = false;

// ---------------------------------------------------------------- the registry
RelicDef Make(const char* name, RelicCategory cat, bool hard, const char* dungeon, const char* platformer, int price, const char* svg) {
    RelicDef r;
    r.name = name; r.category = cat; r.isHardWeapon = hard; r.dungeonText = dungeon; r.platformerText = platformer; r.price = price; r.svgSpriteData = svg;
    r.desc = dungeon;
    return r;
}
bool Roll(int pct) { return GetRandomValue(1, 100) <= pct; }
}  // namespace

const char* RelicSpriteGenerator::PIPE_WRENCH_SVG = SVG_PIPE_WRENCH;
const char* RelicSpriteGenerator::TESLA_GUN_SVG = SVG_TESLA_GUN;
const char* RelicSpriteGenerator::AWAKENED_LANTERN_SVG = SVG_LANTERN;

const std::vector<RelicDef>& RelicRegistry::All() {
    static const std::vector<RelicDef> list = [] {
        std::vector<RelicDef> v;
        using C = RelicCategory;
        // ids 0-11 are the original twelve (they are saved by id); 12-19 are new.
        RelicDef r;
        r = Make("Wrench", C::ENGINEERING, false, "+6 Protection, +5% critical chance", "Repairs machinery 10% faster (soon)", 170, SVG_WRENCH);
        r.prot = 6; r.fx.critPct = 5; v.push_back(r);
        r = Make("Pipe Wrench", C::ENGINEERING, false, "+1 Damage, -1 Speed. 25% to stun armoured foes it hits", "Smashes minor wall obstacles in one hit (soon)", 165, SVG_PIPE_WRENCH);
        r.dmg = 1; r.speed = -1;
        r.combatEffect = [](CombatState& c) { if (c.target && c.target->prot >= 10 && Roll(25)) c.stunTarget = true; };
        v.push_back(r);
        r = Make("Monkey Wrench", C::ENGINEERING, false, "+4 Accuracy, +3 Dodge. +30% damage to constructs (bosses of stone, brass and bronze)", "Works switches without using tools (soon)", 170, SVG_MONKEY);
        r.acc = 4; r.dodge = 3; r.fx.vsConstruct = 30; v.push_back(r);
        r = Make("Rivet Gun", C::OFFENSE, true, "HARD WEAPON. +2 Damage, -4 Accuracy. Ignores 20% of armour", "Fires pins that make wall pegs (soon)", 210, SVG_RIVET_GUN);
        r.dmg = 2; r.acc = -4; r.fx.armorPen = 20; v.push_back(r);
        r = Make("Sword", C::OFFENSE, true, "HARD WEAPON. +1 Damage, +3 Accuracy, +1 Speed", "A basic melee slash (soon)", 195, SVG_SWORD);
        r.dmg = 1; r.acc = 3; r.speed = 1; v.push_back(r);
        r = Make("Butcher Knife", C::OFFENSE, true, "HARD WEAPON. +1 Damage, +2 Speed. 35% to make the wound bleed", "Rapid low-range attack (soon)", 200, SVG_CLEAVER);
        r.dmg = 1; r.speed = 2;
        r.combatEffect = [](CombatState& c) { if (Roll(35)) c.bleedTarget = std::max(c.bleedTarget, 2); };
        v.push_back(r);
        r = Make("Flintlock", C::OFFENSE, true, "HARD WEAPON. +1 Damage, +8 Accuracy, +6% critical chance", "A ranged shot (soon)", 200, SVG_FLINTLOCK);
        r.dmg = 1; r.acc = 8; r.fx.critPct = 6; v.push_back(r);
        r = Make("Six-Shooter", C::OFFENSE, true, "HARD WEAPON. +1 Damage, +1 Speed. 20% for a second shot at half damage", "A six-bullet burst before reloading (soon)", 220, SVG_SIX_SHOOTER);
        r.dmg = 1; r.speed = 1;
        r.combatEffect = [](CombatState& c) {
            if (c.target && Roll(20)) { int extra = std::max(1, c.damage / 2); c.target->hp -= extra; }
        };
        v.push_back(r);
        r = Make("MedKit", C::SUPPORT, false, "+5 Max HP. Heals the hero casts restore 2 more", "Restores a heart (soon)", 170, SVG_MEDKIT);
        r.hp = 5; r.fx.healBonus = 2; v.push_back(r);
        r = Make("Syringe", C::SUPPORT, false, "+2 Speed. Heals restore 1 more", "+5% run speed", 195, SVG_SYRINGE);
        r.speed = 2; r.fx.healBonus = 1; r.fx.speedPct = 5; v.push_back(r);
        r = Make("Pliers", C::SUPPORT, false, "Resist 10% of stress. Landing a blow pulls a bleed or poison out of the hero 30% of the time", "Disarms wire traps (soon)", 180, SVG_PLIERS);
        r.stressResist = 10;
        r.combatEffect = [](CombatState& c) {
            if (c.hero && Roll(30)) { c.hero->st.bleedTurns = 0; c.hero->st.poisonTurns = 0; }
        };
        v.push_back(r);
        r = Make("Backpack", C::UTILITY, false, "+2 inventory slots, +3 Max HP, resist 5% of stress", "+50% coin pickup radius", 205, SVG_BACKPACK);
        r.hp = 3; r.stressResist = 5; r.fx.extraSlots = 2; r.fx.pickupPct = 50; v.push_back(r);
        // ---- new
        r = Make("Power Sword", C::OFFENSE, true, "HARD WEAPON. +3 Damage, +1 Speed, +4% critical chance, -5 Dodge (a hungry blade)", "A charged slash that cuts hazard barriers (soon)", 320, SVG_POWER_SWORD);
        r.dmg = 3; r.speed = 1; r.dodge = -5; r.fx.critPct = 4; v.push_back(r);
        r = Make("Pickaxe", C::OFFENSE, true, "HARD WEAPON. +2 Damage, -2 Accuracy. +30% damage to armoured foes (shell, rock, plate)", "Breaks minerals and secret walls (soon)", 220, SVG_PICKAXE);
        r.dmg = 2; r.acc = -2; r.fx.vsArmored = 30; v.push_back(r);
        r = Make("Awakened Lantern", C::OCCULT, false, "+3 Accuracy. The hero gains 15% less stress, and a critical hit steadies the whole party (-2)", "Lights dark caverns: +30% lamp radius", 240, SVG_LANTERN);
        r.acc = 3; r.fx.stressGainPct = 15; r.fx.lampPct = 30;
        r.combatEffect = [](CombatState& c) { if (c.crit) c.stressRelief += 2; };
        v.push_back(r);
        r = Make("Tesla Gun", C::OFFENSE, true, "HARD WEAPON. +1 Damage. 30% of hits arc to the next enemy for half damage (always on a critical hit with a Crank)", "Charges conductive switches (soon)", 280, SVG_TESLA_GUN);
        r.category = C::OFFENSE; r.dmg = 1;
        r.combatEffect = [](CombatState& c) { if (c.chain || Roll(30)) c.arcDamage = std::max(c.arcDamage, std::max(1, c.damage / 2)); };
        v.push_back(r);
        r = Make("Eldritch Staff", C::OCCULT, true, "HARD WEAPON. +1 Damage. Ignores 30% of armour, but every blow costs the hero 3 nerves", "A slow homing orb (soon)", 260, SVG_STAFF);
        r.dmg = 1; r.fx.armorPen = 30;
        r.combatEffect = [](CombatState& c) { c.selfStress += 3; };
        v.push_back(r);
        r = Make("Dynamite", C::OFFENSE, false, "12% of blows set off a charge: 6 damage to both front enemies. 15% of the time it goes off in the hero's hands (3 damage)", "Blasts boulder blockades (soon)", 180, SVG_DYNAMITE);
        r.combatEffect = [](CombatState& c) {
            if (!Roll(12)) return;
            c.splashFront += 6;
            if (Roll(15)) c.recoilDamage += 3;
        };
        v.push_back(r);
        r = Make("Tesla Crank", C::ENGINEERING, false, "+1 Speed, +2 Accuracy. With a Tesla Gun: +1 damage and a critical hit always arcs", "Powers dead generators (soon)", 190, SVG_CRANK);
        r.speed = 1; r.acc = 2; v.push_back(r);
        r = Make("Chemistry Kit", C::SUPPORT, false, "Heals restore 1 more and splash 1 onto the rest of the party. Resist 5% of stress", "Turns plants into consumables (soon)", 230, SVG_CHEMKIT);
        r.stressResist = 5; r.fx.healBonus = 1; r.fx.healParty = 1; v.push_back(r);
        return v;
    }();
    return list;
}

const char* RelicCategoryName(RelicCategory c) {
    switch (c) {
        case RelicCategory::OFFENSE: return "offense";
        case RelicCategory::ENGINEERING: return "engineering";
        case RelicCategory::SUPPORT: return "support";
        case RelicCategory::OCCULT: return "occult";
        default: return "utility";
    }
}

int RelicRegistry::Count() { return (int)All().size(); }

// ---------------------------------------------------------------- equip rules
bool CanEquipRelic(const Hero& h, int relicId, std::string* why) {
    const auto& all = RelicRegistry::All();
    if (relicId < 0 || relicId >= (int)all.size()) return false;
    int used = 0;
    bool hard = false;
    for (int r : h.relics) {
        if (r < 0) continue;
        used++;
        if (r == relicId) { if (why) *why = "already carrying one"; return false; }
        hard |= all[r].isHardWeapon;
    }
    if (used >= 2) { if (why) *why = "no free relic slot"; return false; }
    if (all[relicId].isHardWeapon && hard) { if (why) *why = "already carrying a hard weapon"; return false; }
    return true;
}
bool CanEquipRelicInSlot(const Hero& h, int relicId) { return CanEquipRelic(h, relicId, nullptr); }

// ---------------------------------------------------------------- synergies
RelicSynergy CheckRelicSynergies(const RelicDef& a, const RelicDef& b) {
    RelicSynergy s;
    auto pair = [&](const char* x, const char* y) { return (a.name == x && b.name == y) || (a.name == y && b.name == x); };
    if (pair("Rivet Gun", "Pliers")) {
        s = {true, "Armour Stripper", "+15% armour ignored", {}, 0, 15};
        s.fx.armorPen = 15;
    } else if (pair("Tesla Gun", "Tesla Crank")) {
        s = {true, "Overcharged Coil", "+1 damage, critical hits always arc", {}, 1, 0};
        s.fx.chainOnCrit = true; s.fx.dmg = 1;
    } else if (pair("Syringe", "Chemistry Kit")) {
        s = {true, "Elixir Line", "single-target heals also heal the party for 2 and restore 1 more", {}, 0, 0};
        s.fx.healParty = 2; s.fx.healBonus = 1;
    } else if (pair("Eldritch Staff", "Awakened Lantern")) {
        s = {true, "Lit Ritual", "-10% stress gained", {}, 0, 0};
        s.fx.stressGainPct = 10;
    }
    return s;
}

RelicFx RelicBundle(const Hero& h) {
    const auto& all = RelicRegistry::All();
    RelicFx t;
    const RelicDef* eq[2] = {nullptr, nullptr};
    int n = 0;
    for (int r : h.relics) if (r >= 0 && r < (int)all.size() && n < 2) eq[n++] = &all[r];
    auto add = [&](const RelicFx& f) {
        t.critPct += f.critPct; t.armorPen += f.armorPen; t.vsArmored += f.vsArmored; t.vsConstruct += f.vsConstruct;
        t.healBonus += f.healBonus; t.healParty += f.healParty; t.stressGainPct += f.stressGainPct; t.extraSlots += f.extraSlots;
        t.pickupPct += f.pickupPct; t.speedPct += f.speedPct; t.jumpPct += f.jumpPct; t.lampPct += f.lampPct; t.dmg += f.dmg;
        t.chainOnCrit |= f.chainOnCrit;
    };
    for (int i = 0; i < n; i++) add(eq[i]->fx);
    if (n == 2) { RelicSynergy s = CheckRelicSynergies(*eq[0], *eq[1]); if (s.active) add(s.fx); }
    t.armorPen = std::min(t.armorPen, 60);
    t.stressGainPct = std::min(t.stressGainPct, 40);
    return t;
}

void RunCombatRelicEffects(CombatState& cs) {
    if (!cs.hero) return;
    const auto& all = RelicRegistry::All();
    RelicFx b = RelicBundle(*cs.hero);
    if (b.chainOnCrit && cs.crit) cs.chain = true;
    for (int r : cs.hero->relics) if (r >= 0 && r < (int)all.size() && all[r].combatEffect) all[r].combatEffect(cs);
}

int InvCapacity(const Game& g) {
    int extra = 0;
    for (int id : g.party) {
        if (id < 0) continue;
        for (const auto& h : g.roster) if (h.id == id) extra += RelicBundle(h).extraSlots;
    }
    return std::min(INV_SLOTS + extra, 9);
}

// ---------------------------------------------------------------- the sprite generator
void RelicSpriteGenerator::Init() {
    if (gReady) return;
    const auto& all = RelicRegistry::All();
    gSprites.assign(all.size(), RenderTexture2D{});
    for (size_t i = 0; i < all.size(); i++) if (!all[i].svgSpriteData.empty()) Render(all[i].svgSpriteData, 128, gSprites[i]);
    gItemSprites.assign(3, RenderTexture2D{});
    const char* itemSvg[3] = {SVG_BATTERY, SVG_BANDAGE, SVG_KEY};
    for (int i = 0; i < 3; i++) Render(itemSvg[i], 128, gItemSprites[i]);
    gReady = true;
}
void RelicSpriteGenerator::Unload() {
    for (auto& rt : gSprites) if (rt.id) UnloadRenderTexture(rt);
    gSprites.clear();
    gReady = false;
}
bool RelicSpriteGenerator::Has(int id) { return gReady && id >= 0 && id < (int)gSprites.size() && gSprites[id].id != 0; }
bool RelicSpriteGenerator::HasItem(int kind) { return gReady && kind >= 0 && kind < (int)gItemSprites.size() && gItemSprites[kind].id != 0; }
Texture2D RelicSpriteGenerator::ItemSprite(int kind) { return HasItem(kind) ? gItemSprites[kind].texture : Texture2D{}; }
Texture2D RelicSpriteGenerator::Sprite(int id) { return Has(id) ? gSprites[id].texture : Texture2D{}; }
Texture2D RelicSpriteGenerator::RenderSvg(const std::string& svg, int px) {
    static std::vector<RenderTexture2D> kept; // owned here for the life of the program
    kept.emplace_back();
    return Render(svg, px, kept.back());
}
