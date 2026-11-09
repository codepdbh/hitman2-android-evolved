/*
 * recomp_prelude.h - first include of every generated translation unit.
 *
 * Fixes how lifted code reports what it cannot do, then pulls in the generated
 * portable cpu.h (build/generated/include) and declares the dispatcher.
 */
#ifndef H2_RECOMP_PRELUDE_H
#define H2_RECOMP_PRELUDE_H

/* An instruction the lifter could not express. The runtime logs the guest
 * address and mnemonic and stops the guest thread; it never returns as if the
 * instruction had executed. Defined in the runtime, not here. */
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn, cold))
#endif
void h2_todo(unsigned guest_va, const char *text);
#ifdef __cplusplus
}
#endif
#define RECOMP_TODO(va, text) h2_todo((va), (text))

#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Transfer control to the lifted body at a guest address (call or tail jump).
 * An address with no lifted body is a logged, fatal runtime error. */
void dispatch(CPU *c, uint32_t guest_va);
void dispatch_jmp(CPU *c, uint32_t guest_va);
#ifdef __cplusplus
}
#endif

#endif /* H2_RECOMP_PRELUDE_H */
