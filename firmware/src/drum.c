/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* The DRUM source: a track's one drum, a MODEL and its sound, played by the track's notes; models from FNTSMG-1 (fg/).
 * A drum is not a voice: a note asks for a hit (dm_req), the track renders it in mix_tracks, its tail its own, whatever
 * the ENV or a note off. A voice still takes the note, silent, for the lists, chords and BANK.
 *   SOUND    MODEL and its three main knobs. DRUM 1, DRUM 2: the rest of its sound, then
 *   KEYS     the keys tune it: C3 as its TONE or COLOR says, at the TUNING's pitches (ON), or every key the same (OFF)
 *   WANDER   each hit's pitch off by up to a semitone, at random
 *   HUMAN    each hit's level down by up to 12 dB or so, at random */
#include "fg/fg_dsp.h"
#include "fg/fg_models.inc"
enum { DM_MEMB, DM_HAIL, NDMODEL };
#define DM_DEEP 8u
static const char *const DM_NAME[NDMODEL] = {"MEMBRANE", "HAIL"};
/* the fg_ parameters each knob is: SOUND's three, then DRUM 1's four and DRUM 2's first */
static const uint8_t DM_MAP[NDMODEL][8] = {{0, 2, 7, 1, 3, 4, 5, 6}, {0, 1, 2, 3, 4, 5, 6, 7}};
static const char *const DM_LAB[NDMODEL][8] = {{"TONE", "DECAY", "LEVEL", "TENSION", "MATERIAL", "HIT", "DRIVE", "PUNCH"},
                                               {"DENSITY", "DECAY", "COLOR", "RESO", "SHAKE", "GRAIN", "PUNCH", "LEVEL"}};
static const uint8_t DM_DEF[NDMODEL][8] = {{48, 60, 110, 55, 50, 70, 25, 45}, {80, 30, 80, 40, 0, 40, 30, 100}};
#define DM_T {55, 50, 70, 25, 45, 1, 0, 0}
static volatile int32_t dm_v[NT][DM_DEEP] = {DM_T, DM_T, DM_T, DM_T};   /* DRUM 1, DRUM 2: the five, KEYS, WANDER, HUMAN */
static volatile int16_t dm_req[NT];             /* a hit asked for: its note + 1 */
typedef union { fgk_memb_t m; fgm_hail_t h; } dm_slot_t;
static struct {
    dm_slot_t s[2];
    uint8_t md[2], cur;
    int32_t sat_lp;
    uint32_t seed;
    uint16_t pk;                                /* (the screen: the last block's peak) */
} dm[NT];
static void dm_def(uint32_t t, uint32_t model)  /* a MODEL's sound as it comes */
{
    const uint8_t *d = DM_DEF[model < NDMODEL ? model : 0u];
    uint32_t k;
    for (k = 0; k < 3u; k++) trk[t].p[ENG_DRUM][1u + k] = d[k];
    for (k = 0; k < 5u; k++) dm_v[t][k] = d[3u + k];
    dm_v[t][5] = 1, dm_v[t][6] = dm_v[t][7] = 0;
}
static void dm_hit(uint32_t t, uint32_t note)
{
    uint32_t model = (uint32_t)clamp(trk[t].p[ENG_DRUM][0], 0, NDMODEL - 1), k, o = dm[t].cur, n = o ^ 1u, vel = 110u;
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
    if (dm[t].md[o] == DM_MEMB) dm[t].s[o].m.kill = FG_DECAY[2]; else dm[t].s[o].h.kill = FG_DECAY[2];   /* the old one out, ~3 ms */
    dm[t].cur = (uint8_t)n, dm[t].md[n] = (uint8_t)model;
    if (model == DM_MEMB) {
        fgk_memb_t *v = &dm[t].s[n].m;
        uint32_t f = pow2_q16(p16);             /* (C3: its TONE) */
        v->seed = dm[t].seed + 1u;
        memb_trig(v, p, vel);
        for (k = 0; k < 4u; k++) v->inc0[k] = (uint32_t)(((uint64_t)v->inc0[k] * f) >> 16), v->inc[k] = v->inc0[k];
    } else {
        fgm_hail_t *v = &dm[t].s[n].h;
        p[2] = (uint8_t)clamp((int32_t)p[2] + p16 * 341 / 1600, 0, 127);   /* (COLOR: 3.1 octaves in 127) */
        v->seed = dm[t].seed + 1u;
        hail_trig(v, p, vel);
    }
}
static int dm_wanted(uint32_t t)
{
    return dm_req[t] || (dm[t].md[0] == DM_MEMB ? dm[t].s[0].m.on : dm[t].s[0].h.on) || (dm[t].md[1] == DM_MEMB ? dm[t].s[1].m.on : dm[t].s[1].h.on);
}
static void dm_run(uint32_t t, int32_t *x, uint32_t n)
{
    uint32_t s, i, pk = 0;
    int32_t a = clamp(dm_v[t][3], 0, 127), lp = dm[t].sat_lp;
    int req = dm_req[t];
    if (req) dm_req[t] = 0, dm_hit(t, (uint32_t)req - 1u);
    for (s = 0; s < 2u; s++) {
        if (dm[t].md[s] == DM_MEMB) memb_render(&dm[t].s[s].m, x, n);
        else hail_render(&dm[t].s[s].h, x, n);
    }
    if (dm[t].md[dm[t].cur] == DM_MEMB) {       /* (DRIVE, as FNTSMG-1's SATURA: on the membrane) */
        int32_t g = 4096 + a * 290, pres = a * 160;
        for (i = 0; i < n; i++) {
            int32_t y = fg_soft((int32_t)(((int64_t)x[i] * g) >> 12));
            lp += fg_mul15(y - lp, 3113);       /* ~700 Hz */
            x[i] = y + fg_mul15(y - lp, pres);
        }
        dm[t].sat_lp = lp;
    }
    for (i = 0; i < n; i++) x[i] = dm[t].md[dm[t].cur] == DM_MEMB ? x[i] >> 1 : x[i] * 2;   /* (level with the other sources) */
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
    if (k == 0u) put_s(s, DM_NAME[clamp(p[0], 0, NDMODEL - 1)]);
    else put_u(s, (uint32_t)clamp(p[k], 0, 127));
}
static const engine_t DRUM_ENGINE = {"Drum", {"MODEL", "TONE", "DECAY", "LEVEL"}, {NDMODEL - 1, 127, 127, 127}, drum_begin,
                                     drum_block, drum_text};
