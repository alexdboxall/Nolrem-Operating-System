#pragma once

#include <stddef.h>
#include <stdint.h>

struct msgbox;

void InitMessageBox(void);
struct msgbox* CreateMessageBox(size_t message_size, size_t max_count);
int KePostMessage(struct msgbox* mbox, const void* msg, int64_t timeout);
int KeGetMessage(struct msgbox* mbox, void* msg, int64_t timeout);
int KePeekMessage(struct msgbox* mbox, void* msg, int64_t timeout);
int KeTryReplaceOrAdd(struct msgbox* mbox, const void* compare_to,
                             const void* replace_with, int64_t timeout);
int KeGetMessageFromMany(struct msgbox** mboxes, int count, void* msg, int64_t timeout, bool remove, int* num_out);