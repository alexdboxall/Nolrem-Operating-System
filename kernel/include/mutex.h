#pragma once

#include <sem.h>

struct mutex {
    struct sem sem;
};

void InitStaticMutex(struct mutex* mtx);
void DestroyStaticMutex(struct mutex* mtx);