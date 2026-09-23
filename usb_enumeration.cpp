#include "usb_enumeration.h"
#include "usb_enumeration_delay.h"
#include "pico/time.h"

namespace
{
UsbConfigurationDelay delay;
uint8_t address;
uint8_t configuration;
tuh_xfer_cb_t complete;
uintptr_t userData;
// 8BitDo Arcade Stick XInput: 設定前にDevice Qualifierを1回要求すると動作。
// STALLでも継続する。文字列取得・追加待機・専用LED/vendor要求は不要だった。
enum class QualifierState { Idle, Pending, InFlight, Configure };
QualifierState qualifierState = QualifierState::Idle;
alignas(4) uint8_t qualifierBuffer[10]; // Preserve TinyUSB's configuration descriptor.
void qualifierComplete(tuh_xfer_t *xfer)
{
    if (qualifierState != QualifierState::InFlight || xfer->daddr != address) return;
    qualifierState = QualifierState::Configure;
}
}

extern "C" uint32_t tusb_time_millis_api(void)
{
    return to_ms_since_boot(get_absolute_time());
}

extern "C" bool tuh_configuration_set_cb(uint8_t device, uint8_t config,
                                              tuh_xfer_cb_t callback, uintptr_t data,
                                              const uint8_t *descriptor, uint16_t length)
{
    // TinyUSB serializes enumeration, so only one configuration can be pending.
    delay.reset();
    qualifierState = QualifierState::Idle;
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(device, &vid, &pid);
    if (vid == 0x045e && pid == 0x028e) {
        address = device;
        configuration = config;
        complete = callback;
        userData = data;
        qualifierState = QualifierState::Pending;
        return true;
    }
    if (!usbNeedsConfigurationDelay(descriptor, length))
        return tuh_configuration_set(device, config, callback, data);
    address = device;
    configuration = config;
    complete = callback;
    userData = data;
    delay.start(to_ms_since_boot(get_absolute_time()));
    return true;
}

extern "C" void arcade_usb_enumeration_task(void)
{
    tuh_enumeration_recovery_task();
    if (qualifierState == QualifierState::Pending) {
        tusb_control_request_t request{};
        request.bmRequestType = 0x80;
        request.bRequest = 6; // GET_DESCRIPTOR
        request.wValue = 0x0600; // DEVICE_QUALIFIER
        request.wLength = sizeof qualifierBuffer;
        tuh_xfer_t xfer{};
        xfer.daddr = address;
        xfer.setup = &request;
        xfer.buffer = qualifierBuffer;
        xfer.complete_cb = qualifierComplete;
        qualifierState = QualifierState::InFlight;
        if (!tuh_control_xfer(&xfer)) qualifierState = QualifierState::Pending;
    } else if (qualifierState == QualifierState::Configure &&
               tuh_configuration_set(address, configuration, complete, userData)) {
        qualifierState = QualifierState::Idle;
    }
    if (delay.ready(to_ms_since_boot(get_absolute_time())) &&
        tuh_configuration_set(address, configuration, complete, userData))
        delay.reset();
}

extern "C" void arcade_usb_enumeration_reset(void)
{
    delay.reset();
    complete = nullptr;
    qualifierState = QualifierState::Idle;
}

extern "C" void arcade_usb_enumeration_removed(uint8_t device)
{
    if (device == address)
        arcade_usb_enumeration_reset();
}

extern "C" void tuh_enumeration_cancel_cb(void)
{
    arcade_usb_enumeration_reset();
}
