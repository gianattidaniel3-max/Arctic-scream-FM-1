/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* LFO's BANK and BANK 2: a track's own bank of four narrow resonators, in front of its filter and sends, tuned by what the
 * track plays (its notes at the TUNING, exact), so whatever passes through rings at those pitches.
 *   MODE     OFF: nothing runs. CHORD: the notes the track holds (fewer than four: octaves up). HARM: harmonics 1, 2, 3, 5
 *            of its lowest note. GHOST: CHORD a quarter tone (50 cents) up: at 24 EDO, the other of Alois Haba's fields
 *   RING     how narrow, how long it rings: Q 2 .. 256
 *   MIX      dry .. the bank alone
 *   WIND     noise into the bank: the track sings its pitches over silence, played or not
 *   GLACIER  a new pitch glides there, up to ~8 s. Let go: the bank stays on its last pitches
 * Chamberlin's state-variable filter in Q30 with 64-bit products: a band's frequency is a coefficient of 2 sin(pi f / fs),
 * from pitch_inc, so a low note is as exactly tuned as a high one, and a long ring dies away without a floor. No floats. */
#define BK_BANDS 4u
enum { BK_OFF, BK_CHORD, BK_HARM, BK_GHOST, NBKMODE };
static const char *const BK_NAME[NBKMODE] = {"OFF", "CHORD", "HARM", "GHOST"};
#define BK_INC_MAX 715827882u                   /* fs / 6: over it, a band takes nothing in (f stays stable) */
#define BK_CLAMP (1 << 29)
#define BK_QUIET 64                             /* a ring under this (as x << 4): over */
typedef struct {
    int32_t lp[BK_BANDS], bp[BK_BANDS], f[BK_BANDS], tg[BK_BANDS];
    int16_t p16[BK_BANDS];                      /* (the screen: its pitch, 1/16 semitone) */
    uint16_t lv[BK_BANDS];                      /* (the screen: its peak this block) */
    uint8_t on, ring;                           /* bands taking input; ringing (the track kept on after its voices) */
} bank_t;
static bank_t bk[NT];
static uint32_t bk_rng = 0x1F2E3D4Cu;
/* (grimm.c's list of the notes on, further down) */
static uint8_t lat_note[NV], lat_trk[NV];
static volatile uint32_t nlat;

static int32_t bk_coef(uint32_t inc)            /* 2 sin(pi inc / 2^32), Q30 */
{
    int32_t x = (int32_t)(((uint64_t)inc * 3373259426u) >> 32);   /* (pi, Q30) */
    int32_t x2 = (int32_t)(((int64_t)x * x) >> 30), x3 = (int32_t)(((int64_t)x2 * x) >> 30);
    int32_t x5 = (int32_t)(((int64_t)x3 * x2) >> 30);
    return 2 * (x - x3 / 6 + x5 / 120);
}
static uint32_t bk_inc(int32_t p16, uint32_t note)   /* a pitch's phase increment, TUNE and TUNING's rest in */
{
    uint32_t inc = pitch_inc((uint32_t)clamp(p16, 0, 2047));
    return inc + (uint32_t)((int32_t)(inc >> 12) * (tune_f + tn_fine(note)));
}
static void bk_tune(uint32_t t, int32_t mode)   /* the bands' targets, from the notes the track holds */
{
    static const uint8_t H[BK_BANDS] = {1, 2, 3, 5};
    bank_t *B = &bk[t];
    uint32_t ns[BK_BANDS], used[BK_BANDS], m = 0, i, j, oct = (uint32_t)tn_edo();
    uint32_t nl = nlat;
    for (i = 0; i < nl && i < NV; i++) {        /* (low to high, the same once: a band keeps its note) */
        uint32_t x = lat_note[i];
        if (lat_trk[i] != t) continue;
        for (j = 0; j < m && ns[j] != x; j++)
            ;
        if (j < m) continue;
        if (m == BK_BANDS && x >= ns[m - 1u]) continue;
        j = m < BK_BANDS ? m++ : m - 1u;
        while (j && ns[j - 1u] > x) ns[j] = ns[j - 1u], j--;
        ns[j] = x;
    }
    if (!m) {                                   /* (let go: its last pitches; none yet: ROOT) */
        if (B->tg[0]) return;
        ns[0] = (uint32_t)tn_anchor(), m = 1;
    }
    B->on = 0;
    for (i = 0; i < BK_BANDS; i++) {
        uint32_t n = ns[i % m] + oct * (i / m), inc, k;
        int32_t p;
        for (k = 0; k < i; k++)                 /* (an octave up already played: the next one, no band doubled) */
            if (used[k] == n) n += oct, k = (uint32_t)-1;
        used[i] = n;
        if (mode == BK_HARM) {
            n = ns[0];
            p = tn_p16(n);
            inc = bk_inc(p, n);
            inc = (uint64_t)inc * H[i] > BK_INC_MAX ? BK_INC_MAX + 1u : inc * H[i];
            p += i == 1u ? 192 : i == 2u ? 304 : i == 3u ? 446 : 0;
        } else {
            if (n > 127u) continue;             /* (over the notes: a band less) */
            p = tn_p16(n) + (mode == BK_GHOST ? 8 : 0);
            inc = bk_inc(p, n);
        }
        B->p16[i] = (int16_t)p;
        if (inc > BK_INC_MAX) continue;
        B->tg[i] = bk_coef(inc);
        B->on |= (uint8_t)(1u << i);
    }
}
static int bk_wanted(uint32_t t, uint32_t used)  /* it runs this block */
{
    int32_t mode = trk[t].bk_mode;
    return mode > BK_OFF && mode < NBKMODE && (((used >> t) & 1u) || trk[t].bk_wind > 0 || bk[t].ring);
}
static void bk_run(uint32_t t, int32_t *x, uint32_t n)
{
    bank_t *B = &bk[t];
    int32_t ringv = lfo_move(t, LD_BKRING, clamp(trk[t].bk_ring, 0, 127), 127), mix = lfo_move(t, LD_BKMIX, clamp(trk[t].bk_mix, 0, 127), 127) * 258,
            wind = clamp(trk[t].bk_wind, 0, 127);
    int32_t q = (int32_t)(pow2_q16(-ringv * 192 / 18) << 13);   /* (0.5 .. 0.5 / 2^7, Q30) */
    int32_t ns = wind ? (int32_t)(((uint32_t)wind * pow2_q16(ringv * 192 / 36 - ringv * 3)) >> 20) : 0;   /* (the narrower, the more
                                                 * goes in: as loud at any RING, with mk) */
    int32_t mk = (int32_t)(pow2_q16(ringv * 3) >> 4);   /* (the narrower, the less of a wide sound is left: up to 4 x, Q12) */
    int32_t g = clamp(trk[t].bk_glacier, 0, 127), alpha = 65536 / (1 + g * g / 36), lp0, lp1, lp2, lp3, bp0, bp1, bp2, bp3;
    int32_t f0, f1, f2, f3;
    uint32_t i, on, pk0 = 0, pk1 = 0, pk2 = 0, pk3 = 0;
    bk_tune(t, trk[t].bk_mode);
    for (i = 0; i < BK_BANDS; i++)
        B->f[i] = B->f[i] ? B->f[i] + (int32_t)(((int64_t)(B->tg[i] - B->f[i]) * alpha) >> 16) : B->tg[i];
    f0 = B->f[0], f1 = B->f[1], f2 = B->f[2], f3 = B->f[3];
    lp0 = B->lp[0], lp1 = B->lp[1], lp2 = B->lp[2], lp3 = B->lp[3];
    bp0 = B->bp[0], bp1 = B->bp[1], bp2 = B->bp[2], bp3 = B->bp[3];
    on = B->on;
#define BK_BAND(L, P, F, K, PK)                                                                                        \
    do {                                                                                                               \
        int32_t hp;                                                                                                    \
        L += (int32_t)(((int64_t)F * P) >> 30);                                                                        \
        hp = ((on >> K & 1u) ? in : 0) - L - (int32_t)(((int64_t)q * P) >> 30);                                        \
        P += (int32_t)(((int64_t)F * hp) >> 30);                                                                       \
        L = clamp(L, -BK_CLAMP, BK_CLAMP), P = clamp(P, -BK_CLAMP, BK_CLAMP);                                          \
        PK |= (uint32_t)(P < 0 ? -P : P);                                                                              \
    } while (0)
    for (i = 0; i < n; i++) {
        int32_t d = x[i], in = d << 4, w;
        if (ns) {
            bk_rng ^= bk_rng << 13, bk_rng ^= bk_rng >> 17, bk_rng ^= bk_rng << 5;
            in += ((int32_t)bk_rng >> 16) * ns;
        }
        in = (int32_t)(((int64_t)q * in) >> 30);
        BK_BAND(lp0, bp0, f0, 0u, pk0);
        BK_BAND(lp1, bp1, f1, 1u, pk1);
        BK_BAND(lp2, bp2, f2, 2u, pk2);
        BK_BAND(lp3, bp3, f3, 3u, pk3);
        w = soft_knee((((bp0 >> 6) + (bp1 >> 6) + (bp2 >> 6) + (bp3 >> 6)) >> 2) * (mk >> 2) >> 8, 100000) << 2;
        x[i] = d + mulq15(w - d, mix);
    }
#undef BK_BAND
    B->lp[0] = lp0, B->lp[1] = lp1, B->lp[2] = lp2, B->lp[3] = lp3;
    B->bp[0] = bp0, B->bp[1] = bp1, B->bp[2] = bp2, B->bp[3] = bp3;
    B->lv[0] = (uint16_t)(pk0 >> 13), B->lv[1] = (uint16_t)(pk1 >> 13), B->lv[2] = (uint16_t)(pk2 >> 13), B->lv[3] = (uint16_t)(pk3 >> 13);
    B->ring = (pk0 | pk1 | pk2 | pk3) >= BK_QUIET;
    if (!B->ring)                               /* (nothing left: exactly silent) */
        for (i = 0; i < BK_BANDS; i++) B->lp[i] = B->bp[i] = 0;
}
static void bk_off(uint32_t t)                  /* MODE OFF: it starts from silence next time */
{
    bank_t *B = &bk[t];
    uint32_t i;
    if (!B->ring && !B->f[0]) return;
    B->ring = 0;
    for (i = 0; i < BK_BANDS; i++) B->lp[i] = B->bp[i] = B->f[i] = B->tg[i] = 0, B->lv[i] = 0;
}
