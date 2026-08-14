#pragma once

void InitNullDevice(void);

struct vnode;

struct vnode* CreatePipe(void);
void BreakPipe(struct vnode* node);