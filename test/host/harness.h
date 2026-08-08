#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>

static int gTestsRun = 0;
static int gTestsFailed = 0;
static int gCurrentTestFailed = 0;

#define RUN_TEST(fn)                          \
    do {                                      \
        gCurrentTestFailed = 0;               \
        gTestsRun++;                          \
        fn();                                 \
        if (gCurrentTestFailed) {             \
            gTestsFailed++;                   \
            printf("FAIL  %s\n", #fn);        \
        } else {                              \
            printf("ok    %s\n", #fn);        \
        }                                     \
    } while (0)

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("  check failed at %s:%d: %s\n",                    \
                   __FILE__, __LINE__, #cond);                         \
            gCurrentTestFailed = 1;                                    \
        }                                                              \
    } while (0)

#define CHECK_EQ_INT(a, b)                                             \
    do {                                                               \
        long long _va = (long long) (a);                               \
        long long _vb = (long long) (b);                               \
        if (_va != _vb) {                                              \
            printf("  check failed at %s:%d: %s (%lld) != %s (%lld)\n",\
                   __FILE__, __LINE__, #a, _va, #b, _vb);              \
            gCurrentTestFailed = 1;                                    \
        }                                                              \
    } while (0)

static int test_summary(void) {
    printf("%d tests, %d failed\n", gTestsRun, gTestsFailed);
    return gTestsFailed != 0;
}

#endif
