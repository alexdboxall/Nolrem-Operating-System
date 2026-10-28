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
#include <vfs.h>
#include <panic.h>

extern volatile uint64_t last_task_switch_time;

void LowPriorityBackgroundThread(void*) {
    BeginNewThread();
    SetThreadPriority(GetCurrentThread(), PRIORITY_IDLE);

    size_t spare_frame = 0;
    bool is_zeroed = false;
    bool done_heap_purge = false;
    bool done_vnode_purge = false;
    bool out_of_discards = false;
    while (true) {
        if (!ArchAreInterruptsEnabled()) {
            Panic(PANIC_FUCK_ME);
        }

        ArchIdle();
        Schedule();
        
        /* How long the idle task has been running for uninterrupted */
        uint64_t time_running_for_ns = (GetTimeSinceBoot() - last_task_switch_time);

        bool low_mem = IsMemoryRunningLow();

        /* If idle for 75ms sec or more, start getting some zeroed out pages ready */
        if (low_mem && spare_frame != 0) {
            // Probably free it entirely. then spare_frame = 0 and is_zeroed = false
        }
        (void) is_zeroed;
#if 1
        if (time_running_for_ns >= MS_TO_NS(75) && !low_mem) {
            if (spare_frame == 0) {
                spare_frame = AllocPhys(true);
                is_zeroed = false;
                
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
#endif

        /* If idle for 0.5 sec or more, toss out any empty pages in the heap, unless we're starting to run
         * low on memory, in which case we'll do it quicker */  
        (void) done_heap_purge;
#if 1
        if (time_running_for_ns >= (low_mem ? MS_TO_NS(100) : MS_TO_NS(500))) {
            if (!done_heap_purge) KePurgeFreeAreasFromHeap();
            done_heap_purge = true;
        } else {
            done_heap_purge = false;
        }
#endif

        (void) done_vnode_purge;
#if 1
        if (time_running_for_ns >= (low_mem ? MS_TO_NS(300) : MS_TO_NS(2000))) {
            if (!done_vnode_purge) PurgeOldVnodes();
            done_vnode_purge = true;
        } else {
            done_vnode_purge = false;
        }
#endif

        LogPrintf("%d KB free!\n", GetFreeMemory() / 1024);

        if (time_running_for_ns >= MS_TO_NS(5000)) {
            if (/*low_mem &&*/ !out_of_discards) {
                struct phys_page* retv = DiscardPage();
                out_of_discards = retv == NULL;
            }
            ArchIdle();
            ArchIdle();
            ArchIdle();
        } else {
            out_of_discards = false;
        }
    }
}
