#pragma once

#include "CLIPDRAW/api.h"

void KeAcquireScheduler(void);
void KeReleaseScheduler(void);
void KeSchedule(void);

void KeInitScheduler(void);
void KeLowPriorityBackgroundThread(void*);
bool KeIsSchedulingInitialised(void);

void KePostMessageIrq(struct msg msg);
void KeProcessIrqPostMessage(void);

void KeBeginNewThread(void);