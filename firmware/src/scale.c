/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's SEL: the scale the keys snap to (global: ROOT, SCALE) and a track's one-key chord (CHORD TYPE, VOICING).
 *   sc_snap:     a note to the nearest one of the scale (a tie: the lower); SCALE OFF: as it is
 *   chord_notes: the notes a chord on a note is (its CHORD TYPE, then its VOICING). SCALE: the triad of the scale on the
 *                note (the scale's 1st, 3rd and 5th from it: in key); SCALE OFF: MIN
 *   chord_name:  "Cm7", "D#dim", a scale's odd triad by its notes ("C-F-A#")
 * The main loop and the audio ISR (note_on) both call these: no state of their own. */

enum { CH_SCALE, CH_MIN, CH_MAJ, CH_SUS2, CH_SUS4, CH_FIFTH, CH_MIN7, CH_MAJ7, CH_DIM, CH_ADD9, CH_CLUSTER, CH_SCALE7, CH_QUART3,
       CH_QUART4, CH_QUINT, CH_HARM, NCHORD };   /* (16 at most: a chord step's byte has 4 bits for it) */
enum { VC_CLOSE, VC_OPEN, VC_SPREAD, VC_OCT, VC_DROP, NVOICING };
#define NSCALE 15
#define CH_MAX 5u                               /* a chord's notes at most (4, and +OCT's) */
static const uint16_t SC_MASK[NSCALE] = {       /* bit i: i semitones over ROOT in the scale */
    0xFFF, 0x5AD, 0xAB5, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B, 0x9AD, 0x5B3, 0x9CD, 0x9B3, 0x4A9, 0x295,
    0x663};                                     /* HORROR: Audrey Touch's eerie pad scale (0 1 5 6 9 10) */
static const char *const SC_NAME[NSCALE] = {"CHROMATIC", "NATURAL MINOR", "MAJOR", "DORIAN", "PHRYGIAN", "LYDIAN",
    "MIXOLYDIAN", "LOCRIAN", "HARMONIC MINOR", "PHRYGIAN DOMINANT", "HUNGARIAN MINOR", "DOUBLE HARMONIC",
    "MINOR PENTATONIC", "MAJOR PENTATONIC", "HORROR"};
static const char *const SC_SHORT[NSCALE] = {"OFF", "MINOR", "MAJOR", "DORIAN", "PHRYG", "LYDIAN", "MIXO", "LOCRIA",
                                             "HARMIN", "PHRDOM", "HUNGMI", "DBLHAR", "MINPEN", "MAJPEN",
                                             "HORROR"};   /* (slabs) */
/* a TUNING's own scales: bit i, i steps over ROOT. 19 EDO: the generated scales of that tuning (generator, notes:
 * Kleismic 5 steps, 7; Negri 2, 9; Semaphore 4, 9; Magic 6, 7; Sensi 7, 8). 24 EDO: maqamat, and a normal
 * scale with one or two whole tones filled with quarter tones: ZONE, mixolydian's 2-3 and 7-8; MAJZON, major's 2-3, 6-7 */
#define NNATIVE 11
static const struct { uint8_t edo; uint32_t mask;   /* (in decimal: tools/build.py takes long hex for a register) */
    const char *name, *slab; } SC_NATIVE[NNATIVE] = {
    {19, 35939u, "KLEISMIC", "KLEISM"}, {19, 87381u, "NEGRI", "NEGRI"}, {19, 78643u, "SEMAPHORE", "SEMAPH"},
    {19, 399457u, "MAGIC", "MAGIC"}, {19, 84629u, "SENSI", "SENSI"},
    {24, 2376849u, "RAST", "RAST"}, {24, 1131593u, "BAYATI", "BAYATI"}, {24, 1118537u, "SABA", "SABA"},
    {24, 2245769u, "SIKAH", "SIKAH"}, {24, 16008689u, "MIXO ZONES", "ZONE"}, {24, 8144369u, "MAJOR ZONES", "MAJZON"}};
static uint32_t sc_native_n(void)               /* how many the TUNING has */
{
    uint32_t i, n = 0;
    for (i = 0; i < NNATIVE; i++) n += SC_NATIVE[i].edo == (uint32_t)tn_edo();
    return n;
}
static int32_t sc_native_at(int32_t k)          /* the TUNING's k-th: its row (-1: none) */
{
    int32_t i;
    for (i = 0; i < NNATIVE; i++)
        if (SC_NATIVE[i].edo == (uint32_t)tn_edo() && k-- == 0) return i;
    return -1;
}
static inline int32_t sc_now(void)              /* SCALE as it counts: one the TUNING lacks: OFF */
{
    int32_t v = sc_scale;
    return v < 0 || v >= NSCALE + (int32_t)sc_native_n() ? 0 : v;
}
static const char *sc_name(int32_t v) { return v < NSCALE ? SC_NAME[v < 0 ? 0 : v] : SC_NATIVE[sc_native_at(v - NSCALE)].name; }
static const char *sc_slab(int32_t v) { return v < NSCALE ? SC_SHORT[v < 0 ? 0 : v] : SC_NATIVE[sc_native_at(v - NSCALE)].slab; }
static const char *const SC_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const CH_SHORT[NCHORD] = {"SCALE", "MIN", "MAJ", "SUS2", "SUS4", "FIFTH", "MIN7", "MAJ7", "DIM",
                                             "ADD9", "CLUSTR", "SCALE7", "QUART3", "QUART4", "QUINT", "HARM"};
static const char *const CH_SUF[NCHORD] = {"m", "m", "", "sus2", "sus4", "5", "m7", "maj7", "dim", "add9", "clus", "m7", "q3", "q4", "q5", "h"};
static const uint8_t CH_IV[NCHORD][5] = {       /* its notes' count, then their semitones over the note */
    {3, 0, 3, 7}, {3, 0, 3, 7}, {3, 0, 4, 7}, {3, 0, 2, 7}, {3, 0, 5, 7}, {2, 0, 7}, {4, 0, 3, 7, 10}, {4, 0, 4, 7, 11},
    {3, 0, 3, 6}, {4, 0, 4, 7, 14}, {3, 0, 2, 3},
    {4, 0, 3, 7, 10}, {3, 0, 5, 10}, {4, 0, 5, 10, 15}, {3, 0, 7, 14}, {4, 0, 4, 7, 10}};   /* (SCALE7 without a scale: MIN7;
                                                 * HARM: harmonics 4:5:6:7, by CH_HARM_C at a TUNING) */
static const int16_t CH_HARM_C[4] = {0, 386, 702, 969};   /* its cents */
static const char *const VC_NAME[NVOICING] = {"CLOSE", "OPEN", "SPREAD", "+OCT", "DROP"};

static int sc_in(int32_t n)                     /* note n (from 0) in the scale (a TUNING but 12 EDO: the scales are of
                                                 * twelve notes, every step is in) */
{
    int32_t edo = tn_edo(), v = sc_now();
    if (edo != 12) {
        int32_t d, i;
        uint32_t mk;
        if (!v) return 1;
        d = (n - tn_anchor()) % edo;
        if (d < 0) d += edo;
        if (v >= NSCALE) return SC_NATIVE[sc_native_at(v - NSCALE)].mask >> d & 1u;
        for (mk = SC_MASK[v], i = 0; i < 12; i++)   /* (the twelve-note scale: each note's nearest step) */
            if ((mk >> i & 1u) && i * edo + 6 >= d * 12 && i * edo + 6 < (d + 1) * 12) return 1;
        return 0;
    }
    return SC_MASK[v] >> ((uint32_t)(n + 132 - clamp(sc_root, 0, 11)) % 12u) & 1u;
}
static uint32_t sc_snap(uint32_t n)
{
    int32_t d, m = (int32_t)n;
    if (sc_now() <= 0) return n;
    for (d = 0; d < tn_edo(); d++) {
        if (m - d >= 0 && sc_in(m - d)) return (uint32_t)(m - d);
        if (m + d <= 127 && sc_in(m + d)) return (uint32_t)(m + d);
    }
    return n;
}
static int32_t sc_up(int32_t n, uint32_t k)     /* the k-th note of the scale over n */
{
    for (; k; k--)
        do n++; while (!sc_in(n));
    return n;
}
static int32_t sc_walk(int32_t n, int32_t d)    /* GRID's NOTE d notes of the scale on (0 OFF; under the lowest: OFF) */
{
    int32_t s = d > 0 ? 1 : -1, m;
    if (!n) return d > 0 ? (int32_t)sc_snap(60u) : 0;
    for (; d; d -= s) {
        for (m = n + s; m >= 1 && m <= 127 && !sc_in(m); m += s)
            ;
        if (m < 1) return 0;
        if (m > 127) break;
        n = m;
    }
    return n;
}
static uint32_t chord_notes_tv(int32_t ty, int32_t vo, uint32_t note, uint8_t *out)   /* CHORD TYPE ty, VOICING vo on
                                                 * note: its notes (0..127, at most CH_MAX) */
{
    int32_t n[CH_MAX], c, i, r = (int32_t)(note & 127u), oct = tn_edo();
    ty = clamp(ty, 0, NCHORD - 1);
    uint32_t k = 0;
    if ((ty == CH_SCALE || ty == CH_SCALE7) && sc_now() > 0) {   /* the scale's triad on it, SCALE7 its seventh too (a note off
                                                 * the scale: as its nearest's) */
        int32_t s = (int32_t)sc_snap(note);
        n[0] = r, n[1] = r + sc_up(s, 2) - s, n[2] = r + sc_up(s, 4) - s, c = 3;
        if (ty == CH_SCALE7) n[3] = r + sc_up(s, 6) - s, c = 4;
    } else
        for (c = 0; c < CH_IV[ty][0]; c++)      /* (a TUNING: each interval's nearest step) */
            n[c] = r + (ty == CH_HARM && oct != 12 ? (CH_HARM_C[c] * oct + 600) / 1200 : (CH_IV[ty][1 + c] * oct + 6) / 12);
    switch (clamp(vo, 0, NVOICING - 1)) {
    case VC_OPEN: if (c >= 3) n[1] += oct; break;   /* the middle note up an octave */
    case VC_SPREAD: n[0] -= oct; for (i = 1; i < c; i++) n[i] += oct; break;   /* the root down, the rest up */
    case VC_OCT: n[c++] = r + oct; break;        /* close, and the root an octave up */
    case VC_DROP: n[0] -= oct; break;            /* the root down an octave */
    default: break;
    }
    for (i = 0; i < c; i++) {                   /* out of 0..127 (DROP, SPREAD at the ends): octaves in */
        uint32_t j;
        while (n[i] < 0) n[i] += oct;
        while (n[i] > 127) n[i] -= oct;
        for (j = 0; j < k && out[j] != (uint8_t)n[i]; j++)   /* (few steps: two intervals may meet on one) */
            ;
        if (oct == 12 || j == k) out[k++] = (uint8_t)n[i];
    }
    return k;
}
static uint32_t chord_notes(uint32_t t, uint32_t note, uint8_t *out)   /* track t's chord on note */
{
    return chord_notes_tv(trk[t].ch_type, trk[t].ch_voice, note, out);
}
static uint32_t chord_step(uint32_t t)          /* a chord step's byte: track t's chord now (0x80, VOICING << 4, TYPE) */
{
    return 0x80u | (uint32_t)clamp(trk[t].ch_voice, 0, NVOICING - 1) << 4 | (uint32_t)clamp(trk[t].ch_type, 0, NCHORD - 1);
}
static char *tn_name(char *s, uint32_t n)       /* a TUNING's note: its octave (the anchor's is 3), a dot, its step from
                                                 * ROOT: "3.7" */
{
    int32_t edo = tn_edo(), d = (int32_t)n - tn_anchor(), o = d >= 0 ? d / edo : -((edo - 1 - d) / edo);
    d -= o * edo, o += 3;
    if (o < 0) *s++ = '-', o = -o;
    return put_u(put_s(put_u(s, (uint32_t)o), "."), (uint32_t)d);
}
static char *chord_name_tv(int32_t ty, uint32_t note, char *s)   /* CHORD TYPE ty's name on note: "Cm7" */
{
    static const uint8_t TRI[6][2] = {{3, 7}, {4, 7}, {3, 6}, {4, 8}, {2, 7}, {5, 7}};
    static const char *const TRI_SUF[6] = {"m", "", "dim", "aug", "sus2", "sus4"};
    int32_t i;
    ty = clamp(ty, 0, NCHORD - 1);
    if (tn_on()) return put_s(tn_name(s, note), (ty == CH_SCALE || ty == CH_SCALE7) && sc_now() > 0 ? (ty == CH_SCALE ? "tri" : "7th") : CH_SUF[ty]);
    s = put_s(s, SC_NOTE[note % 12u]);
    if ((ty == CH_SCALE || ty == CH_SCALE7) && sc_now() > 0) {
        int32_t r = (int32_t)sc_snap(note), a = sc_up(r, 2) - r, b = sc_up(r, 4) - r, g = sc_up(r, 6) - r;
        for (i = 0; i < 6; i++)
            if (TRI[i][0] == a && TRI[i][1] == b) {
                s = put_s(s, ty == CH_SCALE7 && g == 11 && i == 1 ? "maj" : TRI_SUF[i]);
                return ty == CH_SCALE7 ? put_s(s, "7") : s;
            }
        s = put_s(put_s(s, "-"), SC_NOTE[(note + (uint32_t)a) % 12u]);   /* (a triad without a name: its notes) */
        return put_s(put_s(s, "-"), SC_NOTE[(note + (uint32_t)b) % 12u]);
    }
    return put_s(s, CH_SUF[ty]);
}
static char *chord_name(uint32_t t, uint32_t note, char *s) { return chord_name_tv(trk[t].ch_type, note, s); }
