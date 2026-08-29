#include "../CLIPDRAW/api.h"
#include "../CLIPDRAW/clipdraw_internal.h"

struct window {
    // LOCAL COORDS
    struct rect local_win_bound;
    struct rect local_client_bound;

    // GLOBAL
    struct region win_rgn;
    struct region client_rgn;
    struct region vis_rgn;
    struct region dirty_rgn;
    struct point global_offset_cached;

    struct window* next_sibling;
    struct window* first_child;
    struct window* parent;
};

struct window_class {
    char* name;
    winproc_t proc;
};

struct dc* WmGetDC(void);
int WmReturnDC(struct dc* dc);
struct dc* WmBeginPaint(struct window* win);
void WmEndPaint(struct window* win, struct dc* dc);