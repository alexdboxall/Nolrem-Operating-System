#pragma once

#include <common.h>

uint64_t GetTimeSinceBoot(void);

void AdvanceTimer(uint64_t ns);
void InitTimer(void);