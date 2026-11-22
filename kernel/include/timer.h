#pragma once

#include <common.h>

uint64_t KeGetTimeSinceBoot(void);
void KeSleepUntil(uint64_t ns);
void KeSleepNano(uint64_t ns);
void KeSleepMilli(uint64_t ms);

void KeAdvanceTimer(uint64_t ns);
void KeInitTimer(void);

struct thread;
bool KeAddThreadToSleepQueue(bool relative, uint64_t ns, bool need_sched_lock);
void KeCancelTimer(struct thread* thr);