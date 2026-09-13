#pragma once

#include <sem.h>

struct mutex {
    // Must go first, as we require sem/mutex cast to work
    struct sem sem;
};

void InitStaticMutex(struct mutex* mtx);
void DestroyStaticMutex(struct mutex* mtx);