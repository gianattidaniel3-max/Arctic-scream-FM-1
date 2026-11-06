/* SPDX-License-Identifier: GPL-3.0-only
 * Drum models from FNTSMG-1 by fantasmogenesis (github.com/fantasmogenesis/fntsmg-1, commit 2e84d10), GPL-3.0:
 * motores/src/fg_dsp.h, kept as they are but for the names of their helpers. */
/* Bloques de DSP en punto fijo para los motores del FM-1.
 * El AC791N no tiene float por hardware: todo es entero. Las muestras viven en int32 con el rango
 * de un int16 (±32767) y margen arriba; los coeficientes en Q30, las amplitudes en Q30 o Q15. */

#include <stdint.h>
#include "fg_tables.h"
#include "fg_tables.inc"

#define FG_ONE30 (1 << 30)
#define FG_BLOCK 128                     /* muestras por bloque, como el I2S del FM-1 */

/* sin libc: el firmware no la tiene */
/* en el FM-1, lo grande que no hace falta en cero al arrancar va a la RAM grande (.pool, sin limpiar): .bss está casi llena */
#ifdef FG_DEVICE
#define FG_POOL __attribute__((section(".pool")))
#else
#define FG_POOL
#endif
static inline void fg_zero(void *p, unsigned bytes) { uint8_t *b = (uint8_t *)p; while (bytes--) *b++ = 0; }
static inline void fg_copy(void *d, const void *s, unsigned bytes)
{
    uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s;
    while (bytes--) *a++ = *b++;
}

static inline int32_t fg_mul30(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 30); }
static inline int32_t fg_mul15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }
static inline int32_t fg_clamp16(int32_t x) { return x > 32767 ? 32767 : x < -32767 ? -32767 : x; }

/* seno: fase de 32 bits, 11 bits de índice + 16 de interpolación */
static inline int32_t fg_sin(uint32_t ph)
{
    uint32_t i = ph >> 21;
    int32_t f = (int32_t)((ph >> 5) & 0xFFFF), a = FG_SIN[i], b = FG_SIN[i + 1];
    return a + (((b - a) * f) >> 16);
}

static inline uint32_t fg_rand(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static inline int32_t fg_noise(uint32_t *s) { return (int32_t)(fg_rand(s) >> 16) - 32768; }

/* 2^x con x en Q16 (0..14): resultado en Q16 */
static inline uint32_t fg_exp2_q16(uint32_t x)
{
    uint32_t ip = x >> 16, fr = x & 0xFFFF, i = fr >> 8, f = fr & 0xFF;
    uint32_t a = FG_EXP2[i], b = FG_EXP2[i + 1];
    return (a + (((b - a) * f) >> 8)) << ip;
}

/* perilla de cantidad: 0 = nada; 1..127 sube parejo en dB, de -24 dB a `top` (Q15). Así cada tramo
 * del giro cambia lo mismo de oído, en vez de que todo pase en el primer cuarto. */
static inline int32_t fg_amount(unsigned knob, int32_t top)
{
    if (!knob) return 0;
    return (int32_t)(((int64_t)top * fg_exp2_q16(knob * 4u * 65536u / 127u)) >> 20);
}

/* parameter locks: los valores del paso pisan a los de la pista donde el bit de la máscara está prendido */
static inline void fg_apply_locks(uint8_t *dst, const uint8_t *base, const uint8_t *ov, unsigned mask, unsigned n)
{
    for (unsigned i = 0; i < n; i++) dst[i] = (ov && ((mask >> i) & 1u)) ? ov[i] : base[i];
}

/* nota MIDI (con fracción Q8) -> incremento de fase. C-1 = 8.1758 Hz */
static inline uint32_t fg_note_inc(int32_t note_q8)
{
    if (note_q8 < 0) note_q8 = 0;
    uint32_t f_q8 = (uint32_t)(((uint64_t)2093u * fg_exp2_q16((uint32_t)((note_q8 * 65536) / (12 * 256)))) >> 16);
    return (uint32_t)(((uint64_t)f_q8 * FG_INC_HZ) >> 16);
}

/* coeficiente de un filtro de estado variable que corre DOS veces por muestra: estable hasta ~15 kHz */
static inline int32_t fg_svf2_f(uint32_t fc_hz) { return 2 * fg_sin((uint32_t)(((uint64_t)(fc_hz << 8) * FG_INC_HZ) >> 18)); }
typedef struct { int32_t lo, bp, hi; } fg_svf_t;
/* un paso del filtro (dos iteraciones); devuelve en s->lo, s->bp, s->hi */
static inline void fg_svf2(fg_svf_t *s, int32_t x, int32_t f, int32_t q)
{
    for (int k = 0; k < 2; k++) {
        s->lo += (int32_t)(((int64_t)f * s->bp) >> 15);
        s->hi = x - s->lo - (int32_t)(((int64_t)q * s->bp) >> 15);
        s->bp += (int32_t)(((int64_t)f * s->hi) >> 15);
        s->lo = s->lo > 65534 ? 65534 : s->lo < -65534 ? -65534 : s->lo;
        s->bp = s->bp > 65534 ? 65534 : s->bp < -65534 ? -65534 : s->bp;
    }
}

/* frecuencia en Hz Q8 -> incremento de fase */
static inline uint32_t fg_inc(uint32_t f_q8) { return (uint32_t)(((uint64_t)f_q8 * FG_INC_HZ) >> 16); }

/* coeficiente de caída para el valor de una perilla (0..127) mapeado entre dos índices de FG_DECAY */
static inline int32_t fg_decay(uint32_t knob, uint32_t lo, uint32_t hi)
{
    return FG_DECAY[lo + (knob * (hi - lo)) / 127u];
}
/* el mismo coeficiente elevado a 16: para avanzar una envolvente un tramo de 16 muestras */
static inline int32_t fg_pow16(int32_t k)
{
    for (int i = 0; i < 4; i++) k = fg_mul30(k, k);
    return k;
}

/* plegado triangular infinito de una señal (rango ±32767 después de la ganancia) */
static inline int32_t fg_fold(int32_t v)
{
    uint32_t u = (uint32_t)(v + 32768) & 0x1FFFFu;
    return u < 65536u ? (int32_t)u - 32768 : 98303 - (int32_t)u;
}

/* saturación suave cúbica: lineal cerca de 0, se dobla hacia ±32767 */
static inline int32_t fg_soft(int32_t x)
{
    if (x > 49151) return 32767;
    if (x < -49151) return -32767;
    int64_t c = (int64_t)x * x * x;                   /* x - x^3 / (3 k^2), k = 49151, sin dividir */
    return x - (int32_t)((c * 9710) >> 46);
}
