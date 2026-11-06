/* SPDX-License-Identifier: GPL-3.0-only
 * Drum models from FNTSMG-1 by fantasmogenesis (github.com/fantasmogenesis/fntsmg-1, commit 2e84d10), GPL-3.0:
 * motores/src/fg_kick.h/.c (MEMBRANA), fg_metal.h/.c (GRANIZO), kept as they are but for the names of their helpers,
 * and TONE: 65536u * 2447u * p overflows 32 bits above p = 26 (there too), so it wrapped; here 64 bits, 22 .. 120 Hz. */
static const uint32_t DM_MEMB_RATIO[4] = {65536, 104464, 139985, 150405};   /* modes of a round membrane, Q16 */
static int32_t dm_sq2(unsigned v) { return (int32_t)(v * v * 2u); }
static int32_t dmk_hold(unsigned p) { return (int32_t)(p * 17u); }
static int32_t dmk_drive(unsigned p) { return 4096 + (int32_t)(p * 4096u * 25u / 10u / 127u); }
static int32_t dmh_hold(unsigned p) { return (int32_t)(p * 12u); }
static int32_t dmh_drive(unsigned p) { return 4096 + (int32_t)(p * 4096u * 25u / 10u / 127u); }
static int32_t dm_svf_f2(uint32_t fc_hz) { return 2 * fg_sin(fg_inc(fc_hz << 8) >> 2); }
typedef struct {
    uint8_t on, cnt;
    uint32_t ph[4], inc0[4], inc[4];
    int32_t amp[4], k[4], g[4], kill;
    int32_t tenv, kt16, tens;        /* tensión: la altura arranca arriba y cae (Q30 / Q28) */
    int32_t att, att_inc;            /* ataque del mazo, Q15 */
    int32_t click, kc, clvl, chp;    /* chasquido de ruido */
    int32_t lvl, norm;
    int32_t hold, drive;
    uint32_t seed;
} fgk_memb_t;typedef struct {
    uint8_t on;
    int32_t amp, ka, kill, att, att_inc;
    uint32_t thresh;                    /* probabilidad de grano por muestra (de 2^32) */
    int32_t glen, gpos, gamp, ginv;     /* grano en curso: largo, posición, amplitud, 1/largo (Q16) */
    uint32_t gph, ginc, cinc;           /* GRANO alto: cada grano es una gota con tono cerca del COLOR */
    int32_t tone;
    int32_t lo, bp, sf, sq, norm, hpz;
    int32_t hold, drive, lvl;
    uint32_t seed;
} fgm_hail_t;static void memb_trig(fgk_memb_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t f_q8 = (uint32_t)(((uint64_t)(22u * 256u) * fg_exp2_q16((uint32_t)((uint64_t)p[0] * 65536u * 2447u / 127000u))) >> 16);   /* 22..120 Hz */
    uint32_t inc = fg_inc(f_q8), m = p[3], base = 50u + p[2] * 65u / 127u;
    int32_t a = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    /* MATERIAL: piel blanda = solo el fundamental; dura = los tres modos altos fuertes y largos */
    static const int32_t GH[4] = {32767, 30000, 26000, 22000};
    for (int i = 0; i < 4; i++) {
        v->inc0[i] = (uint32_t)(((uint64_t)inc * DM_MEMB_RATIO[i]) >> 16);
        v->inc[i] = v->inc0[i];
        v->ph[i] = 0;
        v->amp[i] = a;
        v->g[i] = i == 0 ? GH[0] : fg_amount(m, GH[i]);
        int32_t idx = i == 0 ? (int32_t)base : (int32_t)base - 4 * i - (int32_t)((127u - m) * 40u / 127u);
        v->k[i] = FG_DECAY[idx < 4 ? 4 : idx];
    }
    {   /* la suma de modos no debe saturar sola: si no, MATERIAL no se oye */
        int32_t sum = v->g[0] + v->g[1] + v->g[2] + v->g[3];
        v->norm = (int32_t)(((int64_t)32767 * 36000) / sum);
        if (v->norm > 32767) v->norm = 32767;
    }
    /* TENSIÓN: la altura arranca hasta 4x arriba (2 octavas) y se relaja en ~6..130 ms */
    v->tens = (int32_t)((3u << 28) / 127u * p[1]);
    v->tenv = FG_ONE30;
    v->kt16 = fg_pow16(fg_decay(p[1], 30, 85));
    /* GOLPE: mazo blando = ataque de hasta 15 ms y nada de chasquido; palo duro = ataque inmediato y chasquido */
    unsigned golpe = p[4];
    unsigned att_smp = (127u - golpe) * 660u / 127u;
    v->att = 0;
    v->att_inc = 32767 / (int32_t)(att_smp + 1u);
    v->click = a;
    v->kc = fg_decay(golpe, 6, 32);                             /* el chasquido dura 4..17 ms */
    v->clvl = fg_amount(golpe, 40000);
    v->chp = 0;
    v->kill = 0;
    v->lvl = dm_sq2(p[7]);
    v->hold = dmk_hold(p[6]);
    v->drive = dmk_drive(p[6]);
    v->cnt = 0; v->on = 1;
}

static void memb_render(fgk_memb_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            uint32_t m = (1u << 28) + (uint32_t)(((int64_t)v->tens * v->tenv) >> 30);
            for (int j = 0; j < 4; j++) v->inc[j] = (uint32_t)(((uint64_t)v->inc0[j] * m) >> 28);
            v->tenv = fg_mul30(v->tenv, v->kt16);
        }
        int32_t s = 0;
        for (int j = 0; j < 4; j++) {
            s += fg_mul15(fg_mul30(fg_sin(v->ph[j]), v->amp[j]), v->g[j]);
            v->ph[j] += v->inc[j];
            if (v->hold <= 0 || v->kill || j > 0) v->amp[j] = fg_mul30(v->amp[j], v->kill ? v->kill : v->k[j]);
        }
        if (v->hold > 0) v->hold--;
        if (v->att < 32767) { v->att += v->att_inc; if (v->att > 32767) v->att = 32767; }
        s = fg_mul15(fg_mul15(s, v->norm), v->att);
        s = fg_soft((int32_t)(((int64_t)s * v->drive) >> 12));
        if (v->click > 2000 && v->clvl) {                       /* chasquido encima de la saturación */
            int32_t nz = fg_mul30(fg_noise(&v->seed), v->click);
            int32_t hp = nz - v->chp; v->chp += (nz - v->chp) >> 3;
            s += fg_mul15(hp, v->clvl);
            v->click = fg_mul30(v->click, v->kill ? v->kill : v->kc);
        }
        out[i] += fg_mul15(s, v->lvl) * 2;
        if (v->amp[0] < 2000 && v->click <= 2000) v->on = 0;
    }
}

static void hail_trig(fgm_hail_t *v, const uint8_t *p, unsigned vel)
{
    /* DENSIDAD: de 150 a 30 000 granos por segundo (7.6 octavas, parejo en el oído) */
    uint32_t dens = (uint32_t)(((uint64_t)150u * fg_exp2_q16(p[0] * 65536u * 76u / 1270u)) >> 16);
    v->thresh = (uint32_t)(((uint64_t)dens << 32) / FG_SR);
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->ka = fg_decay(p[1], 10, 100);                            /* CAÍDA: hat cerrado ~5 ms .. abierto ~1.2 s */
    /* COLOR: 1.5 .. 13 kHz; RESO: Q 0.7 .. 12, compensando la ganancia para que suene igual de fuerte */
    uint32_t fc = (uint32_t)(((uint64_t)1500u * fg_exp2_q16(p[2] * 65536u * 31u / 1270u)) >> 16);
    v->sf = dm_svf_f2(fc > 13000u ? 13000u : fc);                  /* el filtro corre al doble: estable hasta 13 kHz */
    v->sq = 46000 - (int32_t)(p[3] * 350u);                     /* 1/Q: 1.4 .. 0.05: de soplo a silbido */
    v->norm = v->sq < 1500 ? 1500 : v->sq;
    /* AGITA: 0 = golpe seco; 127 = shaker que entra en ~60 ms */
    v->att = 0; v->att_inc = 32767 / (int32_t)(p[4] * 2650u / 127u + 1u);
    /* GRANO: de chispa de ruido de 1 muestra a gota con tono de ~8 ms */
    v->glen = 1 + (int32_t)(((uint32_t)p[5] * p[5]) * 350u / 16129u);
    v->tone = (int32_t)p[5] * 258; v->cinc = fg_inc((fc > 13000u ? 13000u : fc) << 8); v->gph = 0;
    v->gpos = v->glen; v->ginv = 65536 / v->glen;
    v->hold = dmh_hold(p[6]); v->drive = dmh_drive(p[6]);
    v->lvl = dm_sq2(p[7]);
    v->lo = v->bp = v->hpz = 0; v->kill = 0; v->on = 1;
}

static void hail_render(fgm_hail_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        uint32_t r = fg_rand(&v->seed);
        if (r < v->thresh) {                                     /* nace un grano, con su altura al azar cerca del COLOR */
            v->gpos = 0; v->gamp = (int32_t)(fg_rand(&v->seed) >> 17) + 16384;
            v->ginc = (uint32_t)(((uint64_t)v->cinc * (45000u + (fg_rand(&v->seed) >> 16))) >> 16);
        }
        int32_t x = 0;
        if (v->gpos < v->glen) {
            int32_t e = v->gamp - (int32_t)(((int64_t)v->gamp * v->gpos * v->ginv) >> 16);   /* sin dividir por muestra */
            int32_t g = fg_noise(&v->seed) + fg_mul15(fg_sin(v->gph) - fg_noise(&v->seed), v->tone);   /* ruido -> gota */
            v->gph += v->ginc;
            x = fg_mul15(g, e);
            v->gpos++;
        }
        int32_t y = x - v->hpz; v->hpz += (x - v->hpz) >> 3;     /* sin graves */
        int32_t hi = 0;
        for (int k = 0; k < 2; k++) {
            v->lo += fg_mul15(v->sf, v->bp);
            hi = y - v->lo - fg_mul15(v->sq, v->bp);
            v->bp += fg_mul15(v->sf, hi);
            v->bp = fg_clamp16(v->bp); v->lo = fg_clamp16(v->lo);
        }
        int32_t s = fg_mul15(v->bp, v->norm) * 2 + (hi >> 2);
        s = fg_mul30(s, v->amp);
        if (v->att < 32767) { v->att += v->att_inc; if (v->att > 32767) v->att = 32767; s = fg_mul15(s, v->att); }
        s = fg_soft((int32_t)(((int64_t)s * v->drive) >> 12)) >> 1;
        out[i] += fg_mul15(s, v->lvl) * 2;
        if (v->hold > 0 && !v->kill) v->hold--;
        else v->amp = fg_mul30(v->amp, v->kill ? v->kill : v->ka);
        if (v->amp < 2000) v->on = 0;
    }
}
