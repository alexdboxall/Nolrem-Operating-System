#pragma once

#include <stddef.h>

void InitLog(void);
void LogCharacter(char c);
void LogString(char* s);
void LogInt(int i);
void LogHex(size_t hx);
void LogStringAndHexLine(char* s, size_t hx);