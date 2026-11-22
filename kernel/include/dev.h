#pragma once

void KeInitNullDevice(void);

struct vnode;

struct vnode* KeCreatePipe(void);
void KeBreakPipe(struct vnode* node);