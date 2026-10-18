#pragma once

#include <vfs.h>
#include <common.h>
#include <filesystem.h>

int FatMountCreator(struct file* raw_device, struct file** out);
void InitFat(void);