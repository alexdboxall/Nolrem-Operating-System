#include <common.h>
#include <harderror.h>
#include <log.h>
#include <stdatomic.h>

static _Atomic(ke_hard_error_handler_t) hard_error_handler = NULL;

export void KeSetHardErrorHandler(ke_hard_error_handler_t handler) {
    atomic_store(&hard_error_handler, handler);
}

export int KeHardError(int herrno) {
    ke_hard_error_handler_t handler = atomic_load(&hard_error_handler);
    if (handler == NULL) {
        KeLogPrintf("Hard error %d, and nothing is able to report it.\n", herrno);
        return herrno != 0 ? herrno : -1;
    }
    return handler(herrno);
}
