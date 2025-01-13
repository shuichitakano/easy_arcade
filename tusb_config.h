/*
 * author : Shuichi TAKANO
 * since  : Sun Nov 20 2022 03:29:57
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    // #define CFG_TUSB_DEBUG 2

#define CFG_TUH_ENUMERATION_BUFSIZE 1024 // DS4とか
#define CFG_TUH_XINPUT 2

#define CFG_TUSB_RHPORT0_MODE OPT_MODE_HOST

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

    // #define CFG_TUH_ENUMERATION_TIMEOUT 5000

#define CFG_TUH_HUB 2
#define CFG_TUH_CDC 0
// #define CFG_TUH_HID 2
#define CFG_TUH_HID (3 * CFG_TUH_DEVICE_MAX) // typical keyboard + mouse device can have 3-4 HID interfaces#define CFG_TUH_MSC 0
#define CFG_TUH_VENDOR 0
#define CFG_TUSB_HOST_DEVICE_MAX (4 + 1)
#define CFG_TUH_HID_EP_BUFSIZE 64

// max device support (excluding hub device): 1 hub typically has 4 ports
#define CFG_TUH_DEVICE_MAX (3 * CFG_TUH_HUB + 1)

#ifdef __cplusplus
}
#endif
