/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* (Arctic Scream) the drums' audio (drum.c's): a hit, a block; here, after lfo.c, so the hit takes the LFOs, the locks
 * and WARP as a voice does (lfo_params), and ENV's AHD (env_coef) */
static void dm_kill(uint32_t t, uint32_t s)
{
    dm_slot_t *v = &dm[t].s[s];
    switch (dm[t].md[s]) {
    case DM_MEMB: v->m.kill = FG_DECAY[2]; break;
    case DM_HAIL: v->h.kill = FG_DECAY[2]; break;
    case DM_KICK: v->k.kill = FG_DECAY[2]; break;
    case DM_SNARE: v->s.kill = FG_DECAY[2]; break;
    case DM_FMD: v->f.kill = FG_DECAY[2]; break;
    case DM_BELL: v->b.kill = 1; break;         /* (its partials fall fast: bell_render) */
    case DM_PART: v->p.kill = FG_DECAY[2]; break;
    case DM_WEIRD: v->w.kill = FG_DECAY[2]; break;
    default: v->c.kill = FG_DECAY[6]; break;    /* (a clap: as FNTSMG-1's, a little slower) */
    }
}
static uint32_t dm_scale(uint32_t inc, uint32_t f) { return (uint32_t)(((uint64_t)inc * f) >> 16); }
static void dm_hit(uint32_t t, uint32_t note)
{
    uint32_t model = dm_model(t), k, o = dm[t].cur, n = o ^ 1u, vel = 110u, f, eng = ENG_KICK + model;
    uint8_t p[8];
    int32_t p16 = tn_p16(note) - 48 * 16, w = clamp(dm_v[t][6], 0, 127), h = clamp(dm_v[t][7], 0, 127);
    uint32_t r;
    const volatile int32_t *q = lfo_params(NV - 1u, t, eng);   /* (its knobs as a voice's: the step's locks, the LFOs,
                                                 * DRONE and WARP on them; voices are done when the drums play) */
    dm[t].seed = dm[t].seed ? dm[t].seed : 2654435769u + t;
    r = fg_rand(&dm[t].seed);
    int32_t pr = lk_m[t] & 1u ? lk_v[t][0] : trk[t].p[eng][0];   /* (PRESET: its lock, its LFOs; not DRONE's or WARP's
                                                 * wander, which would swap whole sounds) */
    pr = clamp(lfo_move(t, LD_K1, clamp(pr, 0, DM_NPR - 1), DM_NPR - 1), 0, DM_NPR - 1);
    if (pr != clamp(trk[t].p[eng][0], 0, DM_NPR - 1))   /* PRESET locked (or moved) away from the knob's: that preset's
                                                 * sound, this hit alone */
        for (k = 0; k < 8u; k++) p[k] = DM_PR[model * DM_NPR + (uint32_t)pr][k];
    else
        for (k = 0; k < 8u; k++)
            p[DM_MAP[model][k]] = (uint8_t)clamp(k < 3u ? q[1u + k] : lfo_move(t, LD_D1 + (k - 3u), clamp(dm_v[t][k - 3u], 0, 127), 127), 0, 127);
    p16 += lfo_p16[t];                          /* (its LFOs on PITCH) */
    dm_env[t].e = 0, dm_env[t].st = 1, dm_env[t].hold = 0;   /* (AHD: from the hit) */
    if (!dm_v[t][5]) p16 = lfo_p16[t];          /* (KEYS OFF) */
    if (w) p16 += (int32_t)((r >> 8 & 255u) - 128u) * w / 127 * 16 / 128;   /* WANDER: +-1 semitone at its top */
    if (h) vel = vel * (127u - ((r >> 16 & 255u) * (uint32_t)h >> 8) * 3u / 4u) / 127u;   /* HUMAN */
    dm_kill(t, o);                              /* the old one out, ~3 ms */
    dm[t].cur = (uint8_t)n, dm[t].md[n] = (uint8_t)model;
    f = pow2_q16(p16);                          /* (C3: its TONE) */
    switch (model) {
    case DM_MEMB: {
        fgk_memb_t *v = &dm[t].s[n].m;
        v->seed = dm[t].seed + 1u;
        memb_trig(v, p, vel);
        for (k = 0; k < 4u; k++) v->inc0[k] = dm_scale(v->inc0[k], f), v->inc[k] = v->inc0[k];
        break;
    }
    case DM_KICK: {
        fgk_sweep_t *v = &dm[t].s[n].k;
        v->seed = dm[t].seed + 1u;
        sweep_trig(v, p, vel);
        v->inc_end = dm_scale(v->inc_end, f);
        break;
    }
    case DM_SNARE: {
        fgs_voice_t *v = &dm[t].s[n].s;
        v->seed = dm[t].seed + 1u;
        voice_trig(v, p, vel);
        for (k = 0; k < 2u; k++) v->inc0[k] = dm_scale(v->inc0[k], f), v->inc[k] = v->inc0[k];
        dm[t].lvl[n] = dm_sq2(p[7]);
        break;
    }
    case DM_CLAP: {
        fgs_clap_t *v = &dm[t].s[n].c;
        p[0] = (uint8_t)clamp((int32_t)p[0] + p16 * 559 / 1600, 0, 127);   /* (TONE: 1.9 octaves in 127) */
        v->seed = dm[t].seed + 1u;
        clap_trig(v, p, vel);
        dm[t].lvl[n] = dm_sq2(p[7]);
        break;
    }
    case DM_FMD: {
        pc_fmd_t *v = &dm[t].s[n].f;
        v->seed = dm[t].seed + 1u;
        fmd_trig(v, p, vel);
        v->inc0 = pc_scale(v->inc0, f), v->inc = v->inc0;
        break;
    }
    case DM_BELL: {
        pc_bell_t *v = &dm[t].s[n].b;
        v->seed = dm[t].seed + 1u;
        bell_trig(v, p, vel);
        for (k = 0; k < v->np; k++) v->inc[k] = pc_scale(v->inc[k], f);
        break;
    }
    case DM_PART: {
        pc_part_t *v = &dm[t].s[n].p;
        v->seed = dm[t].seed + 1u;
        part_trig(v, p, vel);
        v->lf += p16 * 65536 / 192;             /* (the keys: its pitch, in octaves) */
        break;
    }
    case DM_WEIRD: {
        pc_weird_t *v = &dm[t].s[n].w;
        v->seed = dm[t].seed + 1u;
        weird_trig(v, p, vel);
        v->base = (uint32_t)(((uint64_t)v->base * f) >> 16), v->sweep = (uint32_t)(((uint64_t)v->sweep * f) >> 16);
        break;
    }
    default: {
        fgm_hail_t *v = &dm[t].s[n].h;
        p[2] = (uint8_t)clamp((int32_t)p[2] + p16 * 341 / 1600, 0, 127);   /* (COLOR: 3.1 octaves in 127) */
        v->seed = dm[t].seed + 1u;
        hail_trig(v, p, vel);
        break;
    }
    }
}
static int dm_on(uint32_t t, uint32_t s)
{
    const dm_slot_t *v = &dm[t].s[s];
    switch (dm[t].md[s]) {
    case DM_MEMB: return v->m.on;
    case DM_HAIL: return v->h.on;
    case DM_KICK: return v->k.on;
    case DM_SNARE: return v->s.on;
    case DM_FMD: return v->f.on;
    case DM_BELL: return v->b.on;
    case DM_PART: return v->p.on;
    case DM_WEIRD: return v->w.on;
    default: return v->c.on;
    }
}
static int dm_wanted(uint32_t t) { return dm_req[t] || dm_on(t, 0) || dm_on(t, 1); }
static void dm_run(uint32_t t, int32_t *x, uint32_t n)
{
    uint32_t s, i, pk = 0, md;
    int32_t a = clamp(dm_v[t][3], 0, 127), lp = dm[t].sat_lp;
    int req = dm_req[t];
    if (req) dm_req[t] = 0, dm_hit(t, (uint32_t)req - 1u);
    for (s = 0; s < 2u; s++) {
        dm_slot_t *v = &dm[t].s[s];
        switch (dm[t].md[s]) {
        case DM_MEMB: memb_render(&v->m, x, n); break;
        case DM_HAIL: hail_render(&v->h, x, n); break;
        case DM_KICK: sweep_render(&v->k, x, n); break;
        case DM_SNARE: voice_render(&v->s, dm[t].lvl[s], x, n); break;
        case DM_FMD: fmd_render(&v->f, x, n); break;
        case DM_BELL: bell_render(&v->b, x, n); break;
        case DM_PART: part_render(&v->p, x, n); break;
        case DM_WEIRD: weird_render(&v->w, x, n); break;
        default: clap_render(&v->c, dm[t].lvl[s], x, n); break;
        }
    }
    md = dm[t].md[dm[t].cur];
    if (md == DM_MEMB || md == DM_KICK) {       /* (DRIVE, as FNTSMG-1's SATURA: on the kicks) */
        int32_t g = 4096 + a * 290, pres = a * 160;
        for (i = 0; i < n; i++) {
            int32_t y = fg_soft((int32_t)(((int64_t)x[i] * g) >> 12));
            lp += fg_mul15(y - lp, 3113);       /* ~700 Hz */
            x[i] = y + fg_mul15(y - lp, pres);
        }
        dm[t].sat_lp = lp;
    }
    for (i = 0; i < n; i++) x[i] = md == DM_HAIL ? x[i] * 2 : x[i] >> 1;   /* (level with the other sources) */
    if (env_mode[t] == EM_AHD) {                /* ENV AHD: ATTACK, HOLD, DECAY on the drum, a control step at a time */
        env_c_t c;
        int32_t e = dm_env[t].e;
        env_coef(&c, trk[t].env, t);
        for (i = 0; i < n; i += CTL) {
            int32_t e0 = e, j;
            if (dm_env[t].st == 1u) { e += c.atk; if (e >= 1 << 24) e = 1 << 24, dm_env[t].st = 2, dm_env[t].hold = c.hold; }
            else if (dm_env[t].st == 2u) { if (dm_env[t].hold) dm_env[t].hold--; else dm_env[t].st = 3; }
            else if (dm_env[t].st == 3u) e -= mulq16(e, c.adec);
            for (j = 0; j < (int32_t)CTL && i + (uint32_t)j < n; j++)
                x[i + (uint32_t)j] = (int32_t)(((int64_t)x[i + (uint32_t)j] * ((e0 + (e - e0) * j / (int32_t)CTL) >> 9)) >> 15);
        }
        dm_env[t].e = e;
    }
    for (i = 0; i < n; i += 16u) {
        uint32_t v = (uint32_t)(x[i] < 0 ? -x[i] : x[i]);
        if (v > pk) pk = v;
    }
    dm[t].pk = (uint16_t)(pk > 65535u ? 65535u : pk);
}
