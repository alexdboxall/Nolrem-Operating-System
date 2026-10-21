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

        bool low_mem = IsMemoryRunningLow();

        /* If idle for 300ms sec or more, start getting some zeroed out pages ready */
        if (time_running_for_ns >= MS_TO_NS(300) && !low_mem) {
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
        }

        /* If idle for 2 sec or more, toss out any empty pages in the heap, unless we're starting to run
         * low on memory, in which case we'll do it quicker */
        if (time_running_for_ns >= (low_mem ? MS_TO_NS(500) : MS_TO_NS(2000))) {
            if (!done_heap_purge) KePurgeFreeAreasFromHeap();
            done_heap_purge = true;
        } else {
            done_heap_purge = false;
        }
    }
}
