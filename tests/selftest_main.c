/* selftest_main.c - console runner for runtime/h2_selftest.c (PC, headless). */
#include <stdio.h>
#include "h2_selftest.h"

int main(void)
{
    static char report[8192];
    int failed = h2_selftest(report, sizeof report);
    fputs(report, stdout);
    return failed ? 1 : 0;
}
