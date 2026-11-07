/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's LFO, two a track now (LFO 1: the track's lfo_* as ever, in its sounds too; LFO 2: lf2_*; both saved with the
 * project). Each: RATE, DEPTH, DESTINATION, SHAPE (its page) and MODE, START, FADE, SYNC (its MODE page).
 *   RATE: ~60 s a cycle (0) to ~20 Hz (127), exponential; SYNC ON: note values at the tempo (LS_DIV: 1/32 .. 16 bars)
 *   DEPTH: 0..100 %; 0: off
 *   DESTINATION: PITCH (100 %: +-1 octave), VOLUME, PAN, the source's four knobs, REV SEND, GRAIN SEND, TRK CUTOFF, and
 *   (Arctic Scream) RESONANCE, DLY SEND, DRIVE (the track's), ATTACK, DECAY, RELEASE (the ENV's; AHD: its ATTACK, HOLD,
 *   DECAY), the other LFO's RATE, its DEPTH, BANK MIX, BANK RING, DRUM 1's four. It moves the destination around its knob,
 *   the knob itself not moved (100 %: +-half its range)
 *   SHAPE: SINE, TRIANGLE, SAW UP, SAW DOWN, SQUARE, RANDOM (a value a cycle), DRIFT (a smooth wander)
 *   MODE: FREE (never restarted), TRIG (a note restarts it at START), HOLD (runs free; a note holds its value till the
 *   next), ONCE (a note starts one cycle from START, then it stays), HALF (half a cycle, then it stays)
 *   START: the phase a note restarts it at (0..360 degrees); FADE: after a note, DEPTH comes in over ~0..10 s (0: at once)
 * Audio ISR, a control step (ctl_hook: lfo_ctl); notes: lfo_note (the main loop's note_on, a bit a track). */

enum { LD_PITCH, LD_VOLUME, LD_PAN, LD_K1, LD_K2, LD_K3, LD_K4, LD_REV, LD_GRAIN, LD_CUT, LD_RES, LD_DLY, LD_DRIVE,
       LD_ATK, LD_DEC, LD_REL, LD_ORATE, LD_ODEPTH, LD_BKMIX, LD_BKRING, LD_D1, LD_D2, LD_D3, LD_D4, NLFODEST };
enum { LS_SINE, LS_TRI, LS_SAWUP, LS_SAWDN, LS_SQR, LS_RND, LS_DRIFT, NLFOSHAPE };
enum { LM_FREE, LM_TRIG, LM_HOLD, LM_ONCE, LM_HALF, NLFOMODE };
static const char *const LFO_SHAPE_NAME[NLFOSHAPE] = {"SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "RANDOM",
                                                      "DRIFT"};
static const char *const LFO_SHAPE_SHORT[NLFOSHAPE] = {"SIN", "TRI", "SAWUP", "SAWDN", "SQR", "RND", "DRIFT"};   /* (slabs) */
static const char *const LFO_DEST_NAME[NLFODEST] = {"PITCH", "VOLUME", "PAN", 0, 0, 0, 0, "REV SEND", "GRAIN SEND",
                                                    "TRK CUTOFF", "RESONANCE", "DLY SEND", "DRIVE", "ATTACK", "DECAY",
                                                    "RELEASE", "OTHER RATE", "OTHER DEPTH", "BANK MIX", "BANK RING",
                                                    "DRUM1 K1", "DRUM1 K2", "DRUM1 K3", "DRUM1 K4"};
static const char *const LFO_MODE_NAME[NLFOMODE] = {"FREE", "TRIG", "HOLD", "ONCE", "HALF"};
#define NLSDIV 12
static const uint16_t LS_DIV[NLSDIV] = {3, 6, 12, 24, 48, 96, 192, 384, 768, 1536, 3072, 6144};   /* SYNC: 1/24 beats a cycle */
static const char *const LS_DIV_NAME[NLSDIV] = {"1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS",
                                                "8 BARS", "16 BARS", "32 BARS", "64 BARS"};
#define LFO_SMOOTH 7045                         /* the output's one-pole a control step (Q15): ~3 ms */

/* LFO 2 and both LFOs' MODE pages (main loop writes; saved with the project: store.c) */
static volatile int32_t lf2_rate[NT] = {40, 40, 40, 40}, lf2_depth[NT], lf2_dest[NT] = {LD_PAN, LD_PAN, LD_PAN, LD_PAN},
                        lf2_shape[NT];
static volatile int32_t lf_mode[NT][2], lf_start[NT][2], lf_fade[NT][2], lf_sync[NT][2];
static volatile uint32_t lfo_note;              /* main loop: a note began on track t (bit t); audio takes them */
static volatile uint32_t lfo_bpm10 = 1200;      /* main loop: the tempo x 10 (SYNC) */

static volatile uint32_t lfo_ph[NT][2];         /* audio: the phase (a cycle: 2^32) */
static int32_t lfo_a[NT][2], lfo_b[NT][2], lfo_hv[NT][2], lfo_fd[NT][2];   /* RANDOM's value, DRIFT's from and to,
                                                 * HOLD's held value, FADE's way in (Q15) */
static uint8_t lfo_done[NT][2];                 /* ONCE, HALF: its cycle done (it stays) */
static volatile int32_t lfo_amt[NT][2];         /* audio: the output times DEPTH, smoothed (Q15, +-32767; 0: off) */
static int32_t lfo_pm[NV][4];                   /* audio: a voice's four, one of them moved */
static volatile uint8_t lk_m[NT], lk_v[NT][4];  /* audio: the step playing's locks (seq.c): which of the four, their
                                                 * values (the knob's in their place while it plays) */
static uint32_t lfo_rng = 0x2545F491u;

static int32_t lf_get(uint32_t t, uint32_t i, uint32_t k)   /* LFO i's RATE, DEPTH, DESTINATION, SHAPE (k 0..3) */
{
    if (!i) return k == 0u ? trk[t].lfo_rate : k == 1u ? trk[t].lfo_depth : k == 2u ? trk[t].lfo_dest : trk[t].lfo_shape;
    return k == 0u ? lf2_rate[t] : k == 1u ? lf2_depth[t] : k == 2u ? lf2_dest[t] : lf2_shape[t];
}
static uint32_t lfo_hz_q16(int32_t rate)        /* RATE -> 2^(rate x 10.23 / 127), Q16: x 1/60 Hz (1 .. ~1200) */
{
    return pow2_q16(clamp(rate, 0, 127) * 1965 / 127);
}
static int32_t lfo_shape(uint32_t s, uint32_t ph, int32_t a, int32_t b)   /* SHAPE s at phase ph: Q15 */
{
    int32_t x = (int32_t)(ph >> 16), f, k;
    switch (s) {
    case LS_TRI: return x < 32768 ? x * 2 - 32767 : (65535 - x) * 2 - 32767;
    case LS_SAWUP: return x - 32768;
    case LS_SAWDN: return 32767 - x;
    case LS_SQR: return ph < 0x80000000u ? 32767 : -32767;
    case LS_RND: return a;
    case LS_DRIFT:                              /* from a to b over the cycle, smoothstep */
        f = x >> 1;
        k = (int32_t)(((f * f) >> 15) * (98304 - 2 * f) >> 15);
        return a + (int32_t)(((int64_t)(b - a) * k) >> 15);
    default: return sine_i(ph);
    }
}
static int32_t lfo_rand(void)
{
    uint32_t s = lfo_rng;
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    lfo_rng = s;
    return (int32_t)(s >> 16) - 32768;
}
static int32_t lfo_mv1(uint32_t t, uint32_t i, uint32_t dest, int32_t v, int32_t mx)   /* LFO i's on v, if dest is its */
{
    int32_t a = lfo_amt[t][i];
    if (!a || lf_get(t, i, 2u) != (int32_t)dest) return v;
    return clamp(v + (a * mx + (a < 0 ? -32767 : 32767)) / 65534, 0, mx);
}
static int32_t lfo_move(uint32_t t, uint32_t dest, int32_t v, int32_t mx)   /* track t's v moved by its LFOs aimed at dest */
{
    return lfo_mv1(t, 1u, dest, lfo_mv1(t, 0u, dest, v, mx), mx);
}
static uint32_t lfo_inc(uint32_t t, uint32_t i) /* a control step's phase */
{
    int32_t r = clamp(lf_get(t, i, 0u), 0, 127);
    if (lf_sync[t][i]) {                        /* SYNC: a cycle a note value: (bpm / 60) / (beats) a second */
        uint32_t b = LS_DIV[clamp(r, 0, NLSDIV - 1)];   /* (1/24 beats) */
        /* bpm10 192 2^32 / (b 600 11025) = (bpm10 32 2^32 / 1102500) / b: the divisors 32-bit (the chip divides
         * 64 by 32 only: no __udivdi3) */
        return (uint32_t)((((uint64_t)lfo_bpm10 * 32u) << 32) / 1102500u) / b;   /* (a control step:
                                                 * 8 / 11025 s at 44.1 kHz, CTL 32) */
    }
    r = lfo_mv1(t, i ^ 1u, LD_ORATE, r, 127);   /* (the other LFO on its RATE) */
    return (uint32_t)(((uint64_t)51942u * lfo_hz_q16(r)) >> 16);   /* (1/60 Hz: 51942 a control step) */
}

static void lfo_ctl(void)                       /* audio, a control step: each track's LFOs on */
{
    uint32_t t, i, ph, nt = lfo_note;
    int32_t d, x, a;
    if (nt) lfo_note = 0;
    for (t = 0; t < NT; t++) {
        lfo_p16[t] = 0;
        for (i = 0; i < 2u; i++) {
            uint32_t m = (uint32_t)clamp(lf_mode[t][i], 0, NLFOMODE - 1), sh = (uint32_t)clamp(lf_get(t, i, 3u), 0, NLFOSHAPE - 1);
            if (nt >> t & 1u) {                 /* a note: TRIG, ONCE, HALF restart at START; HOLD holds; FADE from 0 */
                if (m == LM_TRIG || m == LM_ONCE || m == LM_HALF)
                    lfo_ph[t][i] = (uint32_t)clamp(lf_start[t][i], 0, 127) * 33818640u, lfo_done[t][i] = 0;   /* (2^32 / 127) */
                if (m == LM_HOLD) lfo_hv[t][i] = lfo_shape(sh, lfo_ph[t][i], lfo_a[t][i], lfo_b[t][i]);
                lfo_fd[t][i] = 0;
            }
            ph = lfo_ph[t][i];
            if (!lfo_done[t][i]) {
                uint32_t nph = ph + lfo_inc(t, i), st = (uint32_t)clamp(lf_start[t][i], 0, 127) * 33818640u;
                if ((m == LM_ONCE && nph - st < ph - st && ph != st) || (m == LM_HALF && nph - st >= 0x80000000u && ph - st < 0x80000000u))
                    lfo_done[t][i] = 1, nph = m == LM_HALF ? st + 0x7FFFFFFFu : st - 1u;   /* (it stays at its end) */
                lfo_ph[t][i] = nph;
                if (nph < ph && !lfo_done[t][i]) {   /* a new cycle: RANDOM's value, DRIFT's next */
                    lfo_a[t][i] = sh == LS_DRIFT ? lfo_b[t][i] : lfo_rand();
                    lfo_b[t][i] = lfo_rand();
                }
            }
            if (m != LM_ONCE && m != LM_HALF) lfo_done[t][i] = 0;   /* (FREE, TRIG, HOLD: they run on) */
            d = clamp(lfo_mv1(t, i ^ 1u, LD_ODEPTH, clamp(lf_get(t, i, 1u), 0, 100), 100), 0, 100);
            a = lfo_amt[t][i];
            if (!d && !a) continue;
            x = d ? (m == LM_HOLD ? lfo_hv[t][i] : lfo_shape(sh, lfo_ph[t][i], lfo_a[t][i], lfo_b[t][i])) * d / 100 : 0;
            if (lf_fade[t][i] > 0 && lfo_fd[t][i] < 32767) {   /* FADE: in after a note, ~0 .. 10 s */
                lfo_fd[t][i] += 32767 / (1 + clamp(lf_fade[t][i], 0, 127) * 108);
                if (lfo_fd[t][i] > 32767) lfo_fd[t][i] = 32767;
                x = (int32_t)(((int64_t)x * lfo_fd[t][i]) >> 15);
            }
            a += ((x - a) * LFO_SMOOTH) >> 15;
            if (!d && a > -64 && a < 64) a = 0; /* (DEPTH 0: out, then off) */
            lfo_amt[t][i] = a;
            if (lf_get(t, i, 2u) == LD_PITCH) lfo_p16[t] += a * 192 / 32767;
        }
    }
}
/* voice v of track t (engine e): its four as the engine gets them (the step playing's locks in place of the knobs, one
 * moved by each LFO, all moved by DRONE (drone.c's dr_k, the track's source's) and WARP: a copy; else the track's) */
static const volatile int32_t *lfo_params(uint32_t v, uint32_t t, uint32_t e)
{
    int32_t k0 = lf_get(t, 0u, 2u) - LD_K1, k1 = lf_get(t, 1u, 2u) - LD_K1, i;
    int l0 = lfo_amt[t][0] && k0 >= 0 && k0 <= 3, l1 = lfo_amt[t][1] && k1 >= 0 && k1 <= 3;
    uint32_t lm = lk_m[t], dr = (dr_any[t] || ch_any[t]) && (int32_t)e == trk[t].eng;
    if (!l0 && !l1 && !lm && !dr) return trk[t].p[e];
    if (v >= NV) v = NV - 1u;
    for (i = 0; i < 4; i++) {
        lfo_pm[v][i] = lm >> i & 1u ? clamp(lk_v[t][i], 0, ENGINES[e]->max[i]) : trk[t].p[e][i];
        if (dr) lfo_pm[v][i] = clamp(lfo_pm[v][i] + dr_k[t][i] + ch_k[t][i], 0, ENGINES[e]->max[i]);
    }
    for (i = 0; i < 4; i++) {
        int32_t mx = ENGINES[e]->max[i];
        lfo_pm[v][i] = lfo_mv1(t, 1u, LD_K1 + (uint32_t)i, lfo_mv1(t, 0u, LD_K1 + (uint32_t)i, lfo_pm[v][i], mx), mx);
    }
    return lfo_pm[v];
}
