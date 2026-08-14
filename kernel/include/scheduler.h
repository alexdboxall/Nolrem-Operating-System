#pragma once

void AcquireScheduler(void);
void ReleaseScheduler(void);
void Schedule(void);

void InitScheduler(void);
void IdleTask(void*);