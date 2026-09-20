#pragma once

#include "tusb.h"

#ifdef __cplusplus
extern "C" {
#endif

void arcade_usb_enumeration_task(void);
void arcade_usb_enumeration_reset(void);
void arcade_usb_enumeration_removed(uint8_t address);

#ifdef __cplusplus
}
#endif
