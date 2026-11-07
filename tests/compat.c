/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* Saves across versions (host mode, tests/run_host.sh): a project as an older GR!MM saved it loads as it did there.
 * Environment, at the end of a host run (tests/host.c):
 *   GRIMM_FILL=1          a varied project into RAM (every step's values over their ranges, chord steps, locks, the
 *                         patterns' settings), into slots A..C, and saved (autosave), with two user sounds
 *   GRIMM_SAVE_OUT=file   the save sectors (6 x 4 KiB) into file
 *   GRIMM_STATE_OUT=file  what was loaded, as text: each step's values, the packed project, the slots, the settings
 *                         (all but their version, which a newer GR!MM raises; then the bytes it adds) and the sounds
 *                         (hex)
 * and at the start, GRIMM_SAVE_IN=file: the save sectors from file (tests/host.c main).
 * tests/data/a1.2-saves.bin was made by Arctic Scream p0larpatch 1.2 (its saves as 1.2 makes them, after the v1.2 tag's
 * Holvut work) with this file and GRIMM_FILL; a1.2-state.txt is what it loaded from it. The same source works on both: the steps' fields are assigned, never pointed to. */
static uint32_t cp_rng = 0x2468ACE1u;
static uint32_t cp_r(uint32_t n)
{
    cp_rng ^= cp_rng << 13, cp_rng ^= cp_rng >> 17, cp_rng ^= cp_rng << 5;
    return n ? cp_rng % n : 0u;
}
static void compat_fill_seq(void)               /* every track's pattern: a varied one */
{
    uint32_t t, k, j;
    for (t = 0; t < NT; t++) {
        seq[t].steps = 1 + (int32_t)cp_r(SEQ_N), seq[t].div = (int32_t)cp_r(8), seq[t].swing = (int32_t)cp_r(128);
        seq[t].mode = (int32_t)cp_r(SEQ_NMODE);
        for (k = 0; k < SEQ_N; k++) {
            seq_step_t *s = &seq[t].pat[0].st[k];
            if (!cp_r(4)) {                     /* (an empty step) */
                s->note = 0, s->chance = 100, s->ratchet = 1, s->len = 1, s->chord = 0, lk_clear_step(t, k);
                continue;
            }
            s->note = 1 + cp_r(127), s->chance = cp_r(101), s->ratchet = 1 + cp_r(4), s->len = 1 + cp_r(SEQ_N);
            s->chord = cp_r(3) ? 0 : 0x80u | cp_r(NVOICING) << 4 | cp_r(NCHORD);
            {                                   /* (the SOUND locks: as the old four a step, the same draws) */
                uint32_t m = cp_r(16);
                lk_clear_step(t, k);
                for (j = 0; j < 4u; j++) {
                    uint32_t lv = cp_r(256);
                    if (m >> j & 1u) lk_set(t, k, j, (int32_t)lv);
                }
            }
        }
    }
}
static void compat_hex(FILE *f, const char *what, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    fprintf(f, "%s %u\n", what, n);
    for (i = 0; i < n; i++) fprintf(f, "%02x%s", p[i], (i & 31u) == 31u || i + 1u == n ? "\n" : "");
}
static void compat_exit(void)                   /* tests/host.c: at the end of the run */
{
    const char *e;
    FILE *f;
    uint32_t t, k;
    if (getenv("GRIMM_FILL")) {
        for (k = 0; k < 3u; k++) compat_fill_seq(), slot_save(k);
        compat_fill_seq();
        snd_save(4, 0, 1), snd_save(9, 1, 6);
        autosave();
    }
    if ((e = getenv("GRIMM_SAVE_OUT")) != 0 && (f = fopen(e, "wb")) != 0)
        fwrite(fake_save, 1, sizeof fake_save, f), fclose(f);
    if ((e = getenv("GRIMM_STATE_OUT")) != 0 && (f = fopen(e, "w")) != 0) {
        for (t = 0; t < NT; t++) {
            fprintf(f, "track %u steps %d div %d swing %d mode %d\n", t, (int)seq[t].steps, (int)seq[t].div,
                    (int)seq[t].swing, (int)seq[t].mode);
            for (k = 0; k < SEQ_N; k++) {
                const seq_step_t *s = &seq[t].pat[0].st[k];
                int32_t m = 0, lv[4] = {0, 0, 0, 0}, j, i;
                for (j = 0; j < 4; j++)
                    if ((i = lk_find(t, k, (uint32_t)j)) >= 0) m |= 1 << j, lv[j] = lk_e[t][i].v;
                fprintf(f, "%2u %3d %3d %d %2d %3d %2d %3d %3d %3d %3d\n", k, (int)s->note, (int)s->chance,
                        (int)s->ratchet, (int)s->len, (int)s->chord, (int)m, (int)lv[0], (int)lv[1], (int)lv[2], (int)lv[3]);
            }
        }
        pk_work(st_img);
#ifdef FX2_PK
        compat_hex(f, "work", st_img, WORK_PK - FX2_PK);   /* (V1.0's part: V2's MASTER and DRIVE after it, not 1.0's) */
#else
        compat_hex(f, "work", st_img, WORK_PK);
#endif
        for (k = 0; k < 4u; k++) compat_hex(f, "slot", slots[k], SLOT_ALL);
        compat_hex(f, "settings", (const uint8_t *)&set, 167u);   /* (V1.0's and later's alike: all but the version,
                                                 * then what comes after it: V2's CLOCK, 0 INT; 1.0: none, 0) */
        fprintf(f, "after %u\n", sizeof set > 168u ? (unsigned)((const uint8_t *)&set)[168] : 0u);
        compat_hex(f, "sounds", save_flash(0), 4096u);
        fclose(f);
    }
}
