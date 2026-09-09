#include <common.h>
#include <scheduler.h>
#include <spinlock.h>
#include <arch.h>
#include <log.h>
#include <thread.h>

export void IdleTask(void*) {
    LogPrintf("IdleTask\n");
    BeginNewThread();
    SetThreadPriority(GetCurrentThread(), PRIORITY_IDLE);
    
    while (true) {
        /* As we don't yet have a way to pre-empt, manually switch. */
        Schedule();
        ArchIdle();
    }
}
