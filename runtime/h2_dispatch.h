#ifndef H2_DISPATCH_H
#define H2_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#include "recomp_prelude.h"      /* CPU */

#ifdef __cplusplus
extern "C" {
#endif

#define H2_TRAIL_LEN 64

/* Called for a guest address with no lifted body. Returns non-zero if it
 * handled the transfer (an imported function). */
typedef int (*h2_import_hook)(CPU *c, uint32_t guest_va);
void h2_dispatch_set_import_hook(h2_import_hook hook);

/* Stop the guest (H2_STOP_BUDGET) after this many dispatches; 0 = unlimited. */
void h2_dispatch_set_budget(uint64_t max_dispatches);
uint64_t h2_dispatch_count(void);
size_t h2_dispatch_function_count(void);
int h2_dispatch_has(uint32_t guest_va);

/* The most recent dispatch targets, oldest first. A ring, not a single
 * "last" value: after a callee returns, "last" names the callee. */
size_t h2_dispatch_trail(uint32_t *out, size_t max);

#ifdef __cplusplus
}
#endif
#endif
