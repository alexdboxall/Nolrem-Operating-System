#include <common.h>
#include <scheduler.h>
#include <spinlock.h>
#include <arch.h>

export void IdleTask(void*) {
    while (true) {
        ArchIdle();
    }
}