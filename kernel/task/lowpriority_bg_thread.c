#include <common.h>
#include <scheduler.h>
#include <spinlock.h>
#include <arch.h>
#include <log.h>
#include <thread.h>
#include <phys.h>
#include <vmm.h>
#include <panic.h>

void LowPriorityBackgroundThread(void*) {
    BeginNewThread();
    SetThreadPriority(GetCurrentThread(), PRIORITY_IDLE);

    size_t spare_frame = 0;
    bool is_zeroed = false;

    while (true) {
        if (!ArchAreInterruptsEnabled()) {
            Panic(PANIC_FUCK_ME);
        }

        /* As we don't yet have a way to pre-empt, manually switch. */
        Schedule();
        ArchIdle();

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
}
