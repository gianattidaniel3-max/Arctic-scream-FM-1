/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* SEL > SCALE's TUNING (global, with ROOT and SCALE): the octave in N equal steps. Every note number is one step: a key,
 * a MIDI note and a step of the sequencer all play note n at (n - anchor) x 1200 / N cents from the anchor, which is
 * ROOT in the third octave and stays where 12 EDO has it. 12 EDO: as ever, and nothing here runs.
 *   tn_p16:  a note's pitch in 1/16 semitone (VP16, every source's), the nearest
 *   tn_fine: what that rounding left (up to 3 cents), as tune_f; no floats, no state: the audio ISR calls these */
static volatile int32_t sc_root, sc_scale;      /* SCALE: ROOT (C .. B), SCALE (0: OFF) */

#define NTUNING 12
#define TN_12 3                                 /* TUNING's index of 12 EDO, the default */
static const uint8_t TN_EDO[NTUNING] = {5, 7, 10, 12, 14, 15, 16, 17, 19, 22, 24, 31};
static const char *const TN_NAME[NTUNING] = {"5 EDO", "7 EDO", "10 EDO", "12 EDO", "14 EDO", "15 EDO", "16 EDO", "17 EDO",
                                             "19 EDO", "22 EDO", "24 EDO", "31 EDO"};
static volatile int32_t sc_tuning = TN_12;

static inline int32_t tn_edo(void) { return TN_EDO[sc_tuning < 0 || sc_tuning >= NTUNING ? TN_12 : sc_tuning]; }
static inline int tn_on(void) { return tn_edo() != 12; }
static inline int32_t tn_anchor(void) { int32_t r = sc_root; return 48 + (r < 0 ? 0 : r > 11 ? 11 : r); }
/* note n from the anchor, in 1/4096 semitone (x 16 x 256: 1/256 of VP16's steps) */
static inline int32_t tn_x(uint32_t n, int32_t edo) { return ((int32_t)n - tn_anchor()) * (192 * 256) / edo; }
static inline int32_t tn_whole(int32_t x) { return (int32_t)(((uint32_t)(x + (1 << 28)) + 128u) >> 8) - (1 << 20); }
static inline int32_t tn_p16(uint32_t n)
{
    int32_t edo = tn_edo(), p;
    if (edo == 12) return (int32_t)n * 16;
    p = tn_anchor() * 16 + tn_whole(tn_x(n, edo));
    return p < 0 ? 0 : p > 127 * 16 ? 127 * 16 : p;
}
static inline int32_t tn_fine(uint32_t n)
{
    int32_t edo = tn_edo(), x;
    if (edo == 12) return 0;
    x = tn_x(n, edo);
    return ((x - tn_whole(x) * 256) * 3787) >> 16;   /* (1/256 of 6.25 cents x 2.367 / 4096 a cent, Q16) */
}
/* TUNING's saved form is the number of steps itself: 0 (an older project, the byte spare then) and anything else: 12 */
static int32_t tn_index(uint32_t edo)
{
    int32_t i;
    for (i = 0; i < NTUNING; i++)
        if (TN_EDO[i] == edo) return i;
    return TN_12;
}
