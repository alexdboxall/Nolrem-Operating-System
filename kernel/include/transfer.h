#pragma once

#include <common.h>

enum transfer_type { 
    TRANSFER_INTRA_KERNEL,
    TRANSFER_USERMODE,
};

enum transfer_direction {
    TRANSFER_READ,
    TRANSFER_WRITE,
};

/*
* A data structure for performing file read and write operations, potentially
* between the kernel and the user.
*/
struct transfer {
    void* address;
    uint64_t length_remaining;    /* In bytes. Will be modified on copying */
    uint64_t offset;              /* In bytes. Will be modified on copying */

    enum transfer_direction direction;
    enum transfer_type type;

    bool blockable;               /* true by default, KeReadFile/KeWriteFile sets
                                   * to no if O_NONBLOCK is in the openfile */
};


int KePerformTransfer(void* trusted_buffer, struct transfer* untrusted_buffer, uint64_t len);
int KeRevertTransfer(struct transfer* untrusted, uint64_t len);

/*
* max_length of 0 means unbounded
*/
int KeWriteStringToUsermode(const char* trusted_string, char* untrusted_buffer, uint64_t max_length);
int KeReadStringFromUsermode(char* trusted_buffer, const char* untrusted_string, uint64_t max_length);

int KeWriteWordToUsermode(size_t* location, size_t value);
int KeReadWordFromUsermode(size_t* location, size_t* output);

struct transfer KeCreateKernelTransfer(void* addr, uint64_t length, uint64_t offset, int direction);
struct transfer KeCreateTransferWritingToUser(void* untrusted_addr, uint64_t length, uint64_t offset);
struct transfer KeCreateTransferReadingFromUser(const void* untrusted_addr, uint64_t length, uint64_t offset);
