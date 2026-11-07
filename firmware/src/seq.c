/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's sequencer: a sequence a track (T1..T4), all four played together on one tempo, each with its own STEPS,
 * DIVISION, SWING and PLAY MODE.
 *   a step: OFF (NOTE 0) or a note, with CHANCE (0..100 %: it plays or not, each time), RATCHET (1..4 hits within the
 *   step) and LENGTH (in steps: longer than one, the note ties over the next ones)
 *   the clock: 96 ticks a quarter note (every DIVISION, triplets too, a whole number of ticks), in the audio ISR a
 *   control block (32 samples) at a time: a note starts and stops within 0.73 ms of its time
 *   SWING: every other step later, by up to half a step
 *   PLAY MODE: FORWARD, BACKWARD, PINGPONG, RANDOM
 * Sequenced notes play through the track's source and ENV, as the keys' notes (grimm.c note_on, note_off: SRC_SEQ);
 * LATCH does not touch them. A track keeps SEQ_NPAT patterns (one now: song mode adds more). Nothing is saved yet.
 * The main loop edits the steps and the settings, starts and stops it (NQ_PLAY, NQ_STOP); the audio ISR plays it. */

#define SEQ_N 64u                               /* steps a pattern */
#define SEQ_NPAT 1u                             /* patterns a track (song mode: more, later) */
#define SEQ_NOFF 8u                             /* a track's sequenced notes sounding at once */
#define SEQ_TICK_Q16 1806336000u                /* a tick at 1 BPM, in samples Q16: 60 x 44100 / 96 x 65536 */
enum { SEQ_FWD, SEQ_BWD, SEQ_PONG, SEQ_RND, SEQ_NMODE };

typedef struct {                                /* (bytes: each value fits one; V1.0's were 32 bits, the saves the same) */
    volatile uint8_t note, chance, ratchet, len;   /* note 0: OFF, else 1..127; CHANCE 0..100; RATCHET 1..4; LENGTH 1..64 */
    volatile uint8_t chord;                     /* a chord step: 0x80 | VOICING << 4 | CHORD TYPE (recorded in chord
                                                 * mode: its chord, chord mode on or not); 0: a note */
} seq_step_t;                                   /* (its locks: the track's lock list, lk_e) */
_Static_assert(sizeof(seq_step_t) == 5u, "a step: 5 bytes");
typedef struct { seq_step_t st[SEQ_N]; } seq_pat_t;
typedef struct {
    volatile int32_t steps, div, swing, mode;   /* PATTERN SETTINGS: 1..64, SEQ_DIV_T's, 0..127, SEQ_ */
    volatile int32_t cursor;                    /* the selected step (SELECT) */
    seq_pat_t pat[SEQ_NPAT];
    volatile int32_t cur;                       /* the pattern playing */
    volatile int32_t pos;                       /* audio: the step playing (-1: stopped) */
    volatile uint32_t t;                        /* audio: ticks into it */
    int32_t dir;                                /* audio: PINGPONG's way */
    uint8_t off_note[SEQ_NOFF];                 /* audio: the notes sounding, */
    uint32_t off_left[SEQ_NOFF];                /* .. the ticks to their end (0: a free place) */
    uint32_t r_note, r_left, r_next, r_sub, r_gate, r_last, r_chord;   /* audio: the step's hits to come (RATCHET) */
} seq_trk_t;
static seq_trk_t seq[NT];
static volatile int32_t seq_bpm = 120;          /* the tempo, 20..300 (SELECT on HOME) */
static volatile uint32_t seq_on;                /* audio: playing */
static uint32_t seq_acc, seq_rng = 0x1234567u;

/* DIVISION: ticks a step, and its name */
static const uint8_t SEQ_DIV_T[8] = {96, 64, 48, 32, 24, 16, 12, 8};
static const char *const SEQ_DIV_NAME[8] = {"1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
static const char *const SEQ_MODE_NAME[SEQ_NMODE] = {"FORWARD", "BACKWARD", "PINGPONG", "RANDOM"};
static const char *const SEQ_MODE_SHORT[SEQ_NMODE] = {"FWD", "BWD", "PONG", "RND"};

#define SEQ_BAR 384u                            /* a bar: 4 quarter notes of 96 ticks */
static uint32_t seq_ticks;                      /* audio: ticks since PLAY (the bars) */
static volatile int32_t seq_recall = -1;        /* main loop: a slot to come in at the next bar (-1: none); audio: -1 */
static void slot_apply_isr(uint32_t s);         /* (store.c) */
enum { CK_INT, CK_OUT, CK_USB, CK_TRS, CK_N };  /* (clock.c: CLOCK, INT .. TRS) */
static volatile uint32_t ck_mode;               /* (clock.c) */
static void ck_out(uint32_t st, uint32_t rem);  /* (clock.c: CLOCK INT+OUT, a message out at its time: STOP's) */
static int slot_used(uint32_t s);
static void arp_tick(uint32_t t, int locked);   /* (arp.c: the arps on the sequencer's clock while it plays) */
static volatile uint32_t gen_on;                /* (V2) main loop: bit t: track t GENERATEs (gen.c): its pattern plays on
                                                 * unheard */
static void gen_tick(uint32_t t);               /* (gen.c: a tick of track t, from arp_tick) */

/* (Arctic Scream) parameter locks, Elektron's way: a track's list of (step, knob, value), any knob of the pages that
 * hold the track's sound and its FX (a knob: its page x 4 + its number; the SOUND page's four: 0..3), a step with or
 * without a note (a trigless lock). A step plays: the last step's locks give their knobs back, its own take theirs
 * (SOUND's four through lk_m, as a voice reads them; the others written, their values kept, till the next step) */
#define LK_N 96u                                /* locks a track (its pattern's), in all */
typedef struct { uint8_t st, pid, v; } lock_t;   /* (st 0xFF: a free place) */
static lock_t lk_e[NT][LK_N];
#define LK_ACT 8u                               /* a step's knobs written at once, at most */
static volatile int32_t *lk_ptr[NT][LK_ACT];    /* audio: the knobs a step's locks hold now, */
static int32_t lk_base[NT][LK_ACT];             /* .. their own values (given back) */
static uint32_t lk_na[NT];
static volatile int32_t *knob_param_t(uint32_t pg, uint32_t k, uint32_t t);   /* (grimm.c: a knob of track t's) */
static int32_t lk_find(uint32_t t, uint32_t st, uint32_t pid)   /* the lock of step st on knob pid: its place, -1 none */
{
    uint32_t i;
    for (i = 0; i < LK_N; i++)
        if (lk_e[t][i].st == st && lk_e[t][i].pid == pid) return (int32_t)i;
    return -1;
}
static int lk_set(uint32_t t, uint32_t st, uint32_t pid, int32_t v)   /* main loop: 0 ok, -1 full */
{
    int32_t i = lk_find(t, st, pid);
    uint32_t j;
    if (i < 0)
        for (j = 0; j < LK_N && i < 0; j++)
            if (lk_e[t][j].st == 0xFFu) i = (int32_t)j;
    if (i < 0) return -1;
    lk_e[t][i].pid = (uint8_t)pid, lk_e[t][i].v = (uint8_t)clamp(v, 0, 255), lk_e[t][i].st = (uint8_t)st;
    return 0;
}
static uint32_t lk_step_n(uint32_t t, uint32_t st)   /* step st's locks, how many */
{
    uint32_t i, n = 0;
    for (i = 0; i < LK_N; i++) n += lk_e[t][i].st == st;
    return n;
}
static void lk_clear_step(uint32_t t, uint32_t st)
{
    uint32_t i;
    for (i = 0; i < LK_N; i++)
        if (lk_e[t][i].st == st) lk_e[t][i].st = 0xFFu;
}
static void lk_clear(uint32_t t) { uint32_t i; for (i = 0; i < LK_N; i++) lk_e[t][i].st = 0xFFu; }
static void lk_restore(uint32_t t)              /* audio: the knobs a step's locks held, their own values again */
{
    while (lk_na[t]) {
        lk_na[t]--;
        *lk_ptr[t][lk_na[t]] = lk_base[t][lk_na[t]];
    }
    lk_m[t] = 0;
}
static void lk_apply(uint32_t t, uint32_t st)   /* audio: step st begins: its locks */
{
    uint32_t i, m = 0;
    lk_restore(t);
    for (i = 0; i < LK_N; i++) {
        const lock_t *L = &lk_e[t][i];
        if (L->st != st) continue;
        if (L->pid < 4u) {                      /* SOUND's: as a voice reads them (lfo.c lfo_params, drum_run.c) */
            m |= 1u << L->pid, lk_v[t][L->pid] = L->v;
        } else if (lk_na[t] < LK_ACT) {
            volatile int32_t *p = knob_param_t(L->pid >> 2, L->pid & 3u, t);
            if (!p) continue;
            lk_ptr[t][lk_na[t]] = p, lk_base[t][lk_na[t]] = *p, lk_na[t]++;
            *p = L->v;
        }
    }
    lk_m[t] = m;
}
static void lk_turned(volatile int32_t *p, int32_t v)   /* main loop: knob p turned to v while a lock holds it: its own
                                                 * value is v (given back so) */
{
    uint32_t t, i;
    for (t = 0; t < NT; t++)
        for (i = 0; i < lk_na[t]; i++)
            if (lk_ptr[t][i] == p) lk_base[t][i] = v;
}

static void seq_clear(void)                     /* main loop: the four sequences empty, 16 steps of 1/16, all OFF */
{
    uint32_t t, p, i;
    for (t = 0; t < NT; t++) lk_clear(t);
    for (t = 0; t < NT; t++) {
        seq[t].steps = 16, seq[t].div = 4, seq[t].swing = 0, seq[t].mode = SEQ_FWD;
        for (p = 0; p < SEQ_NPAT; p++)
            for (i = 0; i < SEQ_N; i++) {
                seq_step_t *s = &seq[t].pat[p].st[i];
                s->note = 0, s->chance = 100, s->ratchet = 1, s->len = 1;
                s->chord = 0;
            }
    }
}
static void seq_init(void)                      /* main loop, at boot */
{
    uint32_t t;
    seq_clear();
    for (t = 0; t < NT; t++) seq[t].pos = -1;
}
static uint32_t seq_rnd(void)
{
    uint32_t s = seq_rng;
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return seq_rng = s;
}
static int32_t seq_steps(const seq_trk_t *S) { return clamp(S->steps, 1, (int32_t)SEQ_N); }

/* audio: a hit of track t: the note on now (a chord step: its chord), off after gate ticks (a note still on from
 * before: again from here) */
static void seq_note(uint32_t t, uint32_t note, uint32_t gate, uint32_t chord)
{
    seq_trk_t *S = &seq[t];
    uint32_t i, f = SEQ_NOFF;
    for (i = 0; i < SEQ_NOFF; i++)
        if (S->off_left[i] && S->off_note[i] == note) {
            note_off(t, note, SRC_SEQ, 0);
            S->off_left[i] = 0;
        }
    for (i = 0; i < SEQ_NOFF && f == SEQ_NOFF; i++)
        if (!S->off_left[i]) f = i;
    if (f == SEQ_NOFF) {                        /* (all taken: the first goes) */
        f = 0;
        note_off(t, S->off_note[0], SRC_SEQ, 0);
    }
    note_on(t, note, note, SRC_SEQ, 0, chord ? (int32_t)chord : -1);   /* (its origin: the note; a note step: the
                                                 * track's chord mode) */
    S->off_note[f] = (uint8_t)note;
    S->off_left[f] = gate ? gate : 1u;
}
/* audio: the step playing starts (after its swing): CHANCE, then its first hit and the RATCHET's to come */
static void seq_fire(uint32_t t)
{
    seq_trk_t *S = &seq[t];
    const seq_step_t *s = &S->pat[clamp(S->cur, 0, SEQ_NPAT - 1)].st[S->pos];
    uint32_t note = (uint32_t)clamp(s->note, 0, 127), r, stl, sub, len, gate, last;
    if (!note || seq_rnd() % 100u >= (uint32_t)clamp(s->chance, 0, 100))
        return;
    r = (uint32_t)clamp(s->ratchet, 1, 4);
    stl = SEQ_DIV_T[clamp(S->div, 0, 7)];
    len = (uint32_t)clamp(s->len, 1, (int32_t)SEQ_N);
    sub = stl / r;
    gate = sub * 3u / 4u;                       /* a ratchet's hits: three quarters of theirs */
    last = len * stl - (r - 1u) * sub - 1u;     /* the last (or only) hit: to the note's end, a tick short */
    seq_note(t, note, r > 1u ? gate : last, (uint32_t)s->chord);
    S->r_left = r - 1u, S->r_note = note, S->r_next = sub, S->r_sub = sub, S->r_gate = gate, S->r_last = last;
    S->r_chord = (uint32_t)s->chord;
}
static void seq_next(seq_trk_t *S)              /* audio: the next step, by PLAY MODE */
{
    int32_t n = seq_steps(S), p = S->pos;
    switch (clamp(S->mode, 0, SEQ_NMODE - 1)) {
    case SEQ_BWD: p = p > 0 && p <= n ? p - 1 : n - 1; break;
    case SEQ_PONG:
        p += S->dir;
        if (p >= n) S->dir = -1, p = n > 1 ? n - 2 : 0;
        else if (p < 0) S->dir = 1, p = n > 1 ? 1 : 0;
        break;
    case SEQ_RND: p = (int32_t)(seq_rnd() % (uint32_t)n); break;
    default: p = p + 1 < n ? p + 1 : 0; break;
    }
    S->pos = p;
}
static void seq_track_tick(uint32_t t)          /* audio: a tick of track t */
{
    seq_trk_t *S = &seq[t];
    uint32_t stl = SEQ_DIV_T[clamp(S->div, 0, 7)], i, sw;
    if (S->pos >= seq_steps(S)) S->pos = 0;     /* (STEPS made shorter under it) */
    sw = (S->pos & 1) ? stl * (uint32_t)clamp(S->swing, 0, 127) / 254u : 0u;   /* SWING: up to half a step later */
    for (i = 0; i < SEQ_NOFF; i++)              /* the notes at their end (first: a new hit's gate counts from the
                                                 * next tick) */
        if (S->off_left[i] && !--S->off_left[i])
            note_off(t, S->off_note[i], SRC_SEQ, 0);
    if (S->r_left && !--S->r_next && !(gen_on >> t & 1u)) {   /* the RATCHET's next hit */
        S->r_left--;
        seq_note(t, S->r_note, S->r_left ? S->r_gate : S->r_last, S->r_chord);
        S->r_next = S->r_sub;
    }
    if (S->t == 0u) {                           /* a step begins: its locks are the sound's till the next (lfo.c) */
        if (gen_on >> t & 1u) lk_restore(t);    /* (V2: GENERATE: none) */
        else lk_apply(t, (uint32_t)S->pos);     /* (with a note or without: a trigless lock) */
    }
    if (S->t == sw && !(gen_on >> t & 1u))      /* (V2: GENERATE on: the step not played, its place kept) */
        seq_fire(t);
    if (++S->t >= stl) {
        S->t = 0;
        seq_next(S);
    }
}
static void seq_rewind(void)                    /* audio: all four from their first step */
{
    uint32_t t;
    for (t = 0; t < NT; t++) {
        seq[t].pos = clamp(seq[t].mode, 0, SEQ_NMODE - 1) == SEQ_BWD ? seq_steps(&seq[t]) - 1 : 0;
        seq[t].t = 0, seq[t].dir = 1, seq[t].r_left = 0;
    }
}
static void seq_jump(uint32_t s)                /* audio: slot s in now (a slot recalled, the song's next), all four from
                                                 * their first step, the notes of before released first */
{
    uint32_t t, i;
    for (t = 0; t < NT; t++)
        for (i = 0; i < SEQ_NOFF; i++)
            if (seq[t].off_left[i]) {
                note_off(t, seq[t].off_note[i], SRC_SEQ, 0);
                seq[t].off_left[i] = 0;
            }
    if (slot_used(s)) slot_apply_isr(s);
    seq_rewind();
}

#include "song.c"                               /* song mode: the chain of slots, played and recorded */

static void seq_start(uint32_t slot)            /* audio (NQ_PLAY, the slot in use): all four from their first step (a
                                                 * song: from its first; song record: it starts) */
{
    seq_rewind();
    seq_acc = 0;
    seq_ticks = 0;
    seq_on = 1;
    song_start(slot);
}
static void seq_stop(void)                      /* audio (NQ_STOP): all four stopped, their notes released */
{
    uint32_t t, i;
    if (seq_on && ck_mode == CK_OUT) ck_out(0xFCu, (uint32_t)CTL << 16);   /* (STOP out) */
    seq_on = 0;
    song_stop();                                /* (song record: its last step written, then END) */
    for (t = 0; t < NT; t++) {
        for (i = 0; i < SEQ_NOFF; i++)
            if (seq[t].off_left[i]) {
                note_off(t, seq[t].off_note[i], SRC_SEQ, 0);
                seq[t].off_left[i] = 0;
            }
        seq[t].r_left = 0;
        seq[t].pos = -1;
        lk_restore(t);                          /* (the knobs' sound again) */
    }
}
static int seq_tick(void)                       /* audio: a tick while playing (the FM-1's tempo: seq_ctl; a MIDI
                                                 * clock's: clock.c); 1: the song over, stopped */
{
    uint32_t t;
    if (song_tick()) return 1;                  /* (a pass's end: the song's next step; the song over: stopped) */
    if (seq_recall >= 0 && song_rec != 2u && !(seq_ticks % SEQ_BAR)) {   /* a slot recalled: in at the bar, from
                                                 * step 1 (song record: at the pass's end, song.c) */
        uint32_t s = (uint32_t)seq_recall;
        seq_recall = -1;
        seq_jump(s);
    }
    seq_ticks++;
    for (t = 0; t < NT; t++) {
        seq_track_tick(t);
        arp_tick(t, 1);
    }
    return 0;
}
static void seq_ctl(void)                       /* audio, a control block: the ticks due at the FM-1's tempo */
{
    uint32_t tick = SEQ_TICK_Q16 / (uint32_t)clamp(seq_bpm, 20, 300);
    if (!seq_on) return;
    seq_acc += (uint32_t)CTL << 16;
    while (seq_acc >= tick) {
        seq_acc -= tick;
        if (seq_tick()) return;
    }
}

/* main loop (and arp.c's REC): track t's step for a live-recorded note: the nearest to now (the next one, past half of
 * this) */
static int32_t seq_nearest(uint32_t t)
{
    const seq_trk_t *S = &seq[t];
    int32_t p = S->pos, n = seq_steps(S);
    uint32_t stl = SEQ_DIV_T[clamp(S->div, 0, 7)];
    if (p < 0) return -1;
    if (S->t * 2u < stl) return p;
    switch (clamp(S->mode, 0, SEQ_NMODE - 1)) {
    case SEQ_BWD: return p > 0 ? p - 1 : n - 1;
    case SEQ_PONG: return clamp(p + S->dir, 0, n - 1);
    case SEQ_RND: return p;
    default: return p + 1 < n ? p + 1 : 0;
    }
}
/* main loop (and arp.c's REC): a note into track t's step i (an empty one: CHANCE 100, one hit, one step); chord: its
 * chord (a chord step, chord_step()), 0 a note */
static void seq_write(uint32_t t, int32_t i, uint32_t note, uint32_t chord)
{
    seq_step_t *s = &seq[t].pat[clamp(seq[t].cur, 0, SEQ_NPAT - 1)].st[clamp(i, 0, (int32_t)SEQ_N - 1)];
    if (!s->note) s->chance = 100, s->ratchet = 1, s->len = 1;
    s->note = (uint8_t)note;
    s->chord = (uint8_t)chord;
}
