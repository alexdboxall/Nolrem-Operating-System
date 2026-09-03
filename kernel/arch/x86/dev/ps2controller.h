#pragma once

#include <common.h>

void InitPs2(void);

/* High-level config API: never fail, always return/accept a value. */
uint8_t Ps2ControllerGetConfiguration(void);
void Ps2ControllerSetConfiguration(uint8_t value);
void Ps2ControllerSetIrqEnable(bool enable, bool port2);

/* Device I/O: return error codes, deliver data via out-parameter. */
int Ps2DeviceRead(uint8_t* out);
int Ps2DeviceWrite(uint8_t data, bool port2);

/* Port control. */
void Ps2ControllerEnableDevice(bool port2);
void Ps2ControllerDisableDevice(bool port2);
void Ps2ControllerFlushOutputBuffer(void);

/* Diagnostics: best-effort port tests. */
int Ps2ControllerTestPort(bool port2);
