#pragma once

#include "CLIPDRAW/api.h"

void AcquireScheduler(void);
void ReleaseScheduler(void);
void Schedule(void);

void InitScheduler(void);
void IdleTask(void*);
bool IsSchedulingInitialised(void);

void PostMessageIrq(struct msg msg);
void ProcessIrqPostMessage(void);

void BeginNewThread(void);