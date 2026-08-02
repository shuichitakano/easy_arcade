/*
 * USB Mass Storage host and FatFS integration.
 */
#pragma once

#include <cstdint>

void usbStorageInit();
void usbStorageTask();
void usbStorageDeinit();

bool usbStorageMounted();
uint8_t usbStorageDeviceAddress();
const char *usbStorageRootPath();
