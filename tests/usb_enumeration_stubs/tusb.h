#pragma once
#include <stdint.h>
#define CFG_TUH_DEVICE_MAX 7
struct tusb_control_request_t {
    uint8_t bmRequestType{}, bRequest{};
    uint16_t wValue{}, wIndex{}, wLength{};
};
struct tuh_xfer_t {
    uint8_t daddr{};
    uint32_t result{}, actual_len{};
    tusb_control_request_t *setup{};
    uint8_t *buffer{};
    void (*complete_cb)(tuh_xfer_t*){};
};
bool tuh_vid_pid_get(uint8_t, uint16_t*, uint16_t*);
bool tuh_control_xfer(tuh_xfer_t*);
using tuh_xfer_cb_t = void (*)(tuh_xfer_t *);
bool tuh_configuration_set(uint8_t, uint8_t, tuh_xfer_cb_t, uintptr_t);

extern "C" {
bool tuh_configuration_set_cb(uint8_t, uint8_t, tuh_xfer_cb_t, uintptr_t, const uint8_t*, uint16_t);
void tuh_enumeration_recovery_task(void);
void tuh_enumeration_cancel_cb(void);
uint32_t tusb_time_millis_api(void);
}
