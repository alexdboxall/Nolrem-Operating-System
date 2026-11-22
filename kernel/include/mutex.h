#pragma once

#include <sem.h>

struct mutex {
    // Must go first, as we require sem/mutex cast to work
    struct sem sem;
};

void KeInitStaticMutex(struct mutex* mtx);
void KeDestroyStaticMutex(struct mutex* mtx);