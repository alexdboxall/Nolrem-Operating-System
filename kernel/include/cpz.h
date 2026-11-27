#pragma once

#include <stddef.h>

struct file;
struct cpz_image;

/*
 * Mapping CPZ files (see cpz.h) into memory with the VMM, decompressing a page at a time when it is touched.
 *
 * This sits entirely on top of the public VMM interface (KeAllocFileMemory + a virt_loader); the VMM itself
 * knows nothing about it.
 *
 *     struct cpz_image* img = KeOpenCpzImage(file);
 *     void* a = KeAllocCompressedFileMemory(img, 0, len, 0);
 *     void* b = KeAllocCompressedFileMemory(img, 2 * PAGE_SIZE, PAGE_SIZE, 0);     // any number of mappings
 *     ...
 *     KeFreeMemory(a, len);
 *     KeFreeMemory(b, PAGE_SIZE);
 *     KeDerefObject(img);                                       // LAST - see below
 *
 * The image is an ordinary refcounted object (OBJ_CPZ_INST) that the caller owns. It holds the file's
 * table and is the loader context of every mapping made from it, so, exactly like the bitmap storage that
 * its reloader hangs off, it MUST outlive them: KeFreeMemory() every mapping first, then drop the image.
 */

/* Call once at boot, after the object system is up. Registers OBJ_CPZ_INST. */
void KeInitCpzImages(void);

/*
 * Reads and validates the header and table of a CPZ file. Takes its own reference on 'file'. Returns an image
 * with one reference (yours), or NULL if it isn't a valid CPZ file, on an IO error or if out of memory.
 * May block. The file must not change while the image, or anything mapped from it, exists.
 */
struct cpz_image* KeOpenCpzImage(struct file* file);

/* Size of the decompressed image in bytes: a whole number of pages, so it includes the zero padding. */
size_t KeGetCpzImageSize(struct cpz_image* image);

int KeCheckCpzSignature(struct file* file, bool* is_cpz);

/*
 * Like KeAllocFileMemory(), but memory reads as the DECOMPRESSED image. Each page is decompressed by the
 * page-fault handler when first touched, and again after a KeDiscardPage(). Nothing is read from the file here.
 *
 * 'offset' and 'bytes' are positions in the decompressed image, not in the file: offset must be page aligned
 * and [offset, offset + bytes) must lie inside KeGetCpzImageSize(). A CPZ file doesn't record the length of the
 * original, so the caller has to know how much of it is real; the padding reads as zeros.
 *
 * Release with KeFreeMemory(ptr, bytes) as usual. Writable mappings work (pages are private) but a page that has
 * been written can't be discarded, as there is no way to write it back.
 *
 * Returns NULL on a bad argument or if out of memory. Does not take ownership of, or a reference to, 'image'.
 */
void* KeAllocCompressedFileMemory(struct cpz_image* image, size_t offset, size_t bytes, int flags);