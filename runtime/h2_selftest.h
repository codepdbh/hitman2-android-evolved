#ifndef H2_SELFTEST_H
#define H2_SELFTEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run the runtime self-test. Writes a human-readable report (one line per
 * check) into `report`. Returns the number of failed checks (0 = all passed). */
int h2_selftest(char *report, size_t report_size);

#ifdef __cplusplus
}
#endif
#endif
