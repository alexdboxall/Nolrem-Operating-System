#include <spinlock.h>
#include <log.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <heap.h>
#include <fcntl.h>
#include <obj.h>
#include <vfs.h>
#include <stdatomic.h>
#include <file.h>
#include <vnode.h>
#include <mutex.h>
#include <aio.h>

/*
* Try not to have non-static functions that return in any way a struct vnode*, as it
* probably means you need to use the reference/dereference functions.
*/

#define MAX_COMPONENT_LENGTH	128
#define MAX_PATH_LENGTH			2000
#define MAX_LOOP				5

/*
* A structure for mounted devices and filesystems.
*/
struct mounted_file {
	struct mounted_file* next;

	/* The vnode representing the device / root directory of a filesystem */
	struct file* node;

	/* What the device / filesystem mount is called */
	char name[];
};

static struct mutex* mount_list_lock;
static struct mounted_file* mounted_file_list;
static _Atomic int next_dev_id = 1;	/* don't start at 0 in case that means 'invalid' */

export int KeNextDevId(void) {
	return atomic_fetch_add(&next_dev_id, 1);
}

void KeInitVfs(void) {
    mount_list_lock = KeCreateMutex();
	mounted_file_list = NULL;
}

static bool KeIsRelative(const char* path) {
	for (int i = 0; path[i]; ++i) {
		if (path[i] == ':') {
			return false;
		}
	}
	return true;
}

static int KeCheckValidComponentName(const char* name) {	
	if (name[0] == 0) {
		return EINVAL;
	}

	for (int i = 0; name[i]; ++i) {
		char c = name[i];

		if (c == '/' || c == '\\' || c == ':') {
			return EINVAL;
		}
	}

	return 0;
}

struct mounted_file* KeGetMountPointFromName(const char* name) {
	if (mounted_file_list == NULL) {
		return NULL;
	}
	struct mounted_file* curr = mounted_file_list;
	while (curr) {
		if (!strcmp(curr->name, name)) {
			return curr;
		}
		curr = curr->next;
	}
	return NULL;
}

static int KeDoesMountPointExist(const char* name) {
    if (KeGetMountPointFromName(name) != NULL) {
        return EEXIST;
    }
    return 0;
}

export int KeMount(struct vnode* node, const char* name) {
    if (name == NULL || node == NULL) {
		return EINVAL;
	}

	if (strlen(name) >= MAX_COMPONENT_LENGTH) {
		return ENAMETOOLONG;
	}

    int status = KeCheckValidComponentName(name);
	if (status != 0) {
		return status;
	}

    status = KeAcquireMutex(mount_list_lock, TIMEOUT_INFINITE);
	if (status != 0) {
		return status;
	}

    if (KeDoesMountPointExist(name) == EEXIST) {
        KeReleaseMutex(mount_list_lock);
        return EEXIST;
    }

    struct mounted_file* mount = AllocHeap(sizeof(struct mounted_file) + strlen(name) + 1);
	if (mount == NULL) {
		KeReleaseMutex(mount_list_lock);
		return ENOMEM;
	}
	strcpy(mount->name, name);
    mount->node = KeCreateFile(node, 0, 0, true, true);
	mount->next = mounted_file_list;
	mounted_file_list = mount;
	KeRefObject(mount->node);
    KeReleaseMutex(mount_list_lock);
    return 0;
}

export int KeUnmount(const char* name) {
    if (name == NULL) {
		return EINVAL;
	}

	if (KeCheckValidComponentName(name) != 0) {
		return EINVAL;
	}

	int res = KeAcquireMutex(mount_list_lock, TIMEOUT_INFINITE);
	if (res != 0) {
		return res;
	}

	/*
	* Scan through the mount table for the device
	*/ 
    struct mounted_file* actual = KeGetMountPointFromName(name);
    if (actual == NULL) {
        KeReleaseMutex(mount_list_lock);
        return ENODEV;
    }

    KeDerefObject(actual->node);

    if (actual == mounted_file_list) {
		mounted_file_list = mounted_file_list->next;
	} else {
		struct mounted_file* curr = mounted_file_list;
		struct mounted_file* prev = NULL;
		while (curr) {
			if (curr == actual) {
				prev->next = curr->next;
				break;
			}
			prev = curr;
			curr = curr->next;
		}
	}
	
	FreeHeap(actual->name);
	FreeHeap(actual);
    KeReleaseMutex(mount_list_lock);
    return 0;
}

/*
* Given a filepath, and a pointer to an index within that filepath (representing where
* start searching), copies the next component into an output buffer of a given length.
* The index is updated to point to the start of the next component, ready for the next call.
*
* This also handles duplicated and trailing forward slashes.
*/
static int KeGetPathComponent(const char* path, int* ptr, char* out, int max_len, char delimiter) {
	int i = 0;
	out[0] = 0;

	while (path[*ptr] && path[*ptr] != delimiter) {
		if (i >= max_len - 1) {
			return ENAMETOOLONG;
		}

		out[i++] = path[*ptr];
		out[i] = 0;
		(*ptr)++;
	}

	/*
	* Skip past the delimiter (unless we are at the end of the string),
	* as well as any trailing slashes (which could be after a slash delimiter, 
	* or after a colon). 
	*/
	if (path[*ptr]) {
		do {
			(*ptr)++;
		} while (path[*ptr] == '/');
	}

	/*
	* Ensure that there are no colons or backslashes in the filename itself.
	*/
	return KeCheckValidComponentName(out);
}

static int KeGetFinalPathComponent(const char* path, char* out, int max_len) {
	int path_ptr = 0;
	int status;

	// @@@ TODO: 
	if (!KeIsRelative(path)) {
		status = KeGetPathComponent(path, &path_ptr, out, max_len, ':');
		if (status) {
			return status;
		}
	}

	while (path_ptr < (int) strlen(path)) {
		status = KeGetPathComponent(path, &path_ptr, out, max_len, '/');
		if (status) {
			return status;
		}
	}

	return 0;
}


/*
* Given an absolute or relative filepath, returns the vnode representing
* the file, directory or device. 
*
* Should be used carefully, as the reference count is incremented.
*/
static int KeGetVnodeFromPath(const char* path, struct vnode** out, bool want_parent) {
	if (strlen(path) == 0) {
		return EINVAL;
	}
	if (strlen(path) >= MAX_PATH_LENGTH) {
		return ENAMETOOLONG;
	}

	int path_ptr = 0;
	char component_buffer[MAX_COMPONENT_LENGTH];

	bool relative = KeIsRelative(path);

	int err;
	struct vnode* current_vnode = NULL;
	if (relative) {
		/*struct process* prcss = GetProcess();
		if (prcss == NULL) {
			return ENODEV;
		}
		current_vnode = prcss->cwd;*/

	} else {
		err = KeGetPathComponent(path, &path_ptr, component_buffer, MAX_COMPONENT_LENGTH, ':');
		if (err != 0) {
			return err;
		}

		err = KeAcquireMutex(mount_list_lock, TIMEOUT_INFINITE);
		if (err != 0) {
			return err;
		}
		struct mounted_file* mount = KeGetMountPointFromName(component_buffer);
		KeReleaseMutex(mount_list_lock);
		struct file* current_file = mount == NULL ? NULL : mount->node;
		if (current_file == NULL) {
			return ENODEV;
		}
		current_vnode = current_file->node;
	}
	
	if (current_vnode == NULL) {
		return ENODEV;
	}

	/*
	* This will be dereferenced either as we go through the loop, or
	* after a call to vfs_close (this function should only be called 
	* by vfs_open).
	*/
	KeRefObject(current_vnode);

	char component[MAX_COMPONENT_LENGTH + 1];

	/*
	* Iterate over the rest of the path.
	*/
	while (path_ptr < (int) strlen(path)) {
		int status = KeGetPathComponent(path, &path_ptr, component, MAX_COMPONENT_LENGTH, '/');
		if (status != 0) {
			KeDerefObject(current_vnode);
			return status;
		}

		if (!strcmp(component, ".")) {
			/*
			* This doesn't change where we point to.
			*/
			continue;
		} 

		/* 
		 * No need for ".." here, the filesystem itself handles that. This is
		 * needed for relative directories to work - as only the filesytem
		 * itself can get to the parent from merely a vnode.
		 */

		/*
		* Use a seperate pointer so that both inputs don't point to the same
		* location. vnode_follow either increments the reference count or creates
		* a new vnode with a count of one.
		*/
		struct vnode* next_vnode = NULL;
		status = VnodeFollow(current_vnode, &next_vnode, component);
		if (status != 0) {
			KeDerefObject(current_vnode);
			return status;
		}	
		current_vnode = next_vnode;
	}

	if (want_parent) {
		struct vnode* parent;
		int status = VnodeFollow(current_vnode, &parent, "..");
		KeDerefObject(current_vnode);
		if (status != 0) {
			return status;
		}
		*out = parent;

	} else {
		*out = current_vnode;
	}

	return 0;
}

export int KeRemoveFileOrDirectory(const char* path, bool rmdir) {
	struct vnode* node;
	int res = KeGetVnodeFromPath(path, &node, false);
	if (res != 0) {
		return res;
	}

	bool is_dir = IFTODT(node->stat.st_mode) == DT_DIR;
	if (rmdir && !is_dir) return ENOTDIR;
	if (!rmdir && is_dir) return EISDIR;

	res = node->stat.st_nlink > 0 ? VnodeUnlink(node) : ENOENT;

	KeDerefObject(node);
	return res;
}

export int KeOpenFile(const char* path, int flags, mode_t mode, struct file** out) {
 	if (path == NULL || out == NULL || strlen(path) <= 0) {
		return EINVAL;
	}

    int status;
	struct vnode* node;
    status = KeGetVnodeFromPath(path, &node, false);

    if (flags & O_CREAT) {
		if (status == ENOENT) {
			/*
			* Get the parent folder.
			*/
			status = KeGetVnodeFromPath(path, &node, true);
			if (status) {
				return status;
			}
			
			char name[MAX_COMPONENT_LENGTH + 1];
			status = KeGetFinalPathComponent(path, name, MAX_COMPONENT_LENGTH);
			if (status) {
				return status;
			}

			struct vnode* child;
			status = VnodeCreate(node, &child, name, flags, mode);
			KeDerefObject(node);

			if (status) {
				return status;
			}

			node = child;

		} else if (flags & O_EXCL) {
			/*
			 * The file already exists (as we didn't get ENOENT), but we were 
			 * passed O_EXCL so we must give an error. If O_EXCL isn't passed, 
			 * then O_CREAT will just open the existing file.
			 */
			return EEXIST;
		}

    } else if (status != 0) {
		return status;
    }

	status = VnodeCheckOpen(node, flags & (O_ACCMODE | O_NONBLOCK));
    if (status) {
		KeDerefObject(node);
		return status;
	}

	bool can_read = (flags & O_ACCMODE) != O_WRONLY;
	bool can_write = (flags & O_ACCMODE) != O_RDONLY;

	if (IFTODT(node->stat.st_mode) == DT_DIR && can_write) {
		/*
		* You cannot write to a directory - this also prevents truncation.
		*/
		KeDerefObject(node);
		return EISDIR;
	}

	if ((flags & O_TRUNC) && IFTODT(node->stat.st_mode) == DT_REG) {
		if (can_write) {
			struct aio a;
			status = KeCreateTruncateIO(&a, node, 0, NULL, NULL);
			if (status) {
				return status;
			}
			status = KeWaitIO(&a, TIMEOUT_INFINITE);
			if (status) {
				return status;
			}
		} else {
			return EINVAL;
		}
	}

	// TODO: may need to actually have a VnodeOpOpen, for things like FatFS.

	node->flags = flags & (O_NONBLOCK | O_APPEND);
	*out = KeCreateFile(node, mode, flags & O_CLOEXEC, can_read, can_write);
    return 0;
}

static void KeFileAccessAsyncCallback(void* _aio) {
	struct aio* aio = _aio;
	struct aio* subaio = aio->sub;

	aio->transfer = subaio->transfer;
	KeSignalDoneIO(aio, subaio->result_code);

	FreeHeap(subaio);
}

static int KeFileAccess(struct aio* aio, bool write) {
	if (aio == NULL) {
		return EINVAL;
	}
	struct transfer* io = &aio->transfer;
	struct file* file = aio->file;
    if (io == NULL || io->address == NULL || file == NULL || file->node == NULL) {
		return EINVAL;
	}
    if ((!write && !file->can_read) || (write && !file->can_write)) {
        return EBADF;
    }
	
	io->blockable = !(file->node->flags & O_NONBLOCK);

	struct aio* subaio = AllocHeap(sizeof(struct aio));
	if (subaio == NULL) {
		return ENOMEM;
	}
	KeCreateIO(subaio, file->node, *io, KeFileAccessAsyncCallback, (void*) aio);
	aio->sub = subaio;
	return (write ? VnodeWrite : VnodeRead)(subaio);
}

export int KeReadFile(struct aio* aio) {
	return KeFileAccess(aio, false);
}

export int KeWriteFile(struct aio* aio) {
	return KeFileAccess(aio, true);
}

/*
 * Sets the current working directory of the current process. The node should 
 * be open, and may be safely closed after a call to this function, as the 
 * kernel maintains a references to the working directory.
 */
/*
int KeSetWorkingDirectory(struct vnode* node) {
	struct process* prcss = GetProcess();
	if (prcss == NULL || node == NULL) {
		return EINVAL;
	}
	if (!S_ISDIR(node->stat.st_mode)) {
		return ENOTDIR;
	}
	LockScheduler();
	struct vnode* deref = prcss->cwd;
	prcss->cwd = node;
	ReferenceVnode(node);
	UnlockScheduler();
	if (deref != NULL) {
		DereferenceVnode(deref);
	}
	return 0;
}*/