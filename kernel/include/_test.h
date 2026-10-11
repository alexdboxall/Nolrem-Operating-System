#pragma once

#include <common.h>
#include <log.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#define RUN_TEST(fn)                                       \
    do {                                                   \
        LogPrintf("\x01 Running " #fn " (" __FILE_NAME__ ")\n\x02");      \
        fn();                                              \
    } while (0)

/*
 * The condition is always evaluated, even if assert() is compiled out, because
 * many checks contain calls with side effects (ReleaseSem, AcquireSem, ...).
 */
#define CHECK(cond)                 \
    do {                            \
        int _check_ok = !!(cond);   \
        assert(_check_ok);          \
        (void) _check_ok;           \
    } while (0)

#define TRACE(...) LogPrintf(__VA_ARGS__)

void RunTestSuite(void);
