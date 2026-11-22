#include <vfs.h>
#include <spinlock.h>
#include <errno.h>
#include <stdint.h>
#include <obj.h>

static struct spinlock lock;
static struct vnode* root;

static bool KeShouldRemoveVnode(struct vnode* node) {
    return KeGetRefCount(node) == 1;
}

static inline int KeCompareKey(dev_t dev, ino_t id, const struct vnode* n) {
    if (dev != n->stat.st_dev) {
        return dev < n->stat.st_dev ? -1 : 1;
    }
    if (id != n->stat.st_ino) {
        return id < n->stat.st_ino ? -1 : 1;
    }
    return 0;
}

static inline int KeCompareNodes(const struct vnode* a, const struct vnode* b) {
    return KeCompareKey(a->stat.st_dev, a->stat.st_ino, b);
}

static inline int KeHeight(const struct vnode* n) {
    return n != NULL ? n->tree.height : 0;
}

static inline void KeUpdateHeight(struct vnode* n) {
    int l = KeHeight(n->tree.left);
    int r = KeHeight(n->tree.right);
    n->tree.height = (int8_t) ((l > r ? l : r) + 1);
}

static inline int KeBalanceFactor(const struct vnode* n) {
    return KeHeight(n->tree.left) - KeHeight(n->tree.right);
}

static struct vnode* KeRotateRight(struct vnode* y) {
    struct vnode* x = y->tree.left;
    y->tree.left = x->tree.right;
    x->tree.right = y;
    KeUpdateHeight(y);
    KeUpdateHeight(x);
    return x;
}

static struct vnode* KeRotateLeft(struct vnode* x) {
    struct vnode* y = x->tree.right;
    x->tree.right = y->tree.left;
    y->tree.left = x;
    KeUpdateHeight(x);
    KeUpdateHeight(y);
    return y;
}

static struct vnode* KeRebalance(struct vnode* n) {
    KeUpdateHeight(n);
    int b = KeBalanceFactor(n);
    if (b > 1) {
        if (KeBalanceFactor(n->tree.left) < 0) {
            n->tree.left = KeRotateLeft(n->tree.left);
        }
        return KeRotateRight(n);
    }
    if (b < -1) {
        if (KeBalanceFactor(n->tree.right) > 0) {
            n->tree.right = KeRotateRight(n->tree.right);
        }
        return KeRotateLeft(n);
    }
    return n;
}

static struct vnode* KeInsertNode(struct vnode* sub, struct vnode* node,
                                bool* duplicate) {
    if (sub == NULL) {
        return node;
    }
    int c = KeCompareNodes(node, sub);
    if (c == 0) {
        *duplicate = true;
        return sub;
    }
    if (c < 0) {
        sub->tree.left = KeInsertNode(sub->tree.left, node, duplicate);
    } else {
        sub->tree.right = KeInsertNode(sub->tree.right, node, duplicate);
    }
    return *duplicate ? sub : KeRebalance(sub);
}

static struct vnode* KeDetachMin(struct vnode* sub, struct vnode** min_out) {
    if (sub->tree.left == NULL) {
        *min_out = sub;
        return sub->tree.right;
    }
    sub->tree.left = KeDetachMin(sub->tree.left, min_out);
    return KeRebalance(sub);
}

static struct vnode* KeRemoveNode(struct vnode* sub, struct vnode* node,
                                bool* found) {
    if (sub == NULL) {
        return NULL;
    }
    int c = KeCompareNodes(node, sub);
    if (c < 0) {
        sub->tree.left = KeRemoveNode(sub->tree.left, node, found);
        return KeRebalance(sub);
    }
    if (c > 0) {
        sub->tree.right = KeRemoveNode(sub->tree.right, node, found);
        return KeRebalance(sub);
    }
    if (sub != node) {
        /* Same key but a different vnode: `node` isn't in the table. */
        return sub;
    }

    *found = true;
    struct vnode* replacement;
    if (sub->tree.left == NULL) {
        replacement = sub->tree.right;
    } else if (sub->tree.right == NULL) {
        replacement = sub->tree.left;
    } else {
        struct vnode* successor;
        struct vnode* new_right = KeDetachMin(sub->tree.right, &successor);
        successor->tree.left = sub->tree.left;
        successor->tree.right = new_right;
        replacement = KeRebalance(successor);
    }
    sub->tree.left = NULL;
    sub->tree.right = NULL;
    sub->tree.height = 0;
    return replacement;
}

static struct vnode* KeFindNode(dev_t dev, ino_t id) {
    struct vnode* n = root;
    while (n != NULL) {
        int c = KeCompareKey(dev, id, n);
        if (c == 0) {
            return n;
        }
        n = c < 0 ? n->tree.left : n->tree.right;
    }
    return NULL;
}

export struct vnode* KeTryFindAndReferenceExistingVnode(ino_t id, dev_t dev) {
    KeAcquireSpinlock(&lock);
    struct vnode* node = KeFindNode(dev, id);
    if (node != NULL) {
        KeRefObject(node);
    }
    KeReleaseSpinlock(&lock);
    return node;
}

export int KeRegisterNewVnode(struct vnode* node) {
    if (node == NULL) {
        return EINVAL;
    }
    node->tree.left = NULL;
    node->tree.right = NULL;
    node->tree.height = 1;

    bool duplicate = false;
    KeAcquireSpinlock(&lock);
    root = KeInsertNode(root, node, &duplicate);
    if (!duplicate) {
        KeRefObject(node);
    }
    KeReleaseSpinlock(&lock);
    return duplicate ? EEXIST : 0;
}

/*
 * Explicit eviction (e.g. the file was unlinked or its filesystem is being
 * unmounted): removes `node` from the table and drops the table's reference.
 * Safe to call on a node that isn't registered; then it does nothing.
 */
export void KeUnregisterVnode(struct vnode* node) {
    if (node == NULL) {
        return;
    }
    bool found = false;
    KeAcquireSpinlock(&lock);
    root = KeRemoveNode(root, node, &found);
    KeReleaseSpinlock(&lock);
    if (found) {
        /* Outside the lock: this may run the destructor. */
        KeDerefObject(node);
    }
}

#define PURGE_BATCH 16
 
struct purge_scan {
    bool have_after;            /* resume point: only visit keys > after_* */
    dev_t after_dev;
    ino_t after_id;
    size_t count;
    struct vnode* found[PURGE_BATCH];
};

static void KeCollectPurgeVictims(struct vnode* sub, struct purge_scan* scan) {
    if (sub == NULL || scan->count >= PURGE_BATCH) {
        return;
    }
    bool sub_after_resume = !scan->have_after ||
        KeCompareKey(scan->after_dev, scan->after_id, sub) < 0;
    if (sub_after_resume) {
        /* Left subtree may still hold keys beyond the resume point. */
        KeCollectPurgeVictims(sub->tree.left, scan);
        if (scan->count >= PURGE_BATCH) {
            return;
        }
        if (KeShouldRemoveVnode(sub)) {
            scan->found[scan->count++] = sub;
        }
    }
    /* If sub's key <= resume point, so is everything to its left: skip. */
    KeCollectPurgeVictims(sub->tree.right, scan);
}

export void KePurgeOldVnodes(void) {
    struct purge_scan scan;
    scan.have_after = false;
    scan.after_dev = 0;
    scan.after_id = 0;
 
    for (;;) {
        scan.count = 0;
 
        KeAcquireSpinlock(&lock);
        KeCollectPurgeVictims(root, &scan);
        bool more = scan.count == PURGE_BATCH;
        if (more) {
            /* Remember where to resume while the last victim is still ours
             * to read; it may be freed once we deref it below. */
            const struct vnode* last = scan.found[scan.count - 1];
            scan.have_after = true;
            scan.after_dev = last->stat.st_dev;
            scan.after_id = last->stat.st_ino;
        }
        for (size_t i = 0; i < scan.count; i++) {
            bool found = false;
            root = KeRemoveNode(root, scan.found[i], &found);
            if (!found) {
                scan.found[i] = NULL;   /* never deref what we didn't unlink */
            }
        }
        KeReleaseSpinlock(&lock);
 
        for (size_t i = 0; i < scan.count; i++) {
            if (scan.found[i] != NULL) {
                KeDerefObject(scan.found[i]);
            }
        }
 
        if (!more) {
            break;
        }
    }
}

void KeInitVnodeTable(void) {
    KeInitSpinlock(&lock);
    root = NULL;
}