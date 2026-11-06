/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* SYSTEM 3's CC MAP: fixed CCs, nothing to learn. The channel picks the track, as MIDI CHANNEL says (ALL > SEL, CH n > SEL:
 * the selected one); SCALE's are for all. A CC MIDI LEARN has linked goes there instead.
 * Switches, 64 and up on, under 64 off: 88 mute, 89 solo, 90 chord mode, 92 LATCH (the track's); 119 PLAY (all). */
typedef struct { uint8_t cc, pg, k; } cc_map_t;
static const cc_map_t CC_MAP[] = {
    {7, PG_HOME, 0}, {74, PG_HOME, 1}, {10, PG_MIX2, 0}, {71, PG_MIX2, 3}, {91, PG_MIX1, 2}, {94, PG_MIX1, 1},
    {20, PG_SOUND, 0}, {21, PG_SOUND, 1}, {22, PG_SOUND, 2}, {23, PG_SOUND, 3},
    {24, PG_ENV, 0}, {25, PG_ENV, 1}, {26, PG_ENV, 2}, {27, PG_ENV, 3},
    {28, PG_LFO, 0}, {29, PG_LFO, 1}, {30, PG_LFO, 2}, {31, PG_LFO, 3},
    {85, PG_DRONE, 0}, {86, PG_DRONE, 1}, {87, PG_DRONE, 2},
    {102, PG_BANK, 0}, {103, PG_BANK, 1}, {104, PG_BANK, 2}, {105, PG_BANK, 3}, {106, PG_BANK2, 0},
    {107, PG_DRUM1, 0}, {108, PG_DRUM1, 1}, {109, PG_DRUM1, 2}, {110, PG_DRUM1, 3},
    {111, PG_DRUM2, 0}, {112, PG_DRUM2, 1}, {113, PG_DRUM2, 2}, {114, PG_DRUM2, 3},
    {115, PG_SCALE, 0}, {116, PG_SCALE, 1}, {117, PG_SCALE, 2}, {118, PG_SCALE, 3},
};
static void cc_map(uint32_t cc, uint32_t val, uint32_t ch)
{
    uint32_t i, t;
    uint8_t L[5];
    if (set.mch >= MCH_ALL) t = sel;
    else if (ch - set.mch < NT) t = ch - set.mch;
    else t = NT;
    if (cc == 119u) {                           /* PLAY / STOP: only when it changes */
        if ((val >= 64u) != (playing != 0u)) playing ^= 1u, nq_put(playing ? NQ_PLAY : NQ_STOP, 0, (uint32_t)slot_cur & 3u);
        return;
    }
    if (t < NT && (cc == 88u || cc == 89u || cc == 90u || cc == 92u)) {
        uint32_t on = val >= 64u, s = sel;
        if (cc == 88u) trk_mute = on ? trk_mute | 1u << t : trk_mute & ~(1u << t);
        else if (cc == 89u) trk_solo = on ? trk_solo | 1u << t : trk_solo & ~(1u << t);
        else if (cc == 90u) trk[t].chord = (int32_t)on;
        else sel = t, set_latch((int32_t)on), sel = s;
        return;
    }
    for (i = 0; i < sizeof CC_MAP / sizeof CC_MAP[0]; i++) {
        if (CC_MAP[i].cc != cc) continue;
        if (t >= NT && CC_MAP[i].pg != PG_SCALE) return;   /* (not one of the tracks' channels) */
        L[0] = (uint8_t)cc, L[1] = CC_MAP[i].pg, L[2] = CC_MAP[i].k, L[3] = (uint8_t)(t < NT ? t : sel);
        L[4] = (uint8_t)clamp(trk[L[3]].eng, 0, NENG - 1);
        cc_apply(L, val);
        return;
    }
}
