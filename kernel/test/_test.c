
#include <common.h>
#include <assert.h>

extern void RunSemAbandonTests(void);
extern void RunMsgboxTests(void);
extern void RunTimerTests(void);
extern void RunSemTimeoutTests(void);

extern void RunSfnTests(void);

export void RunTestSuite(void) {
#ifndef NDEBUG
    /* Core kernel tests */
    RunTimerTests();
    RunSemTimeoutTests();
    RunSemAbandonTests();
    RunMsgboxTests();

    /* FAT tests */
    RunSfnTests();
#endif
}