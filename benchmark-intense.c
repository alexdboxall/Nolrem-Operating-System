#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

static void* Allocate(size_t size) {
    void* ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Uh oh, my_malloc() returned NULL!\n");
        exit(1);
    }
    return ptr;
}

static void Free(void* ptr) {
    free(ptr);
}

struct allocation_group {
    size_t alloc_size;
    size_t number;
    size_t** pointers;
};

static struct allocation_group* alloc_table;
static int alloc_table_size = 0;

static struct allocation_group* AllocateGroup(void) {
    struct allocation_group* group = Allocate(sizeof(struct allocation_group));
    
    group->alloc_size = (rand() & 31) * 8;

    if (group->alloc_size == 0) {
        group->alloc_size = 8 << (rand() % 20);
        group->number = 1 + (rand() & 1);

    } else if ((rand() & 0x1F) == 0) {
        group->number = (rand() & 0x1FF) + 2;

    } else {
        group->number = 1;
    }

    group->pointers = Allocate(sizeof(size_t*) * group->number);
    for (size_t i = 0; i < group->number; ++i) {    
        group->pointers[i] = Allocate(group->alloc_size);
    }

    return group;
}

static void FreeGroup(struct allocation_group* group) {
    /* Try free some of them in a random order... */
    for (int i = 0; i < group->number / 2 + 1; ++i) {
        size_t rand_index = rand() % group->number;
        if (group->pointers[rand_index] != NULL) {
            Free(group->pointers[rand_index]);
            group->pointers[rand_index] = NULL;
        }
    }

    /* Free any we didn't get. */
    for (int i = 0; i < group->number; ++i) {
        if (group->pointers[i] != NULL) {
            Free(group->pointers[i]);
        }
    }

    Free(group);
}

#define NUM_GROUPS          10000
#define GROUPS_PER_STAGE     1000
#define NUM_STAGES           2000

static void AllocateStage(struct allocation_group** groups) {
    int done = 0;
    for (int i = 0; i < NUM_GROUPS && done < GROUPS_PER_STAGE; ++i) {
        if (groups[i] == NULL) {
            groups[i] = AllocateGroup();
            ++done;
        }
    }
}

static void FreeStage(struct allocation_group** groups) {
    for (int i = 0; i < GROUPS_PER_STAGE; ++i) {
        int index = rand() % NUM_GROUPS;
        if (groups[index] != NULL) {
            FreeGroup(groups[index]);
            groups[index] = NULL;
        }
    }
}

static void InterleaveAllocFreeGroup(struct allocation_group* group) {
    for (int i = 0; i < group->number * 2; ++i) {
        int rand_index = rand() % group->number;
        if (group->pointers[rand_index] == NULL) {
            if ((i & 3) == 0) {
                group->pointers[rand_index] = Allocate((rand() & 0xFFF) + 1);

            } else if ((i & 3) == 1) {
                group->pointers[rand_index] = Allocate((rand() & 0xFF) * 8 + 8);

            } else  {
                group->pointers[rand_index] = Allocate((rand() & 0xFF) + 8);

            }
            
        } else {
            Free(group->pointers[rand_index]);
            group->pointers[rand_index] = NULL;
        }
    }

    for (int i = 0; i < group->number; ++i) {
        if (group->pointers[i] != NULL) {
            Free(group->pointers[i]);
        }
    }
    Free(group);
}

static void InterleaveStage(struct allocation_group** groups) {
    int done = 0;
    for (int i = 0; i < 10000 && done < 100; ++i) {
        int index = rand() % NUM_GROUPS;
        if (groups[index] != NULL && groups[index]->number > 1) {
            InterleaveAllocFreeGroup(groups[index]);
            groups[index] = NULL;
            if (i & 1) {
                groups[index] = AllocateGroup();
            }
            ++done;
        }
    }
}

int main(int argc, char** argv) {
    srand(0);

    struct allocation_group* groups[NUM_GROUPS];
    for (int i = 0; i < NUM_GROUPS; ++i) {
        groups[i] = NULL;
    }

    for (int i = 0; i < NUM_STAGES; ++i) {
        if (i % 100 == 0) {
            printf("  %.1f%% done...            \r", i * 100.0 / ((double) NUM_STAGES));
            fflush(stdout);
        }
        int alloc_count = (rand() & 3) + 1;
        int free_count = (rand() & 3) + 1;
        while (alloc_count--) {
            AllocateStage(groups);
            InterleaveStage(groups);
        }
        while (free_count--) {
            FreeStage(groups);
            InterleaveStage(groups);
        }
    }

    printf("\nFinished.\n");
    return 0;
}
