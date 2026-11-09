/*
 * h2_x87.h - x87 behaviour the stock PCRecomp cpu.h does not model, needed by
 * recomp/h2_lifter.py. Included at the end of the generated cpu.h, so CPU,
 * fst() and the wr* helpers are already declared. See h2_lifter.py for the
 * differential-test evidence behind each helper.
 */
#ifndef H2_X87_H
#define H2_X87_H

#include <math.h>

#define H2_FPU_CW_DEFAULT 0x027Fu   /* what a Win32 process starts with: 53-bit precision, round to nearest */

/* Round to an integral value under the control word's RC field (bits 10-11).
 * The host is left in its default round-to-nearest mode, which the project
 * never changes, so nearbyint() is round-half-even exactly like RC=00. */
static inline double h2_round_rc(const CPU *c, double v)
{
    switch ((c->fpu_cw >> 10) & 3u) {
    case 0:  return nearbyint(v);
    case 1:  return floor(v);
    case 2:  return ceil(v);
    default: return trunc(v);
    }
}

/* fist/fistp. A value that does not fit (or a NaN) stores the "integer
 * indefinite" - the most negative integer - as the hardware does with the
 * invalid-operation exception masked. Written out because a C float->int cast
 * of an out-of-range value is undefined and differs between x86 and ARM64. */
static inline void h2_fist16(CPU *c, uint32_t a, double v)
{
    double r = h2_round_rc(c, v);
    int16_t i = (r >= -32768.0 && r <= 32767.0) ? (int16_t)r : (int16_t)0x8000;
    wr16(a, (uint16_t)i);
}
static inline void h2_fist32(CPU *c, uint32_t a, double v)
{
    double r = h2_round_rc(c, v);
    int32_t i = (r >= -2147483648.0 && r <= 2147483647.0) ? (int32_t)r : (int32_t)0x80000000u;
    wr32(a, (uint32_t)i);
}
static inline void h2_fist64(CPU *c, uint32_t a, double v)
{
    double r = h2_round_rc(c, v);
    /* 2^63 is exactly representable; anything >= it does not fit. */
    int64_t i = (r >= -9223372036854775808.0 && r < 9223372036854775808.0)
              ? (int64_t)r : (int64_t)0x8000000000000000ull;
    wr64(a, (uint64_t)i);
}

/* fnstsw: condition bits from the model, TOP from the stack pointer. */
static inline uint16_t h2_fpu_sw(const CPU *c)
{
    return (uint16_t)((c->fpu_sw & ~0x3800u) | (((uint32_t)c->fpu_top & 7u) << 11));
}

/* String instructions step ESI/EDI by the element size, backwards when DF=1
 * (`std`). MSVC's strrchr scans backwards this way. */
static inline uint32_t h2_dstep(const CPU *c, uint32_t n)
{
    return c->df ? (uint32_t)0 - n : n;
}

/* Power-on state for a new guest thread's CPU. */
static inline void h2_cpu_reset_fpu(CPU *c)
{
    c->fpu_cw = H2_FPU_CW_DEFAULT; c->fpu_sw = 0; c->fpu_top = 0;
}

#endif /* H2_X87_H */
