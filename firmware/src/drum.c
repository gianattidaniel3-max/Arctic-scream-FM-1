/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* The DRUM source: a track's one drum, a MODEL and its sound, played by the track's notes; models from FNTSMG-1 (fg/).
 * A drum is not a voice: a note asks for a hit (dm_req), the track renders it in mix_tracks, its tail its own, whatever
 * the ENV or a note off. A voice still takes the note, silent, for the lists, chords and BANK.
 *   SOUND    MODEL: KICK, SNARE, CLAP, MEMBRANE, HAIL, each with FNTSMG-1's twenty presets after it, and three main
 *            knobs. DRUM 1, DRUM 2: the rest of its sound, then
 *   KEYS     the keys tune it: C3 as its TONE or COLOR says, at the TUNING's pitches (ON), or every key the same (OFF)
 *   WANDER   each hit's pitch off by up to a semitone, at random
 *   HUMAN    each hit's level down by up to 12 dB or so, at random */
#include "fg/fg_dsp.h"
#include "fg/fg_models.inc"
enum { DM_KICK, DM_SNARE, DM_CLAP, DM_MEMB, DM_HAIL, NDMODEL };   /* (as MODEL scrolls them) */
#define DM_NPR 21u                              /* a model, then its twenty presets */
#define NDENT (NDMODEL * DM_NPR)
#define DM_DEEP 8u
static const char *const DM_NAME[NDMODEL] = {"KICK", "SNARE", "CLAP", "MEMBRANE", "HAIL"};
/* each entry's name and its eight, as FNTSMG-1 keeps them (its parameters' order; the first of each model: its INICIAL) */
static const char *const DM_ENAME[NDENT] = {
    "KICK", "909", "808", "DRIVEN", "HARD", "SUB", "BOOM", "DEEP", "TECHNO", "HOUSE", "GABBER", "DEMBOW", "TRAP", "RUMBLE", "MINIMAL", "CLICK", "BREAKS", "DISTORTED", "LO-FI", "FAR", "PUNCH",
    "SNARE", "909", "808", "ELECTRO", "TECHNO", "HOUSE", "BREAK", "TRAP", "DEMBOW", "FAT", "PICCOLO", "LO-FI", "INDUSTRIAL", "RIMSHOT", "CROSS", "LONG", "SHORT", "SOFT", "DUB", "NOISE", "SNARE TOM",
    "CLAP", "808", "909", "DRY", "HALL", "SNAP", "FAR", "CROWD", "ONE HAND", "REGGAETON", "GARAGE", "LONG", "DARK", "BRIGHT", "BROKEN", "CHURCH", "TRAP", "SPACE", "PAPER", "DUB", "SOFT",
    "MEMBRANE", "LOW TOM", "TIMBALE", "SKIN", "TAMBORA", "CONGA", "DEEP", "HIGH TOM", "MID TOM", "SURDO", "LEGUERO", "CAJON", "DJEMBE", "BOMBA", "TECHNO TOM", "ELECTRO", "LOOSE SKIN", "CUMBIA", "HIT", "BOOM BAP", "DRIVEN",
    "HAIL", "CLOSED HAT", "OPEN HAT", "SHAKER", "RAIN", "WHISTLE", "TICK", "808 HAT", "909 OPEN", "SHORT", "MARACA", "SAND", "SPARKS", "STEAM", "METALLIC", "SANDPAPER", "FIRE", "GUIRO", "DARK", "LONG", "RESO"
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
    {85, 40, 70, 127, 0, 30, 40, 127}
};
/* the fg_ parameters each knob is: SOUND's three, then DRUM 1's four and DRUM 2's first */
static const uint8_t DM_MAP[NDMODEL][8] = {{0, 2, 7, 1, 3, 5, 4, 6}, {0, 3, 7, 1, 2, 4, 5, 6}, {0, 3, 7, 1, 2, 4, 5, 6},
                                           {0, 2, 7, 1, 3, 4, 5, 6}, {0, 1, 2, 3, 4, 5, 6, 7}};
static const char *const DM_LAB[NDMODEL][8] = {{"TONE", "DECAY", "LEVEL", "SWEEP", "FOLD", "CLICK", "DRIVE", "PUNCH"},
                                               {"TONE", "DECAY", "LEVEL", "BODY", "NOISE", "BRIGHT", "SNAP", "PUNCH"},
                                               {"TONE", "DECAY", "LEVEL", "CLAPS", "SPREAD", "ROOM", "COLOR", "PUNCH"},
                                               {"TONE", "DECAY", "LEVEL", "TENSION", "MATERIAL", "HIT", "DRIVE", "PUNCH"},
                                               {"DENSITY", "DECAY", "COLOR", "RESO", "SHAKE", "GRAIN", "PUNCH", "LEVEL"}};
#define DM_T {60, 10, 50, 25, 45, 1, 0, 0}
static volatile int32_t dm_v[NT][DM_DEEP] = {DM_T, DM_T, DM_T, DM_T};   /* DRUM 1, DRUM 2: the five, KEYS, WANDER, HUMAN */
static volatile int16_t dm_req[NT];             /* a hit asked for: its note + 1 */
typedef union { fgk_memb_t m; fgm_hail_t h; fgk_sweep_t k; fgs_voice_t s; fgs_clap_t c; } dm_slot_t;
static struct {
    dm_slot_t s[2];
    uint8_t md[2], cur;
    int32_t lvl[2];                             /* (SNARE, CLAP: their LEVEL, as at the hit) */
    int32_t sat_lp;
    uint32_t seed;
    uint16_t pk;                                /* (the screen: the last block's peak) */
} dm[NT];
static uint32_t dm_model_of(int32_t e) { return (uint32_t)clamp(e, 0, NDENT - 1) / DM_NPR; }
static uint32_t dm_model(uint32_t t) { return dm_model_of(trk[t].p[ENG_DRUM][0]); }
static void dm_def(uint32_t t, uint32_t e)      /* an entry's sound: a model as it comes, or a preset (KEYS, WANDER, HUMAN stay) */
{
    uint32_t md, k;
    const uint8_t *d;
    e = e < NDENT ? e : 0u, md = e / DM_NPR, d = DM_PR[e];
    for (k = 0; k < 3u; k++) trk[t].p[ENG_DRUM][1u + k] = d[DM_MAP[md][k]];
    for (k = 0; k < 5u; k++) dm_v[t][k] = d[DM_MAP[md][3u + k]];
}
static void dm_init(uint32_t t) { dm_def(t, 0), dm_v[t][5] = 1, dm_v[t][6] = dm_v[t][7] = 0; }
static void dm_kill(uint32_t t, uint32_t s)
{
    dm_slot_t *v = &dm[t].s[s];
    switch (dm[t].md[s]) {
    case DM_MEMB: v->m.kill = FG_DECAY[2]; break;
    case DM_HAIL: v->h.kill = FG_DECAY[2]; break;
    case DM_KICK: v->k.kill = FG_DECAY[2]; break;
    case DM_SNARE: v->s.kill = FG_DECAY[2]; break;
    default: v->c.kill = FG_DECAY[6]; break;    /* (a clap: as FNTSMG-1's, a little slower) */
    }
}
static uint32_t dm_scale(uint32_t inc, uint32_t f) { return (uint32_t)(((uint64_t)inc * f) >> 16); }
static void dm_hit(uint32_t t, uint32_t note)
{
    uint32_t model = dm_model(t), k, o = dm[t].cur, n = o ^ 1u, vel = 110u, f;
    uint8_t p[8];
    int32_t p16 = tn_p16(note) - 48 * 16, w = clamp(dm_v[t][6], 0, 127), h = clamp(dm_v[t][7], 0, 127);
    uint32_t r;
    dm[t].seed = dm[t].seed ? dm[t].seed : 2654435769u + t;
    r = fg_rand(&dm[t].seed);
    for (k = 0; k < 8u; k++)
        p[DM_MAP[model][k]] = (uint8_t)clamp(k < 3u ? trk[t].p[ENG_DRUM][1u + k] : dm_v[t][k - 3u], 0, 127);
    if (!dm_v[t][5]) p16 = 0;                   /* (KEYS OFF) */
    if (w) p16 += (int32_t)((r >> 8 & 255u) - 128u) * w / 127 * 16 / 128;   /* WANDER: +-1 semitone at its top */
    if (h) vel = vel * (127u - ((r >> 16 & 255u) * (uint32_t)h >> 8) * 3u / 4u) / 127u;   /* HUMAN */
    dm_kill(t, o);                              /* the old one out, ~3 ms */
    dm[t].cur = (uint8_t)n, dm[t].md[n] = (uint8_t)model;
    f = pow2_q16(p16);                          /* (C3: its TONE) */
    switch (model) {
    case DM_MEMB: {
        fgk_memb_t *v = &dm[t].s[n].m;
        v->seed = dm[t].seed + 1u;
        memb_trig(v, p, vel);
        for (k = 0; k < 4u; k++) v->inc0[k] = dm_scale(v->inc0[k], f), v->inc[k] = v->inc0[k];
        break;
    }
    case DM_KICK: {
        fgk_sweep_t *v = &dm[t].s[n].k;
        v->seed = dm[t].seed + 1u;
        sweep_trig(v, p, vel);
        v->inc_end = dm_scale(v->inc_end, f);
        break;
    }
    case DM_SNARE: {
        fgs_voice_t *v = &dm[t].s[n].s;
        v->seed = dm[t].seed + 1u;
        voice_trig(v, p, vel);
        for (k = 0; k < 2u; k++) v->inc0[k] = dm_scale(v->inc0[k], f), v->inc[k] = v->inc0[k];
        dm[t].lvl[n] = dm_sq2(p[7]);
        break;
    }
    case DM_CLAP: {
        fgs_clap_t *v = &dm[t].s[n].c;
        p[0] = (uint8_t)clamp((int32_t)p[0] + p16 * 559 / 1600, 0, 127);   /* (TONE: 1.9 octaves in 127) */
        v->seed = dm[t].seed + 1u;
        clap_trig(v, p, vel);
        dm[t].lvl[n] = dm_sq2(p[7]);
        break;
    }
    default: {
        fgm_hail_t *v = &dm[t].s[n].h;
        p[2] = (uint8_t)clamp((int32_t)p[2] + p16 * 341 / 1600, 0, 127);   /* (COLOR: 3.1 octaves in 127) */
        v->seed = dm[t].seed + 1u;
        hail_trig(v, p, vel);
        break;
    }
    }
}
static int dm_on(uint32_t t, uint32_t s)
{
    const dm_slot_t *v = &dm[t].s[s];
    switch (dm[t].md[s]) {
    case DM_MEMB: return v->m.on;
    case DM_HAIL: return v->h.on;
    case DM_KICK: return v->k.on;
    case DM_SNARE: return v->s.on;
    default: return v->c.on;
    }
}
static int dm_wanted(uint32_t t) { return dm_req[t] || dm_on(t, 0) || dm_on(t, 1); }
static void dm_run(uint32_t t, int32_t *x, uint32_t n)
{
    uint32_t s, i, pk = 0, md;
    int32_t a = clamp(dm_v[t][3], 0, 127), lp = dm[t].sat_lp;
    int req = dm_req[t];
    if (req) dm_req[t] = 0, dm_hit(t, (uint32_t)req - 1u);
    for (s = 0; s < 2u; s++) {
        dm_slot_t *v = &dm[t].s[s];
        switch (dm[t].md[s]) {
        case DM_MEMB: memb_render(&v->m, x, n); break;
        case DM_HAIL: hail_render(&v->h, x, n); break;
        case DM_KICK: sweep_render(&v->k, x, n); break;
        case DM_SNARE: voice_render(&v->s, dm[t].lvl[s], x, n); break;
        default: clap_render(&v->c, dm[t].lvl[s], x, n); break;
        }
    }
    md = dm[t].md[dm[t].cur];
    if (md == DM_MEMB || md == DM_KICK) {       /* (DRIVE, as FNTSMG-1's SATURA: on the kicks) */
        int32_t g = 4096 + a * 290, pres = a * 160;
        for (i = 0; i < n; i++) {
            int32_t y = fg_soft((int32_t)(((int64_t)x[i] * g) >> 12));
            lp += fg_mul15(y - lp, 3113);       /* ~700 Hz */
            x[i] = y + fg_mul15(y - lp, pres);
        }
        dm[t].sat_lp = lp;
    }
    for (i = 0; i < n; i++) x[i] = md == DM_HAIL ? x[i] * 2 : x[i] >> 1;   /* (level with the other sources) */
    for (i = 0; i < n; i += 16u) {
        uint32_t v = (uint32_t)(x[i] < 0 ? -x[i] : x[i]);
        if (v > pk) pk = v;
    }
    dm[t].pk = (uint16_t)(pk > 65535u ? 65535u : pk);
}
static void drum_begin(voice_t *v) { (void)v; }
static void drum_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    (void)v, (void)p, (void)mono, (void)amp0, (void)amp1;   /* (silent: dm_run plays it) */
}
static void drum_text(const volatile int32_t *p, uint32_t k, char *s)
{
    if (k == 0u) put_s(s, DM_ENAME[clamp(p[0], 0, NDENT - 1)]);
    else put_u(s, (uint32_t)clamp(p[k], 0, 127));
}
static const engine_t DRUM_ENGINE = {"Drum", {"MODEL", "TONE", "DECAY", "LEVEL"}, {NDENT - 1, 127, 127, 127}, drum_begin,
                                     drum_block, drum_text};
