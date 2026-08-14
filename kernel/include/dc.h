#pragma once

#include <common.h>
#include <obj.h>

struct graphics_driver;
struct dc;

void InitDc(void);
struct dc* CreateDc(void);
