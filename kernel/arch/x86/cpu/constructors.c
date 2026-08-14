
#include <log.h>

typedef void initfunc_t(void);
extern initfunc_t* start_ctors[];
extern initfunc_t* end_ctors[];

__attribute__ ((constructor)) void DummyGlobalConstructor(void) {

}
 
void ArchCallGlobalConstructors() {
    for (initfunc_t** p = start_ctors; p != end_ctors; p++) {
        (*p)();
    }
}
