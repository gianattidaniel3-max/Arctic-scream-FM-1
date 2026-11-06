/* SPDX-License-Identifier: GPL-3.0-only
 * Drum models from FNTSMG-1 by fantasmogenesis (github.com/fantasmogenesis/fntsmg-1, commit 2e84d10), GPL-3.0:
 * motores/src/fg_kick.h/.c (MEMBRANA, BARRIDO), fg_metal.h/.c (GRANIZO), fg_snare.h/.c (CAJA, APLAUSO; from FNTSMG-1 0.10,
 * commit 8f14207), kept as they are but for the names of their helpers, and the kicks' TONE: 65536u * 2447u * p (MEMBRANA)
 * and * 2285u (BARRIDO) overflow 32 bits above p = 26 (there too), so it wrapped; here 64 bits: 22 .. 120 and 20 .. 97 Hz. */
static const uint32_t DM_MEMB_RATIO[4] = {65536, 104464, 139985, 150405};   /* modes of a round membrane, Q16 */
static int32_t dm_sq2(unsigned v) { return (int32_t)(v * v * 2u); }
static int32_t dmk_hold(unsigned p) { return (int32_t)(p * 17u); }
static int32_t dmk_drive(unsigned p) { return 4096 + (int32_t)(p * 4096u * 25u / 10u / 127u); }
static int32_t dmh_hold(unsigned p) { return (int32_t)(p * 12u); }
static int32_t dmh_drive(unsigned p) { return 4096 + (int32_t)(p * 4096u * 25u / 10u / 127u); }
static int32_t dm_svf_f(uint32_t fc_hz) { return 2 * fg_sin(fg_inc(fc_hz << 8) >> 1); }
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

/* ---- BARRIDO (fg_kick.c) ---- */
typedef struct {
    uint8_t on, cnt;
    uint32_t ph, inc, inc_end;
    int32_t amp, ka, kill;           /* envolvente de amplitud Q30 */
    int32_t penv, kp16, sweep;       /* envolvente de altura Q30, octavas de barrido Q16 */
    int32_t fold_g, lvl;             /* doblez Q12, nivel Q15 */
    int32_t hold, drive;             /* pegada: muestras sin caer, ganancia Q12 a la saturación */
    int32_t click, kc, clvl, chp;    /* CLICK: 0 = ataque suave de ~3 ms, 127 = chasquido fuerte */
    int32_t att, att_inc;
    uint32_t cph, cinc, seed;
} fgk_sweep_t;

static void sweep_trig(fgk_sweep_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t f_q8 = (uint32_t)(((uint64_t)(20u * 256u) * fg_exp2_q16((uint32_t)((uint64_t)p[0] * 65536u * 2285u / 127000u))) >> 16);   /* 20..97 Hz */
    v->inc_end = fg_inc(f_q8);
    v->sweep = (int32_t)(32768u + p[1] * 65536u * 11u / 2u / 127u);   /* 0.5 .. 6 octavas arriba */
    v->penv = FG_ONE30;
    v->kp16 = fg_pow16(fg_decay(p[1], 45, 95));                 /* y más largo cuanto más barrido */
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->ka = fg_decay(p[2], 55, 118);                            /* caída ~60 ms .. ~2.8 s */
    v->kill = 0;
    v->fold_g = 4096 + (int32_t)(p[3] * 4096u * 5u / 127u);     /* doblez: ganancia 1..6 antes del plegado */
    v->lvl = dm_sq2(p[7]);
    v->click = v->amp; v->kc = fg_decay(p[5], 12, 32); v->clvl = fg_amount(p[5], 60000);   /* click: ruido brillante y un blip, 6..17 ms */
    v->att = 0; v->att_inc = 32767 / (int32_t)((127u - p[5]) * 130u / 127u + 1u);           /* sin click, el golpe entra suave */
    v->cph = 0; v->cinc = fg_inc(1800u << 8); v->chp = 0;
    v->hold = dmk_hold(p[6]);
    v->drive = dmk_drive(p[6]);
    v->ph = 0x40000000u;                                        /* arranca en coseno: el primer ciclo es un escalón */
    v->cnt = 0; v->on = 1;
}

static void sweep_render(fgk_sweep_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            uint32_t oct = (uint32_t)fg_mul30(v->sweep, v->penv);
            v->inc = (uint32_t)(((uint64_t)v->inc_end * fg_exp2_q16(oct)) >> 16);
            v->penv = fg_mul30(v->penv, v->kp16);
        }
        int32_t x = fg_mul30(fg_sin(v->ph), v->amp);
        v->ph += v->inc;
        x = fg_fold((int32_t)(((int64_t)x * v->fold_g) >> 12));
        x = fg_soft((int32_t)(((int64_t)x * v->drive) >> 12));      /* el cuerpo a escala completa: el bombo manda */
        if (v->att < 32767) { v->att += v->att_inc; if (v->att > 32767) v->att = 32767; x = fg_mul15(x, v->att); }
        if (v->click > 2000 && v->clvl) {                       /* el click va encima de la saturación: no se aplasta */
            int32_t nz = fg_noise(&v->seed), hp = nz - v->chp;
            v->chp += (nz - v->chp) >> 2;
            x += fg_mul15(fg_mul30((hp >> 1) + fg_sin(v->cph), v->click), v->clvl);
            v->cph += v->cinc;
            v->click = fg_mul30(v->click, v->kill ? v->kill : v->kc);
        }
        out[i] += fg_mul15(x, v->lvl) * 2;
        if (v->hold > 0 && !v->kill) v->hold--;
        else v->amp = fg_mul30(v->amp, v->kill ? v->kill : v->ka);
        if (v->amp < 2000) v->on = 0;
    }
}

/* ---- CAJA, APLAUSO (fg_snare.c) ---- */
typedef struct {
    uint8_t on, cnt;
    uint32_t ph[2], inc0[2], inc[2], rph[2], rinc[2];
    int32_t amp[2], k[2], g, kill;
    int32_t tenv, kt16, depth;              /* el parche cae de afinación al golpe */
    int32_t snap, ksnap, slvl, wire, kw, wlvl;   /* ruido: chasquido inicial + cola */
    int32_t hpz, lpz, chp, clp;             /* ruido: paso bajo y paso alto (BRILLO) */
    int32_t click, kc, clvl;                /* el aro (CHASQUIDO arriba de 90) */
    int32_t hold, drive;
    uint32_t seed;
} fgs_voice_t;

typedef struct {
    uint8_t on, left;
    uint32_t t, next, gap;               /* ráfagas: cuándo llega la siguiente y cuántas faltan */
    int32_t env, kb, tail, kt, room, kr, a;
    int32_t lo, bp, lo2, bp2, sf, sq, gain, rlp, rlvl;
    int32_t hold, drive, kill;
    uint32_t seed;
} fgs_clap_t;

static void voice_trig(fgs_voice_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t f_q8 = (uint32_t)(((uint64_t)(120u * 256u) * fg_exp2_q16((uint32_t)((uint64_t)p[0] * 65536u * 1737u / 127000u))) >> 16);   /* TONO 120..400 Hz */
    int32_t a = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    unsigned idx = 50u + p[1] * 42u / 127u;                          /* CUERPO: suena 50 .. 600 ms (60 dB) */
    v->inc0[0] = fg_inc(f_q8);
    v->inc0[1] = (uint32_t)(((uint64_t)v->inc0[0] * 104858u) >> 16);
    v->inc[0] = v->inc0[0]; v->inc[1] = v->inc0[1];
    v->ph[0] = 0x40000000u; v->ph[1] = 0;
    v->amp[0] = a; v->amp[1] = a / 5 * 3;
    v->k[0] = FG_DECAY[idx]; v->k[1] = FG_DECAY[idx - 8u];
    v->g = 9000 + (int32_t)p[1] * 190;                              /* y cuánto se oye */
    v->tenv = FG_ONE30; v->kt16 = fg_pow16(FG_DECAY[38]);              /* cae de afinación en ~25 ms */
    v->depth = 80000000 + (int32_t)p[5] * 700000;                    /* de +30 % a +63 % al golpe: más chasquido, más caída */
    v->wire = a; v->kw = fg_decay(p[3], 62, 108);                    /* CAÍDA del ruido: 90 ms .. 1.3 s */
    v->wlvl = fg_amount(p[2], 60000);                                /* RUIDO */
    v->snap = a; v->ksnap = FG_DECAY[30];                              /* chasquido: ~15 ms */
    v->slvl = fg_amount(p[5] > 90u ? 90u : p[5], 50000);
    uint32_t fhp = 300u + p[4] * 3700u / 127u;                       /* BRILLO: el ruido desde 0.3 .. 4 kHz y hasta 6 .. 14 kHz */
    v->chp = (int32_t)(fhp * 467u / 100u);
    v->clp = 20000 + (int32_t)p[4] * 80;
    v->hpz = v->lpz = 0;
    v->click = p[5] > 90u ? a : 0; v->kc = FG_DECAY[22]; v->clvl = (int32_t)(p[5] > 90u ? (p[5] - 90u) * 700u : 0u);   /* el aro */
    v->rph[0] = v->rph[1] = 0;
    v->rinc[0] = fg_inc(640u << 8); v->rinc[1] = fg_inc(1520u << 8);
    v->hold = (int32_t)(p[6] * 9u);                                    /* PEGADA: el cuerpo se sostiene hasta ~26 ms */
    v->drive = 4096 + (int32_t)(p[6] * 4096u * 3u / 127u);             /* y satura x1..x4 */
    v->kill = 0;
    v->cnt = 0; v->on = 1;
}

static void voice_render(fgs_voice_t *v, int32_t lvl, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            uint32_t m = (1u << 28) + (uint32_t)(((int64_t)v->depth * v->tenv) >> 30);
            v->inc[0] = (uint32_t)(((uint64_t)v->inc0[0] * m) >> 28);
            v->inc[1] = (uint32_t)(((uint64_t)v->inc0[1] * m) >> 28);
            v->tenv = fg_mul30(v->tenv, v->kt16);
        }
        int32_t kill = v->kill;
        int32_t body = fg_mul15(fg_mul30(fg_sin(v->ph[0]), v->amp[0]) + fg_mul30(fg_sin(v->ph[1]), v->amp[1]), v->g);
        v->ph[0] += v->inc[0]; v->ph[1] += v->inc[1];
        if (v->hold > 0 && !kill) v->hold--; else { v->amp[0] = fg_mul30(v->amp[0], kill ? kill : v->k[0]); v->amp[1] = fg_mul30(v->amp[1], kill ? kill : v->k[1]); }
        int32_t nz = fg_noise(&v->seed);
        v->lpz += fg_mul15(nz - v->lpz, v->clp);
        v->hpz += fg_mul15(v->lpz - v->hpz, v->chp);
        int32_t w = v->lpz - v->hpz;
        int32_t s = body + fg_mul15(fg_mul30(w, v->wire), v->wlvl) + fg_mul15(fg_mul30(w, v->snap), v->slvl);
        v->wire = fg_mul30(v->wire, kill ? kill : v->kw);
        v->snap = fg_mul30(v->snap, kill ? kill : v->ksnap);
        s = fg_soft((int32_t)(((int64_t)s * v->drive) >> 12));
        if (v->click > 2000) {                                        /* palo contra el aro, encima de la saturación */
            int32_t r = (fg_sin(v->rph[0]) + (fg_sin(v->rph[1]) >> 1)) >> 1;
            s += fg_mul15(fg_mul30(r + (fg_noise(&v->seed) >> 3), v->click), v->clvl);
            v->rph[0] += v->rinc[0]; v->rph[1] += v->rinc[1];
            v->click = fg_mul30(v->click, kill ? kill : v->kc);
        }
        out[i] += fg_mul15(s, lvl) * 2;
        if (v->amp[0] < 2000 && v->wire < 2000 && v->snap < 2000 && v->click <= 2000) v->on = 0;
    }
}

/* ------------------------------------------------------------ aplauso --- */
/* Ruido por una banda (700..2.6 kHz). Cada ráfaga entra de golpe y se apaga en ~5 ms; las palmas llegan
 * con separación despareja y fuerza al azar. La última deja la cola (CAÍDA); SALA es otra cola, oscura y
 * más larga, mezclada aparte para que se oiga aunque la cola principal sea corta. */
static void clap_trig(fgs_clap_t *c, const uint8_t *p, unsigned vel)
{
    int32_t a = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    uint32_t fc = (uint32_t)((700ull * fg_exp2_q16((uint32_t)((uint64_t)p[0] * 65536u * 1893u / 127000u))) >> 16);   /* 700 Hz .. 2.6 kHz */
    c->sf = dm_svf_f(fc);
    c->sq = 40000 - (int32_t)(p[5] * 260u);                       /* COLOR: banda ancha y crujiente .. angosta y con tono (Q 0.8..5) */
    c->gain = c->sq;                                               /* la banda angosta suena más fuerte: se compensa */
    c->left = (uint8_t)(p[1] * 5u / 127u);                         /* PALMAS: 1..6 ráfagas */
    c->gap = 176u + p[2] * 1146u / 127u;                           /* SEPARA: 4..30 ms */
    c->a = a; c->t = 0; c->next = 0;
    c->env = 0; c->kb = FG_DECAY[12];
    c->tail = 0; c->kt = fg_decay(p[3], 50, 104);                  /* CAÍDA: ~50 ms .. ~1.2 s */
    c->room = 0; c->kr = fg_decay(p[3] / 2u + 64u, 70, 116);       /* la sala dura más que la cola */
    c->rlvl = fg_amount(p[4], 30000);
    c->hold = (int32_t)(p[6] * 8u);                                /* PEGADA: sostiene la cola hasta ~23 ms */
    c->drive = 4096 + (int32_t)(p[6] * 4096u * 3u / 127u);         /* y satura x1..x4 */
    c->lo = c->bp = c->lo2 = c->bp2 = c->rlp = 0; c->kill = 0; c->on = 1;
}

static void clap_render(fgs_clap_t *c, int32_t lvl, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && c->on; i++) {
        if (c->t == c->next && !c->kill) {                        /* llega una palma */
            uint32_t r = fg_rand(&c->seed);
            int32_t force = 21000 + (int32_t)((r >> 16) % 11768u); /* 0.65 .. 1.0 */
            c->env = fg_mul15(c->a, force);
            if (c->left) { c->left--; c->next = c->t + c->gap * (870u + (r & 255u)) / 1000u; }   /* ±13 % */
            else { c->tail = c->a; c->room = c->a; c->next = 0xFFFFFFFFu; }
        }
        c->t++;
        int32_t kill = c->kill;
        int32_t nz = fg_noise(&c->seed);
        c->lo += fg_mul15(c->sf, c->bp);
        int32_t hi = fg_mul15(nz, c->gain) - c->lo - fg_mul15(c->sq, c->bp);
        c->bp += fg_mul15(c->sf, hi);
        c->bp = fg_clamp16(c->bp); c->lo = fg_clamp16(c->lo);
        c->lo2 += fg_mul15(c->sf, c->bp2);                         /* segunda banda igual: faldas de 12 dB/oct */
        int32_t hi2 = fg_mul15(c->bp, c->gain) - c->lo2 - fg_mul15(c->sq, c->bp2);
        c->bp2 += fg_mul15(c->sf, hi2);
        c->bp2 = fg_clamp16(c->bp2); c->lo2 = fg_clamp16(c->lo2);
        int32_t s = fg_mul30(c->bp2 * 6, c->env + c->tail);                 /* (+6 dB: el aplauso quedaba 12 dB abajo del bombo) */
        c->rlp += fg_mul15(nz - c->rlp, 5000);                     /* sala: ruido oscuro (~1.2 kHz) */
        s += fg_mul15(fg_mul30(c->rlp * 2, c->room), c->rlvl);
        s = fg_soft((int32_t)(((int64_t)s * c->drive) >> 12));
        c->env = fg_mul30(c->env, kill ? kill : c->kb);
        if (c->hold > 0 && c->tail && !kill) c->hold--; else c->tail = fg_mul30(c->tail, kill ? kill : c->kt);
        c->room = fg_mul30(c->room, kill ? kill : c->kr);
        out[i] += fg_mul15(s, lvl) * 2;
        if (c->next == 0xFFFFFFFFu && c->env < 2000 && c->tail < 2000 && c->room < 2000) c->on = 0;
        if (kill && c->env < 2000 && c->tail < 2000 && c->room < 2000) c->on = 0;
    }
}
