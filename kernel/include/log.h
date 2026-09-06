#pragma once

#include <stddef.h>

void InitLog(void);
void LogString(char* s);
void LogStringAndHexLine(char* s, size_t hx);
void LogPrintf(const char* format, ...);
