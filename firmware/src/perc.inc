/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM
 * (Arctic Scream 1.4.8) four more drums, integer ports in FNTSMG-1's way (fg/), drum.c's models after its five:
 *   FM DRUM   after Mutable Instruments Peaks' FmDrum (peaks/drums/fm_drum.cc): a sine whose pitch falls from an FM
 *             envelope, a fast "aux" chirp at the hit, its own output fed back into its pitch, noise and overdrive
 *   BELL      after Mutable Instruments Braids' STRUCK BELL and STRUCK DRUM (braids/digital_oscillator.cc): 11 (bell)
 *             or 6 (drum) sine partials at Braids' ratios, each with its own decay; the drum's noise ring-modulated by
 *             its partials; rendered at half the rate, as Braids does
 *   PARTICLE  after Mutable Instruments Plaits' PARTICLE engine (plaits/dsp/noise/particle.h): random impulses, each
 *             ringing a band-pass filter at a random pitch around TONE; here three particles, and a hit is a burst
 *   WEIRD     after WeirdDrums by Daniele Filaretti (github.com/dfilaretti/WeirdDrums): a sine with a pitch envelope in
 *             Hz and an LFO on its pitch (up to 80 Hz: a wobble, a growl), and filtered noise mixed in
 *
 * Peaks, Braids, Plaits: Copyright 2013-2018 Emilie Gillet; WeirdDrums: Copyright (c) 2019 Daniele Filaretti. Both
 * under the MIT license:
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 *   documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 *   the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright
 *   notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 *   WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 *   COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 *   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Each takes eight values 0..127 in its knobs' order (SOUND's three, DRUM 1's four, DRUM 2's first) and a velocity,
 * renders into out[] (+=) as FNTSMG-1's do, and keeps on, kill (drum_run.c: dm_kill, a fast fade). No division by a
 * 64-bit number anywhere (the chip divides 64 by 32 only). */

/* an exponent in Q16 octaves, -6 .. +8: 2^x, Q16 */
static uint32_t pc_exp2(int32_t x)
{
    x = x < -6 * 65536 ? -6 * 65536 : x > 8 * 65536 ? 8 * 65536 : x;
    return fg_exp2_q16((uint32_t)(x + 6 * 65536)) >> 6;
}
/* a frequency in Hz (Q8) from octaves above 1 Hz (Q16): -8 .. 14 (1/256 Hz .. 16 kHz) */
static uint32_t pc_hz_q8(int32_t oct)
{
    oct = clamp(oct, -8 * 65536, 14 * 65536);
    return oct >= 0 ? fg_exp2_q16((uint32_t)oct) >> 8 : fg_exp2_q16((uint32_t)(oct + 8 * 65536)) >> 16;
}
/* a phase increment, capped below a quarter of the rate's */
static uint32_t pc_inc(uint64_t f_q8) { return f_q8 > (11000u << 8) ? fg_inc(11000u << 8) : fg_inc((uint32_t)f_q8); }
/* an increment scaled by a Q16 factor, capped */
static uint32_t pc_scale(uint32_t inc, uint32_t f)
{
    uint64_t r = ((uint64_t)inc * f) >> 16;
    return r > 0x3FFFFFFFu ? 0x3FFFFFFFu : (uint32_t)r;
}

/* ---------------------------------------------------------------- FM DRUM (Peaks) */
typedef struct {
    uint8_t on, cnt;
    uint32_t ph, inc0, inc;
    int32_t amp, ka, kill;                      /* AM: Q30 */
    int32_t fm, kfm4, aux, kaux4;               /* the FM and aux envelopes, Q30, stepped every 4 samples */
    int32_t fm_amt, punch, fb;                  /* their depths, Q16 octaves (fb: at full scale) */
    int32_t noise, drive, dmix, lvl, prev;
    uint32_t seed;
} pc_fmd_t;
static void fmd_trig(pc_fmd_t *v, const uint8_t *p, unsigned vel)
{
    /* TONE: 30 Hz .. 1.2 kHz (Peaks: MIDI 24 .. 96) */
    v->inc0 = v->inc = pc_inc(pc_hz_q8((int32_t)(4.907 * 65536) + (int32_t)p[0] * 5 * 65536 / 127));
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->ka = fg_decay(p[1], 52, 121);            /* Peaks: am_decay = 16384 + decay / 2 */
    v->kill = 0;
    v->fm = FG_ONE30, v->kfm4 = fg_mul30(fg_mul30(fg_decay(p[1], 30, 104), fg_decay(p[1], 30, 104)), fg_mul30(fg_decay(p[1], 30, 104), fg_decay(p[1], 30, 104)));
    v->aux = FG_ONE30, v->kaux4 = fg_mul30(fg_mul30(FG_DECAY[28], FG_DECAY[28]), fg_mul30(FG_DECAY[28], FG_DECAY[28]));   /* ~20 ms, as Peaks' */
    v->fm_amt = (int32_t)p[3] * 6 * 65536 / 127;   /* FM: up to 6 octaves above */
    v->noise = (int32_t)(p[4] * p[4] * 2u) * 6 / 10;   /* NOISE: up to 60 % (Peaks: up to ~62 %) */
    v->drive = 4096 + (int32_t)p[5] * 4096 * 7 / 127, v->dmix = (int32_t)(p[5] * p[5] * 2u);   /* DRIVE */
    v->fb = (int32_t)p[6] * 65536 * 2 / 3 / 127;   /* FEEDBACK: the output on its own pitch, +-2/3 octave at full scale */
    v->punch = (int32_t)p[7] * 65536 * 4 / 3 / 127;   /* PUNCH: the aux chirp, up to 16 semitones */
    v->lvl = dm_sq2(p[2]);
    v->ph = (uint32_t)(0x3FFF * (uint32_t)p[3] / 127u) << 16;   /* (Peaks: the phase starts with the FM amount) */
    v->prev = 0, v->cnt = 0, v->on = 1;
}
static void fmd_render(pc_fmd_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 3u) == 0) {
            int32_t oct = (int32_t)(((int64_t)v->fm * v->fm_amt) >> 30) +
                          (int32_t)(((int64_t)v->aux * v->punch) >> 30) + (int32_t)(((int64_t)v->prev * v->fb) >> 15);
            v->inc = pc_scale(v->inc0, pc_exp2(oct));
            v->fm = fg_mul30(v->fm, v->kfm4), v->aux = fg_mul30(v->aux, v->kaux4);
        }
        v->ph += v->inc;
        int32_t x = fg_sin(v->ph);
        if (v->noise) x += fg_mul15(fg_noise(&v->seed) - x, v->noise);
        x = fg_mul30(x, v->amp);
        if (v->dmix) x += fg_mul15(fg_soft((int32_t)(((int64_t)x * v->drive) >> 12)) - x, v->dmix);
        v->prev = x;
        out[i] += fg_mul15(x, v->lvl) * 2;
        v->amp = fg_mul30(v->amp, v->kill ? v->kill : v->ka);
        if (v->amp < 2000) v->on = 0;
    }
}

/* ---------------------------------------------------------------- BELL (Braids) */
#define PC_NP 11u
static const int16_t PC_BELL_P[PC_NP] = {-1284, -1283, -184, -183, 385, 1175, 1536, 2233, 2434, 2934, 3110};   /* 1/128 semitone */
static const uint16_t PC_BELL_A[PC_NP] = {8192, 5488, 8192, 14745, 21872, 13680, 11960, 10895, 10895, 6144, 10895};
static const uint16_t PC_BELL_DL[PC_NP] = {65533, 65533, 65533, 65532, 65531, 65531, 65530, 65529, 65527, 65523, 65519};
static const uint16_t PC_BELL_DS[PC_NP] = {65308, 65283, 65186, 65123, 64839, 64889, 64632, 64409, 64038, 63302, 62575};
static const int16_t PC_DRUM_P[6] = {0, 0, 1041, 1747, 1846, 3072};
static const uint16_t PC_DRUM_A[6] = {16986, 2654, 3981, 5308, 3981, 2985};
static const uint16_t PC_DRUM_DL[6] = {65533, 65531, 65531, 65531, 65531, 65516};
static const uint16_t PC_DRUM_DS[6] = {65083, 64715, 64715, 64715, 64715, 62312};
typedef struct {
    uint8_t on, type, cnt, np;
    uint32_t ph[PC_NP], inc[PC_NP];
    uint16_t a[PC_NP], k[PC_NP];                /* a partial's amplitude (Braids': Q16) and its decay every 16 samples */
    int32_t kill, lvl, last, cur;               /* (kill: Q30, the fade) */
    int32_t lp0, lp1, lp2, f;                   /* (the drum's noise, three one-poles) */
    int16_t hg, ng, click, kc;                  /* (the drum's harmonics and noise modes; the strike's click) */
    uint32_t seed;
} pc_bell_t;
static void bell_trig(pc_bell_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t i, drum = p[2] >= 1u;              /* TYPE: 0 BELL, 1 DRUM */
    const int16_t *P = drum ? PC_DRUM_P : PC_BELL_P;
    const uint16_t *A = drum ? PC_DRUM_A : PC_BELL_A, *DL = drum ? PC_DRUM_DL : PC_BELL_DL, *DS = drum ? PC_DRUM_DS : PC_BELL_DS;
    /* TONE: the bell's 55 Hz .. 1.76 kHz (its first partials ten semitones below), the drum's 40 Hz .. 1.28 kHz */
    int32_t base = (int32_t)((drum ? 5.322 : 5.781) * 65536) + (int32_t)p[0] * 5 * 65536 / 127;
    int32_t bal = (127 - (int32_t)p[1]) * (127 - (int32_t)p[1]) >> 7;   /* DECAY: Braids' balance, long .. short */
    int32_t stretch = 65536 + ((int32_t)p[4] - 64) * 65536 / 640;   /* COLOR on a bell: its partials +-10 % apart */
    v->type = (uint8_t)drum, v->np = (uint8_t)(drum ? 6u : PC_NP);
    for (i = 0; i < v->np; i++) {
        int32_t pt = P[i];
        if (!drum) {
            pt = (int32_t)(((int64_t)pt * stretch) >> 16);
            pt += (i & 1u ? 1 : -1) * (int32_t)p[3] * 2;   /* SPREAD: Braids' detune, +-2 semitones */
        }
        v->inc[i] = pc_inc(pc_hz_q8(base + pt * 65536 / 1536));   /* (rendered at half the rate: two of these a step) */
        v->ph[i] = 1u << 30;
        int32_t d = DL[i] - ((DL[i] - DS[i]) * bal >> 7);   /* (Braids: a block of 24 at 96 kHz; here 16 at 44.1 kHz: 1.45 x) */
        d = 65536 - (65536 - d) * 1451 / 1000;
        v->k[i] = (uint16_t)clamp(d, 0, 65535);
        /* STRIKE: a soft mallet keeps the upper partials down, a hard one up */
        int32_t tilt = 65536 - (int32_t)(127u - p[5]) * (int32_t)i * 65536 / (int32_t)(v->np * 160u);
        v->a[i] = (uint16_t)clamp((int32_t)(((int64_t)A[i] * tilt >> 16) * (int32_t)vel / 127), 0, 65535);
    }
    v->hg = (int16_t)(drum ? (p[4] < 64u ? 4096 + p[4] * 140 : 16384) : 0);   /* (the drum: COLOR its harmonics) */
    v->ng = (int16_t)(drum ? p[3] * 96 : 0);    /* (the drum: SPREAD its noise modes' balance) */
    {
        int32_t co = base + (int32_t)p[4] * 4 * 65536 / 127 + 2 * 65536;   /* (the drum's noise: cutoff above TONE) */
        v->f = dm_svf_f((uint32_t)clamp((int32_t)(pc_hz_q8(co) >> 8), 40, 7000));
    }
    v->click = (int16_t)(p[6] * p[6] * 2u * vel / 127u >> 1), v->kc = 0;   /* CLICK: a noise tick at the strike */
    v->lvl = dm_sq2(p[7]);
    v->kill = 0, v->cnt = 0, v->last = v->cur = 0, v->on = 1;
}
static int32_t bell_step(pc_bell_t *v)          /* a sample at half the rate */
{
    int32_t s = 0, i;
    if (!v->type) {
        for (i = 0; i < (int32_t)v->np; i++) v->ph[i] += v->inc[i] << 1, s += fg_sin(v->ph[i]) * v->a[i] >> 16;   /* (Braids' >> 17: 6 dB up, level with the others) */
    } else {
        int32_t nz = fg_noise(&v->seed) >> 1, pr[6], h = 0;
        v->lp0 += (nz - v->lp0) * v->f >> 15, v->lp1 += (v->lp0 - v->lp1) * v->f >> 15, v->lp2 += (v->lp1 - v->lp2) * v->f >> 15;
        for (i = 0; i < 6; i++) v->ph[i] += v->inc[i] << 1, pr[i] = fg_sin(v->ph[i]) * v->a[i] >> 16, h += pr[i];
        s = pr[0] + ((pr[1] * v->lp2 >> 8) * (12288 - v->ng) >> 14) + ((pr[3] * v->lp2 >> 9) * v->ng >> 14) + (h * v->hg >> 14);
    }
    if (v->click > 64) s += fg_mul15(fg_noise(&v->seed), v->click), v->click = (int16_t)(v->click * 15 >> 4);
    return fg_clamp16(s);
}
static void bell_render(pc_bell_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt & 1u) == 0) v->last = v->cur, v->cur = bell_step(v);
        int32_t x = v->cnt & 1u ? v->cur : (v->last + v->cur) >> 1;
        if ((++v->cnt & 15u) == 0) {
            uint32_t j, top = 0;
            for (j = 0; j < v->np; j++) {
                v->a[j] = (uint16_t)(v->kill ? v->a[j] * 54000u >> 16 : v->a[j] * (uint32_t)v->k[j] >> 16);
                top = v->a[j] > top ? v->a[j] : top;
            }
            if (top < 40u) v->on = 0;
        }
        out[i] += fg_mul15(x, v->lvl) * 2;
    }
}

/* ---------------------------------------------------------------- PARTICLE (Plaits) */
#define PC_NPART 3u
typedef struct {
    uint8_t on, first;
    int32_t lp[PC_NPART], bp[PC_NPART], f[PC_NPART], pre[PC_NPART];
    uint32_t thresh;                            /* an impulse's chance a sample, of 2^32 */
    int32_t amp, ka, kill, lvl;
    int32_t lf, spread, lq, d, grit, fade;      /* TONE in Q16 octaves above 1 Hz; SPREAD (Q16 octaves); Q (log2, Q16), its damping */
    uint32_t seed;
} pc_part_t;
static void part_new(pc_part_t *v, uint32_t k)  /* a particle's new pitch around TONE, and its gain (Plaits: 0.5 / sqrt(q f)) */
{
    int32_t u = (int32_t)(fg_rand(&v->seed) >> 16) - 32768;   /* -1 .. 1, Q15 */
    int32_t lf = clamp(v->lf + (int32_t)(((int64_t)u * v->spread) >> 15), (int32_t)(5.0 * 65536), (int32_t)(12.8 * 65536));   /* 32 Hz .. 7.1 kHz */
    v->f[k] = dm_svf_f(pc_hz_q8(lf) >> 8);
    /* log2 of the filter's f is about log2(Hz) - 12.78; the gain: 2^(-(that + log2 q) / 2 - 1) */
    v->pre[k] = (int32_t)pc_exp2(((int32_t)(12.78 * 65536) - lf - v->lq) / 2 - 65536);
}
static void part_trig(pc_part_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t k;
    v->lf = (int32_t)(5.322 * 65536) + (int32_t)p[0] * 15 * 65536 / 254;   /* TONE: 40 Hz .. 7.2 kHz */
    {   /* DENSITY: 2^-12 .. 2^-1.5 a sample (about ten a second .. fifteen thousand): 2^(e + 32) of 2^32 */
        int32_t e = -12 * 65536 + (int32_t)p[3] * 21 * 65536 / 254;
        v->thresh = pc_exp2(e + 6 * 65536) << 10;
    }
    v->spread = (int32_t)(p[4] * p[4]) * 4 * 65536 / 16129;   /* SPREAD: +-4 octaves (Plaits: 48 semitones x harmonics^2) */
    v->lq = (int32_t)p[5] * 6 * 65536 / 127;    /* RESO: Q 1 .. 64 (Plaits: up to ~1000) */
    v->d = (int32_t)(pc_exp2(-v->lq) >> 1) + 16;   /* (the damping, Q15: 1 / q) */
    v->grit = (int32_t)(p[6] * p[6] * 2u);      /* GRIT: the raw impulses mixed in (Plaits: its AUX) */
    v->fade = (int32_t)p[7] * 258;              /* FADE: the density falls with the sound */
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->ka = fg_decay(p[1], 40, 123);
    v->lvl = dm_sq2(p[2]);
    for (k = 0; k < PC_NPART; k++) v->lp[k] = v->bp[k] = 0;
    v->kill = 0, v->first = 1, v->on = 1;
}
static void part_render(pc_part_t *v, int32_t *out, unsigned n)
{
    uint32_t k;
    if (v->on && v->first) for (k = 0; k < PC_NPART; k++) part_new(v, k);
    for (unsigned i = 0; i < n && v->on; i++) {
        int32_t x = 0, raw = 0;
        uint32_t th = v->thresh;
        if (v->fade) th -= (uint32_t)(((uint64_t)th * (uint32_t)fg_mul15(32767 - (v->amp >> 15), v->fade)) >> 15);
        for (k = 0; k < PC_NPART; k++) {
            uint32_t u = fg_rand(&v->seed);
            int32_t s = 0;
            if (v->first || u < th) {
                if (!v->first) part_new(v, k);
                s = 20000 + (int32_t)(u >> 21);  /* (an impulse's level) */
                raw += s >> 2;
            }
            /* a band-pass (Chamberlin, Q15) */
            int32_t in = (int32_t)(((int64_t)s * v->pre[k]) >> 16);
            v->lp[k] += fg_mul15(v->f[k], v->bp[k]);
            int32_t hp = in - v->lp[k] - fg_mul15(v->d, v->bp[k]);
            v->bp[k] += fg_mul15(v->f[k], hp);
            v->bp[k] = clamp(v->bp[k], -98304, 98304);
            x += v->bp[k];
        }
        v->first = 0;
        x = fg_soft(x * 8 + fg_mul15(raw, v->grit));   /* (sparse rings: 18 dB up, softly held) */
        x = fg_mul30(x, v->amp);
        out[i] += fg_mul15(x, v->lvl) * 2;
        v->amp = fg_mul30(v->amp, v->kill ? v->kill : v->ka);
        if (v->amp < 2000) v->on = 0;
    }
}

/* ---------------------------------------------------------------- WEIRD (WeirdDrums) */
typedef struct {
    uint8_t on, cnt, hpm;
    uint32_t ph, inc, lph, linc;
    uint32_t base, sweep;                       /* Hz, Q8 */
    int32_t penv, kp16, wob;                    /* the pitch envelope (Q30), the LFO's depth (Q16 octaves) */
    int32_t amp, ka, namp, kn, kill, mix, lvl;
    int32_t lp, bp, f;
    uint32_t seed;
} pc_weird_t;
static void weird_trig(pc_weird_t *v, const uint8_t *p, unsigned vel)
{
    v->base = pc_hz_q8((int32_t)(4.907 * 65536) + (int32_t)p[0] * 6 * 65536 / 127);   /* TONE: 30 Hz .. 1.9 kHz */
    v->mix = (int32_t)p[2] * 258;               /* MIX: the sine .. the noise */
    v->sweep = (uint32_t)p[3] * p[3] * 2000u / 16129u << 8;   /* SWEEP: up to + 2 kHz (WeirdDrums: + 1 kHz x amount) */
    v->penv = FG_ONE30, v->kp16 = fg_pow16(fg_decay(p[3], 20, 80));
    v->wob = (int32_t)p[4] * 65536 * 3 / 2 / 127;   /* WOBBLE: the LFO, +-1.5 octaves */
    v->linc = fg_inc(pc_hz_q8(-(int32_t)(3.32 * 65536) + (int32_t)p[5] * (int32_t)(9.64 * 65536) / 127)) << 4;   /* RATE: 0.1 .. 80 Hz (16 samples a step) */
    v->lph = 0;
    v->hpm = p[6] >= 64u;                       /* NOISE: low-pass opening (0 .. 63), then high-pass closing (64 .. 127) */
    {
        int32_t o = v->hpm ? (int32_t)(p[6] - 64u) : (int32_t)p[6];
        v->f = dm_svf_f(pc_hz_q8((int32_t)(7.64 * 65536) + o * (int32_t)(4.9 * 65536) / 63) >> 8);   /* 200 Hz .. 6 kHz */
    }
    v->lp = v->bp = 0;
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->namp = v->amp;
    v->ka = fg_decay(p[1], 45, 121), v->kn = fg_decay(p[1], 30, 112);   /* DECAY: the sine's, the noise's shorter */
    v->lvl = dm_sq2(p[7]);
    v->ph = 0, v->cnt = 0, v->kill = 0, v->on = 1;
}
static void weird_render(pc_weird_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            uint64_t hz = v->base + (uint32_t)(((uint64_t)v->sweep * (uint32_t)v->penv) >> 30);
            int32_t l = (int32_t)(((int64_t)fg_sin(v->lph) * v->wob) >> 15);
            v->inc = pc_inc((hz * pc_exp2(l)) >> 16);
            v->penv = fg_mul30(v->penv, v->kp16);
            v->lph += v->linc;
        }
        v->ph += v->inc;
        int32_t s = fg_mul30(fg_sin(v->ph), v->amp), nz = fg_noise(&v->seed) >> 1, y;
        v->lp += fg_mul15(v->f, v->bp);
        int32_t hp = nz - v->lp - v->bp;      /* (Q 1: steady at 6 kHz) */
        v->bp += fg_mul15(v->f, hp);
        y = fg_mul30(fg_soft((v->hpm ? hp : v->lp) * 5), v->namp);   /* (the noise 14 dB up: level with the sine) */
        int32_t x = s + fg_mul15(y - s, v->mix);
        out[i] += fg_mul15(x, v->lvl) * 2;
        v->amp = fg_mul30(v->amp, v->kill ? v->kill : v->ka), v->namp = fg_mul30(v->namp, v->kill ? v->kill : v->kn);
        if (v->amp < 2000 && v->namp < 2000) v->on = 0;
    }
}
