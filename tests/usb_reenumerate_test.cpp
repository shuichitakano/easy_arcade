#include <cassert>
#include <cstdint>
#include <cstdio>
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX 7
constexpr unsigned CONTROL_STAGE_IDLE = 0, ENUM_RESET_1 = 1;
struct usbh_dev0_t { uint8_t rhport, hub_addr, hub_port, speed, enumerating; };
struct usbh_device_t {
    uint8_t rhport, hub_addr, hub_port, speed;
    bool connected, configured;
};
static usbh_dev0_t _dev0{};
static struct { unsigned stage; } _ctrl_xfer{};
static usbh_device_t device{0, 8, 1, 0, true, true};
static bool initialized = true, empty = true, accepts = true;
static unsigned resets;
static int _usbh_q;
static bool tuh_inited() { return initialized; }
static bool osal_queue_empty(int) { return empty; }
static usbh_device_t* get_device(uint8_t addr) { return addr == 1 ? &device : nullptr; }
static void process_enumeration() {}
static bool hub_port_reset(uint8_t hub, uint8_t port, void (*callback)(), unsigned state) {
    assert(hub == 8 && port == 1 && callback == process_enumeration && state == ENUM_RESET_1);
    assert(_dev0.enumerating && _dev0.hub_addr == hub && _dev0.hub_port == port);
    ++resets;
    return accepts;
}
#include "tinyusb_reenumerate_under_test.inc"
constexpr unsigned ENUM_RESET_DELAY_MS = 50, ENUM_HUB_CLEAR_RESET_1 = 2;
static struct { uint8_t ctrl[64]; } _usbh_epbuf;
static unsigned order, closed, finished;
static bool statusAccepted = true;
static void tusb_time_delay_ms_api(unsigned ms) {
    assert(ms == 50 && order == 0); order = 1;
}
static bool hub_port_get_status(uint8_t hub, uint8_t port, uint8_t* buffer,
                                void (*callback)(), unsigned state) {
    assert(order == 1 && hub == 8 && port == 1 && buffer == _usbh_epbuf.ctrl);
    assert(callback == process_enumeration && state == ENUM_HUB_CLEAR_RESET_1);
    order = 2;
    return statusAccepted;
}
static void process_removing_device(uint8_t rhport, uint8_t hub, uint8_t port) {
    assert(order == 2 && statusAccepted && rhport == 0 && hub == 8 && port == 1);
    order = 3; ++closed;
}
static void enum_full_complete() { assert(order == 2); ++finished; }
#include "tinyusb_reset_completion_under_test.inc"
int main() {
    assert(!tuh_device_reenumerate(0));
    assert(!tuh_device_reenumerate(8));
    assert(!tuh_device_reenumerate(2));
    initialized = false; assert(!tuh_device_reenumerate(1)); initialized = true;
    _dev0.enumerating = 1; assert(!tuh_device_reenumerate(1)); _dev0.enumerating = 0;
    _ctrl_xfer.stage = 1; assert(!tuh_device_reenumerate(1)); _ctrl_xfer.stage = 0;
    empty = false; assert(!tuh_device_reenumerate(1)); empty = true;
    device.connected = false; assert(!tuh_device_reenumerate(1)); device.connected = true;
    device.configured = false; assert(!tuh_device_reenumerate(1)); device.configured = true;
    device.hub_addr = 0; assert(!tuh_device_reenumerate(1)); device.hub_addr = 8;
    device.hub_port = 0; assert(!tuh_device_reenumerate(1)); device.hub_port = 1;
    assert(resets == 0);
    _dev0 = {0, 9, 2, 1, 0};
    accepts = false;
    assert(!tuh_device_reenumerate(1));
    assert(!_dev0.enumerating && _dev0.hub_addr == 9 && _dev0.hub_port == 2 && _dev0.speed == 1);
    accepts = true;
    assert(tuh_device_reenumerate(1));
    assert(_dev0.enumerating && _dev0.hub_addr == 8 && _dev0.hub_port == 1);
    assert(!tuh_device_reenumerate(1));
    reset_completion_under_test();
    assert(order == 3 && closed == 1 && finished == 0);
    order = 0; statusAccepted = false;
    reset_completion_under_test();
    assert(closed == 1 && finished == 1);
    puts("USB reenumeration admission and reset-order tests passed");
}
