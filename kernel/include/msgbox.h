#pragma once

#include <stddef.h>
#include <stdint.h>

struct msgbox;

void KeInitMessageBox(void);

/*
 * The system message box carries the kernel's own messages (SYSMSG_*): input
 * events, low memory notifications, ... The kernel creates it, and whichever
 * component runs the user interface (the window manager) drains it. Posting to
 * it with a timeout never blocks: if it is full the system is not keeping up
 * with the hardware, and that is fatal.
 */
void KeInitSystemMessageBox(void);
struct msgbox* KeGetSystemMessageBox(void);
struct msgbox* KeCreateMessageBox(size_t message_size, size_t max_count);
int KePostMessage(struct msgbox* mbox, const void* msg, int64_t timeout);
int KeGetMessage(struct msgbox* mbox, void* msg, int64_t timeout);
int KePeekMessage(struct msgbox* mbox, void* msg, int64_t timeout);
int KeTryReplaceOrAdd(struct msgbox* mbox, const void* compare_to,
                             const void* replace_with, int64_t timeout);
int KeGetMessageFromMany(struct msgbox** mboxes, int count, void* msg, int64_t timeout, bool remove, int* num_out);