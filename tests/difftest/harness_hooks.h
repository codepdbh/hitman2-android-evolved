/*
 * harness_hooks.h - included before cpu.h by BOTH the harness and the lifted
 * translation unit, so the two agree on how unimplemented instructions and
 * x87 stack misuse are reported.
 */
#ifndef H2_HARNESS_HOOKS_H
#define H2_HARNESS_HOOKS_H

/* An instruction the lifter cannot express: reported with its address. */
void h2_todo(unsigned va, const char *text);
#define RECOMP_TODO(va, text) h2_todo((va), (text))

/* x87 stack accounting. A function that reads a register the test vector did
 * not load (a CRT helper taking its argument in ST(0), run on an empty stack)
 * is not a valid run: real hardware raises a stack fault and loads a NaN,
 * while neither QEMU nor the lifted model does. Such vectors are detected
 * here and reported as inconclusive instead of being compared. */
extern int g_fp_depth, g_fp_bad;
#define H2_FPU_ON_PUSH(c)  ((void)((g_fp_depth >= 8 ? (g_fp_bad = 1) : 0), g_fp_depth++))
#define H2_FPU_ON_POP(c)   ((void)((g_fp_depth <= 0 ? (g_fp_bad = 1) : 0), g_fp_depth--))
#define H2_FPU_ON_ST(c, i) ((void)((i) >= g_fp_depth ? (g_fp_bad = 1) : 0))

#endif
