#include <common.h>
#include <obj.h>
#include <file.h>
#include <spinlock.h>
#include <heap.h>
#include <log.h>
#include <vnode.h>
#include <sys/stat.h>
#include <dirent.h>

/**
 * Creates an open file from an open vnode. Open files are file descriptors, and
 * keeps track of seek position, ability to read/write and O_CLOEXEC. Currently,
 * the only flags stored here are O_NONBLOCK and O_APPEND.
 */
export struct file* CreateFile(
    struct vnode* node, int mode, int flags, bool can_read, bool can_write
) {    
	struct file* file = AllocHeap(sizeof(struct file));
    InitObject(file, OBJTYPE_FILE);
    
	file->node = node;
	file->can_read = can_read;
	file->can_write = can_write;
	file->initial_mode = mode;
	file->flags = flags;
	file->seek_position = 0;

    RefObject(node);

	return file;
}

static void CleanupFile(void* _file) {
    struct file* file = _file;
    
    if (IFTODT(file->node->stat.st_mode) == DT_FIFO) {
        // TODO:
        //BreakPipe(file->node);
    }

    DerefObject(file->node);
    FreeHeap(file);
}

void InitFile(void) {
    RegisterObjectType(OBJTYPE_FILE, CleanupFile);
}