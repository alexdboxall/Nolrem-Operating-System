#include <common.h>
#include <scheduler.h>
#include <spinlock.h>
#include <arch.h>
#include <log.h>
#include <thread.h>
#include <phys.h>
#include <vmm.h>
#include <heap.h>
#include <timer.h>
#include <panic.h>

extern uint64_t last_task_switch_time;

void LowPriorityBackgroundThread(void*) {
    BeginNewThread();
    SetThreadPriority(GetCurrentThread(), PRIORITY_IDLE);

    size_t spare_frame = 0;
    bool is_zeroed = false;
    bool done_heap_purge = false;

    while (true) {
        if (!ArchAreInterruptsEnabled()) {
            Panic(PANIC_FUCK_ME);
        }

        /* As we don't yet have a way to pre-empt, manually switch. */
        Schedule();
        ArchIdle();

        /* How long the idle task has been running for uninterrupted */
        uint64_t time_running_for_ns = (GetTimeSinceBoot() - last_task_switch_time);

        /* We've been idle for 50ms or more, can start doing more interesting things. */
        if (spare_frame == 0) {
            spare_frame = AllocPhys(true);

        } else if (!is_zeroed) {
            ZeroPhysPage(spare_frame);
            is_zeroed = true;

        } else {
            bool provided = ProvideZeroedPhys(spare_frame);
            if (provided) {
                spare_frame = 0;
                is_zeroed = false;
            } else {
                /* We'll try again next time around. */
            }
        }

        if (time_running_for_ns > MS_TO_NS(150)) {
            /* If we've been idle a good length of time, we can do more expensive operations */
            if (!done_heap_purge) KePurgeFreeAreasFromHeap();
            done_heap_purge = true;
        } else {
            done_heap_purge = false;
        }
    }
}
