#pragma once

#include <common.h>

uint64_t GetTimeSinceBoot(void);
void SleepUntil(uint64_t ns);
void SleepNano(uint64_t ns);
void SleepMilli(uint64_t ms);

void AdvanceTimer(uint64_t ns);
void InitTimer(void);

struct thread;
bool AddThreadToSleepQueue(bool relative, uint64_t ns, bool need_sched_lock);
void CancelTimer(struct thread* thr);