/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* The DRUM source: a track's one drum, a MODEL and its sound, played by the track's notes; models from FNTSMG-1 (fg/).
 * A drum is not a voice: a note asks for a hit (dm_req), the track renders it in mix_tracks, its tail its own, whatever
 * the ENV or a note off. A voice still takes the note, silent, for the lists, chords and BANK.
 *   SOUND    MODEL: KICK, SNARE, CLAP, MEMBRANE, HAIL (FNTSMG-1's), FM DRUM, BELL, PARTICLE, WEIRD (perc.inc), each with
 *            twenty presets after it, and three main
 *            knobs. DRUM 1, DRUM 2: the rest of its sound, then
 *   KEYS     the keys tune it: C3 as its TONE or COLOR says, at the TUNING's pitches (ON), or every key the same (OFF)
 *   WANDER   each hit's pitch off by up to a semitone, at random
 *   HUMAN    each hit's level down by up to 12 dB or so, at random */
#include "fg/fg_dsp.h"
#include "fg/fg_models.inc"
#include "perc.inc"                             /* (1.4.8: FM DRUM, BELL, PARTICLE, WEIRD) */
enum { DM_KICK, DM_SNARE, DM_CLAP, DM_MEMB, DM_HAIL, DM_FMD, DM_BELL, DM_PART, DM_WEIRD, NDMODEL };   /* (as MODEL scrolls them) */
#define DM_NPR 21u                              /* a model, then its twenty presets */
#define NDENT (NDMODEL * DM_NPR)
#define DM_DEEP 8u
static const char *const DM_NAME[NDMODEL] = {"KICK", "SNARE", "CLAP", "MEMBRANE", "HAIL", "FM DRUM", "BELL", "PARTICLE", "WEIRD"};
/* each entry's name and its eight, as FNTSMG-1 keeps them (its parameters' order; the first of each model: its INICIAL) */
static const char *const DM_ENAME[NDENT] = {
    "KICK", "909", "808", "DRIVEN", "HARD", "SUB", "BOOM", "DEEP", "TECHNO", "HOUSE", "GABBER", "DEMBOW", "TRAP", "RUMBLE", "MINIMAL", "CLICK", "BREAKS", "DISTORTED", "LO-FI", "FAR", "PUNCH",
    "SNARE", "909", "808", "ELECTRO", "TECHNO", "HOUSE", "BREAK", "TRAP", "DEMBOW", "FAT", "PICCOLO", "LO-FI", "INDUSTRIAL", "RIMSHOT", "CROSS", "LONG", "SHORT", "SOFT", "DUB", "NOISE", "SNARE TOM",
    "CLAP", "808", "909", "DRY", "HALL", "SNAP", "FAR", "CROWD", "ONE HAND", "REGGAETON", "GARAGE", "LONG", "DARK", "BRIGHT", "BROKEN", "CHURCH", "TRAP", "SPACE", "PAPER", "DUB", "SOFT",
    "MEMBRANE", "LOW TOM", "TIMBALE", "SKIN", "TAMBORA", "CONGA", "DEEP", "HIGH TOM", "MID TOM", "SURDO", "LEGUERO", "CAJON", "DJEMBE", "BOMBA", "TECHNO TOM", "ELECTRO", "LOOSE SKIN", "CUMBIA", "HIT", "BOOM BAP", "DRIVEN",
    "HAIL", "CLOSED HAT", "OPEN HAT", "SHAKER", "RAIN", "WHISTLE", "TICK", "808 HAT", "909 OPEN", "SHORT", "MARACA", "SAND", "SPARKS", "STEAM", "METALLIC", "SANDPAPER", "FIRE", "GUIRO", "DARK", "LONG", "RESO",
    "FM DRUM", "PEAKS BD", "PEAKS SD", "LOW TOM", "HIGH TOM", "ZAP", "LASER", "METAL KICK", "GROWL", "BLIP", "NOISE TOM", "SNAP", "DUB TOM", "ELECTRO", "RIM", "DISTORTED", "CONGA", "CLAVE", "RUMBLE", "STAB", "DROP",
    "BELL", "STRUCK DRUM", "GAMELAN", "CHURCH", "TINY", "COWBELL", "AGOGO", "GLASS", "METAL PLATE", "DETUNED", "SOFT MALLET", "TABLA", "TIMPANI", "SNARE DRUM", "NOISE DRUM", "BOTTLE", "DRONE BELL", "ICE", "BENT", "WOODBLOCK", "KALIMBA",
    "PARTICLE", "DUST", "RAIN", "CRACKLE", "PING", "DROPS", "SHAKER", "GEIGER", "SWARM", "BUBBLES", "FIRE", "TICKS", "GLITCH", "SAND", "MARBLES", "STATIC", "LOW RUMBLE", "INSECTS", "TAPE DUST", "CLICKS", "CHIMES",
    "WEIRD", "KICK", "SNARE", "HAT", "WOBBLE", "GROWL", "ROBOT", "SIREN", "BOING", "ZAP", "WIND", "VIBRATO TOM", "BUZZ", "CLAP-ISH", "SUB DROP", "TRILL", "DUB WOOD", "SPACE", "DIRT", "PEW", "SCRATCH"
};
static const uint8_t DM_PR[NDENT][8] = {
    {58, 60, 55, 10, 25, 50, 45, 110},
    {62, 55, 40, 15, 0, 85, 70, 117},
    {51, 40, 100, 0, 10, 20, 30, 115},
    {55, 65, 50, 20, 95, 40, 60, 107},
    {66, 90, 30, 70, 20, 100, 100, 109},
    {43, 25, 85, 0, 0, 10, 25, 127},
    {20, 30, 120, 0, 30, 10, 60, 110},
    {8, 35, 95, 0, 15, 10, 25, 113},
    {50, 70, 45, 15, 60, 75, 70, 107},
    {56, 60, 50, 10, 30, 60, 60, 109},
    {60, 95, 40, 90, 127, 110, 110, 97},
    {50, 55, 55, 20, 50, 55, 80, 108},
    {38, 30, 120, 5, 40, 20, 40, 109},
    {45, 45, 110, 30, 80, 30, 50, 108},
    {64, 70, 22, 0, 10, 40, 30, 127},
    {75, 100, 15, 0, 0, 127, 20, 127},
    {58, 65, 35, 25, 45, 90, 70, 108},
    {52, 60, 60, 60, 110, 70, 90, 103},
    {44, 30, 60, 60, 90, 0, 40, 108},
    {40, 30, 70, 0, 5, 0, 10, 127},
    {68, 90, 20, 5, 30, 100, 127, 108},
    {40, 80, 90, 50, 70, 60, 70, 105},
    {50, 80, 90, 50, 70, 60, 80, 103},
    {70, 100, 60, 40, 50, 30, 50, 106},
    {30, 100, 110, 45, 110, 85, 110, 95},
    {40, 70, 110, 45, 100, 70, 90, 103},
    {55, 60, 90, 60, 80, 50, 60, 116},
    {35, 100, 100, 65, 60, 80, 110, 95},
    {60, 80, 80, 35, 85, 70, 90, 101},
    {70, 70, 95, 30, 70, 85, 110, 100},
    {20, 127, 100, 70, 50, 60, 127, 93},
    {110, 60, 90, 40, 110, 90, 70, 114},
    {35, 90, 70, 60, 20, 30, 80, 100},
    {30, 100, 127, 70, 110, 100, 127, 94},
    {80, 50, 30, 20, 90, 120, 60, 125},
    {95, 20, 10, 15, 100, 127, 40, 127},
    {40, 80, 100, 110, 80, 60, 70, 103},
    {50, 80, 90, 20, 80, 70, 80, 103},
    {45, 60, 60, 50, 40, 20, 20, 127},
    {30, 90, 80, 90, 40, 40, 60, 105},
    {50, 30, 127, 70, 127, 60, 60, 113},
    {25, 127, 30, 50, 40, 40, 90, 95},
    {55, 70, 45, 50, 30, 55, 40, 105},
    {45, 80, 40, 55, 20, 40, 30, 110},
    {60, 60, 30, 40, 10, 70, 60, 103},
    {70, 30, 20, 15, 0, 60, 50, 109},
    {50, 90, 60, 70, 100, 40, 30, 100},
    {110, 0, 0, 10, 0, 100, 70, 106},
    {40, 90, 70, 60, 120, 30, 20, 104},
    {50, 127, 90, 60, 50, 50, 40, 103},
    {60, 0, 0, 30, 0, 60, 60, 103},
    {52, 90, 50, 30, 20, 55, 90, 97},
    {70, 50, 30, 55, 60, 80, 50, 101},
    {50, 80, 60, 100, 60, 50, 40, 94},
    {30, 70, 45, 50, 40, 20, 40, 104},
    {100, 40, 20, 25, 10, 127, 40, 127},
    {60, 90, 127, 40, 20, 60, 80, 96},
    {35, 100, 80, 90, 127, 20, 20, 101},
    {62, 40, 15, 20, 5, 80, 110, 92},
    {70, 40, 50, 20, 110, 90, 20, 126},
    {100, 30, 20, 15, 0, 127, 30, 127},
    {40, 60, 50, 70, 90, 30, 40, 97},
    {50, 50, 40, 40, 30, 40, 10, 127},
    {48, 55, 60, 50, 70, 25, 45, 110},
    {68, 60, 70, 40, 60, 0, 30, 127},
    {97, 80, 55, 85, 90, 0, 40, 127},
    {44, 40, 90, 20, 40, 30, 50, 108},
    {56, 70, 45, 60, 100, 10, 80, 112},
    {80, 50, 40, 60, 70, 0, 30, 127},
    {6, 40, 85, 30, 50, 40, 40, 107},
    {100, 60, 55, 40, 60, 10, 30, 121},
    {84, 60, 62, 40, 60, 10, 30, 119},
    {20, 30, 100, 30, 40, 20, 40, 110},
    {30, 45, 80, 50, 70, 30, 50, 109},
    {50, 70, 35, 70, 90, 20, 60, 114},
    {70, 50, 40, 80, 110, 10, 40, 127},
    {60, 60, 50, 55, 90, 30, 60, 109},
    {72, 90, 45, 20, 40, 50, 50, 109},
    {64, 110, 40, 10, 30, 70, 40, 108},
    {40, 10, 90, 30, 30, 10, 20, 117},
    {58, 55, 45, 60, 80, 20, 50, 111},
    {50, 80, 25, 90, 127, 40, 80, 108},
    {45, 50, 55, 40, 80, 60, 90, 106},
    {44, 60, 70, 40, 60, 100, 90, 105},
    {80, 30, 80, 40, 0, 40, 30, 100},
    {90, 15, 90, 30, 0, 30, 30, 105},
    {100, 80, 85, 40, 0, 30, 40, 77},
    {85, 35, 75, 25, 70, 25, 90, 97},
    {45, 60, 50, 90, 40, 100, 20, 127},
    {110, 40, 100, 120, 10, 60, 50, 107},
    {120, 5, 110, 20, 0, 50, 10, 127},
    {95, 20, 95, 50, 0, 20, 40, 102},
    {105, 90, 90, 30, 0, 30, 50, 69},
    {110, 5, 100, 30, 0, 40, 30, 99},
    {70, 25, 55, 30, 110, 40, 90, 108},
    {60, 50, 60, 20, 60, 80, 20, 127},
    {30, 40, 90, 60, 30, 110, 20, 127},
    {120, 110, 80, 20, 0, 10, 20, 75},
    {100, 30, 100, 110, 0, 40, 40, 107},
    {90, 40, 50, 60, 40, 70, 30, 127},
    {40, 80, 60, 40, 20, 120, 20, 84},
    {75, 40, 60, 50, 100, 60, 90, 90},
    {90, 30, 40, 40, 0, 30, 40, 101},
    {100, 127, 85, 40, 0, 30, 30, 69},
    {85, 40, 70, 127, 0, 30, 40, 127},
    /* FM DRUM: TONE, DECAY, LEVEL, FM, NOISE, DRIVE, FEEDBACK, PUNCH */
    {30, 60, 110, 50, 0, 0, 20, 40}, {10, 80, 115, 40, 0, 20, 10, 60}, {60, 45, 105, 10, 90, 0, 0, 30},
    {25, 70, 110, 30, 0, 0, 10, 30}, {55, 55, 110, 30, 0, 0, 10, 30}, {70, 30, 105, 127, 0, 0, 0, 80},
    {90, 50, 100, 110, 0, 0, 60, 40}, {15, 75, 108, 70, 0, 60, 100, 70}, {20, 85, 105, 40, 0, 40, 127, 20},
    {100, 15, 100, 20, 0, 0, 0, 0}, {40, 55, 105, 30, 60, 0, 20, 30}, {80, 20, 105, 60, 110, 30, 0, 60},
    {30, 100, 108, 60, 0, 10, 15, 20}, {20, 60, 112, 90, 0, 30, 30, 90}, {110, 10, 100, 40, 40, 50, 0, 50},
    {18, 70, 100, 50, 10, 127, 60, 60}, {65, 40, 108, 15, 0, 0, 5, 20}, {120, 8, 100, 0, 0, 0, 0, 10},
    {5, 110, 112, 25, 20, 50, 40, 10}, {75, 35, 105, 80, 30, 70, 90, 70}, {45, 90, 108, 127, 0, 0, 0, 0},
    /* BELL: TONE, DECAY, TYPE (0 BELL, 1 DRUM), SPREAD, COLOR, STRIKE, CLICK, LEVEL */
    {60, 70, 0, 20, 64, 80, 20, 110}, {40, 60, 1, 40, 50, 80, 30, 115}, {70, 90, 0, 40, 70, 60, 10, 105},
    {35, 120, 0, 10, 60, 90, 20, 105}, {110, 40, 0, 15, 64, 100, 30, 100}, {85, 25, 0, 90, 30, 110, 40, 105},
    {95, 35, 0, 30, 80, 100, 20, 105}, {120, 55, 0, 5, 75, 127, 10, 95}, {50, 80, 0, 127, 100, 127, 60, 100},
    {65, 75, 0, 127, 64, 80, 10, 105}, {60, 80, 0, 20, 64, 0, 0, 110}, {55, 45, 1, 20, 30, 70, 20, 115},
    {20, 95, 1, 30, 40, 50, 10, 118}, {50, 35, 1, 120, 110, 100, 50, 112}, {45, 50, 1, 127, 127, 90, 40, 110},
    {100, 30, 1, 5, 20, 60, 30, 105}, {40, 127, 0, 30, 64, 70, 0, 100}, {127, 60, 0, 60, 110, 127, 20, 95},
    {55, 70, 0, 50, 10, 90, 20, 105}, {90, 10, 1, 0, 0, 127, 60, 110}, {75, 50, 0, 0, 40, 40, 30, 108},
    /* PARTICLE: TONE, DECAY, LEVEL, DENSITY, SPREAD, RESO, GRIT, FADE */
    {70, 70, 110, 60, 50, 50, 10, 40}, {80, 80, 105, 40, 90, 20, 30, 30}, {95, 110, 100, 80, 70, 30, 20, 60},
    {100, 50, 105, 50, 60, 10, 80, 20}, {85, 60, 105, 10, 0, 110, 0, 0}, {75, 90, 105, 35, 60, 100, 0, 50},
    {115, 35, 105, 110, 40, 20, 40, 80}, {120, 100, 100, 30, 0, 0, 127, 0}, {60, 120, 100, 110, 100, 60, 10, 30},
    {50, 80, 105, 45, 70, 115, 0, 40}, {90, 120, 100, 90, 80, 15, 60, 50}, {127, 25, 105, 60, 30, 40, 90, 60},
    {70, 40, 110, 70, 127, 80, 50, 0}, {110, 60, 100, 127, 60, 5, 30, 90}, {65, 70, 105, 20, 50, 127, 10, 30},
    {105, 90, 100, 127, 127, 0, 50, 10}, {20, 100, 112, 100, 40, 60, 0, 40}, {120, 110, 100, 90, 20, 90, 0, 20},
    {90, 127, 95, 25, 90, 30, 70, 0}, {127, 20, 110, 60, 0, 0, 127, 50}, {100, 100, 100, 25, 80, 120, 0, 60},
    /* WEIRD: TONE, DECAY, MIX, SWEEP, WOBBLE, RATE, NOISE (0..63 low-pass, 64..127 high-pass), LEVEL */
    {25, 60, 30, 60, 30, 70, 40, 110}, {12, 70, 10, 80, 0, 0, 30, 118}, {60, 40, 90, 40, 0, 0, 90, 110},
    {110, 35, 127, 0, 0, 0, 120, 110}, {30, 90, 15, 30, 90, 60, 30, 112}, {20, 80, 20, 40, 110, 110, 40, 112},
    {60, 60, 10, 20, 120, 120, 50, 108}, {70, 110, 0, 0, 127, 30, 30, 105}, {35, 85, 0, 100, 60, 45, 30, 112},
    {80, 30, 20, 127, 0, 0, 100, 108}, {50, 120, 127, 0, 0, 0, 40, 100}, {45, 75, 15, 50, 40, 80, 30, 112},
    {40, 50, 30, 30, 127, 127, 110, 108}, {90, 45, 110, 10, 0, 0, 85, 115}, {5, 100, 0, 110, 10, 20, 20, 120},
    {85, 60, 10, 10, 70, 100, 30, 108}, {65, 25, 40, 60, 20, 90, 60, 110}, {75, 127, 60, 30, 50, 15, 100, 100},
    {15, 80, 60, 70, 90, 120, 20, 115}, {100, 40, 0, 127, 30, 90, 0, 105}, {55, 35, 80, 90, 127, 127, 100, 108}
};
/* the fg_ parameters each knob is: SOUND's three, then DRUM 1's four and DRUM 2's first */
static const uint8_t DM_MAP[NDMODEL][8] = {{0, 2, 7, 1, 3, 5, 4, 6}, {0, 3, 7, 1, 2, 4, 5, 6}, {0, 3, 7, 1, 2, 4, 5, 6},
                                           {0, 2, 7, 1, 3, 4, 5, 6}, {0, 1, 2, 3, 4, 5, 6, 7},
                                           {0, 1, 2, 3, 4, 5, 6, 7}, {0, 1, 2, 3, 4, 5, 6, 7}, {0, 1, 2, 3, 4, 5, 6, 7},
                                           {0, 1, 2, 3, 4, 5, 6, 7}};   /* (1.4.8's four: their knobs' order) */
static const char *const DM_LAB[NDMODEL][8] = {{"TONE", "DECAY", "LEVEL", "SWEEP", "FOLD", "CLICK", "DRIVE", "PUNCH"},
                                               {"TONE", "DECAY", "LEVEL", "BODY", "NOISE", "BRIGHT", "SNAP", "PUNCH"},
                                               {"TONE", "DECAY", "LEVEL", "CLAPS", "SPREAD", "ROOM", "COLOR", "PUNCH"},
                                               {"TONE", "DECAY", "LEVEL", "TENSION", "MATERIAL", "HIT", "DRIVE", "PUNCH"},
                                               {"DENSITY", "DECAY", "COLOR", "RESO", "SHAKE", "GRAIN", "PUNCH", "LEVEL"},
                                               {"TONE", "DECAY", "LEVEL", "FM", "NOISE", "DRIVE", "FEEDBACK", "PUNCH"},
                                               {"TONE", "DECAY", "TYPE", "SPREAD", "COLOR", "STRIKE", "CLICK", "LEVEL"},
                                               {"TONE", "DECAY", "LEVEL", "DENSITY", "SPREAD", "RESO", "GRIT", "FADE"},
                                               {"TONE", "DECAY", "MIX", "SWEEP", "WOBBLE", "RATE", "NOISE", "LEVEL"}};
#define DM_T {60, 10, 50, 25, 45, 1, 0, 0}
static volatile int32_t dm_v[NT][DM_DEEP] = {DM_T, DM_T, DM_T, DM_T};   /* DRUM 1, DRUM 2: the five, KEYS, WANDER, HUMAN */
static volatile int16_t dm_req[NT];             /* a hit asked for: its note + 1 */
typedef union { fgk_memb_t m; fgm_hail_t h; fgk_sweep_t k; fgs_voice_t s; fgs_clap_t c; pc_fmd_t f; pc_bell_t b; pc_part_t p; pc_weird_t w; } dm_slot_t;
static struct {
    dm_slot_t s[2];
    uint8_t md[2], cur;
    int32_t lvl[2];                             /* (SNARE, CLAP: their LEVEL, as at the hit) */
    int32_t sat_lp;
    uint32_t seed;
    uint16_t pk;                                /* (the screen: the last block's peak) */
} dm[NT];
/* (Arctic Scream) each model a source of its own now (KICK, SNARE, CLAP, TOM, HATS: ENG_KICK + its DM_), its KNOB 1 its
 * PRESET (0: the model as it comes, 1..20 FNTSMG-1's); an entry: the model x 21 + the preset */
static uint32_t dm_model(uint32_t t) { return eng_drum(trk[t].eng) ? (uint32_t)(trk[t].eng - ENG_KICK) : 0u; }
static uint32_t dm_entry(uint32_t t) { return dm_model(t) * DM_NPR + (uint32_t)clamp(trk[t].p[ENG_KICK + dm_model(t)][0], 0, DM_NPR - 1); }
static void dm_def(uint32_t t, uint32_t e)      /* an entry's sound: a model as it comes, or a preset (KEYS, WANDER, HUMAN stay) */
{
    uint32_t md, k;
    const uint8_t *d;
    e = e < NDENT ? e : 0u, md = e / DM_NPR, d = DM_PR[e];
    trk[t].p[ENG_KICK + md][0] = (int32_t)(e % DM_NPR);
    for (k = 0; k < 3u; k++) trk[t].p[ENG_KICK + md][1u + k] = d[DM_MAP[md][k]];
    for (k = 0; k < 5u; k++) dm_v[t][k] = d[DM_MAP[md][3u + k]];
}
static void dm_init(uint32_t t) { dm_def(t, 0), dm_v[t][5] = 1, dm_v[t][6] = dm_v[t][7] = 0; }
/* (Arctic Scream) ENV in AHD shapes the drums too: the hit restarts it */
static struct { int32_t e; uint32_t st, hold; } dm_env[NT];
static void drum_begin(voice_t *v) { (void)v; }
static void drum_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    (void)v, (void)p, (void)mono, (void)amp0, (void)amp1;   /* (silent: dm_run plays it) */
}
#define DM_TEXT(nm, m)                                                                                                 \
    static void nm(const volatile int32_t *p, uint32_t k, char *s)                                                     \
    {                                                                                                                  \
        if (k == 0u) put_s(s, DM_ENAME[(m) * DM_NPR + (uint32_t)clamp(p[0], 0, DM_NPR - 1)]);                          \
        else put_u(s, (uint32_t)clamp(p[k], 0, 127));                                                                  \
    }
DM_TEXT(kick_text, DM_KICK)
DM_TEXT(snare_text, DM_SNARE)
DM_TEXT(clap_text, DM_CLAP)
DM_TEXT(tom_text, DM_MEMB)
DM_TEXT(hats_text, DM_HAIL)
DM_TEXT(fmd_text, DM_FMD)
DM_TEXT(part_text, DM_PART)
DM_TEXT(weird_text, DM_WEIRD)
static void bell_text(const volatile int32_t *p, uint32_t k, char *s)   /* (TYPE: BELL, DRUM) */
{
    if (k == 3u) put_s(s, p[3] ? "DRUM" : "BELL");
    else if (k == 0u) put_s(s, DM_ENAME[DM_BELL * DM_NPR + (uint32_t)clamp(p[0], 0, DM_NPR - 1)]);
    else put_u(s, (uint32_t)clamp(p[k], 0, 127));
}
static const engine_t KICK_ENGINE = {"Kick", {"PRESET", "TONE", "DECAY", "LEVEL"}, {DM_NPR - 1, 127, 127, 127}, drum_begin, drum_block, kick_text};
static const engine_t SNARE_ENGINE = {"Snare", {"PRESET", "TONE", "DECAY", "LEVEL"}, {DM_NPR - 1, 127, 127, 127}, drum_begin, drum_block, snare_text};
static const engine_t CLAP_ENGINE = {"Clap", {"PRESET", "TONE", "DECAY", "LEVEL"}, {DM_NPR - 1, 127, 127, 127}, drum_begin, drum_block, clap_text};
static const engine_t TOM_ENGINE = {"Tom", {"PRESET", "TONE", "DECAY", "LEVEL"}, {DM_NPR - 1, 127, 127, 127}, drum_begin, drum_block, tom_text};
static const engine_t HATS_ENGINE = {"Hats", {"PRESET", "DENSITY", "DECAY", "COLOR"}, {DM_NPR - 1, 127, 127, 127}, drum_begin, drum_block, hats_text};
static const engine_t FMD_ENGINE = {"FM Drum", {"PRESET", "TONE", "DECAY", "LEVEL"}, {DM_NPR - 1, 127, 127, 127}, drum_begin, drum_block, fmd_text};
static const engine_t BELL_ENGINE = {"Bell", {"PRESET", "TONE", "DECAY", "TYPE"}, {DM_NPR - 1, 127, 127, 1}, drum_begin, drum_block, bell_text};
static const engine_t PART_ENGINE = {"Particle", {"PRESET", "TONE", "DECAY", "LEVEL"}, {DM_NPR - 1, 127, 127, 127}, drum_begin, drum_block, part_text};
static const engine_t WEIRD_ENGINE = {"Weird", {"PRESET", "TONE", "DECAY", "MIX"}, {DM_NPR - 1, 127, 127, 127}, drum_begin, drum_block, weird_text};
