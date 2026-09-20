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
    if (delay.ready(to_ms_since_boot(get_absolute_time())) &&
        tuh_configuration_set(address, configuration, complete, userData))
        delay.reset();
}

extern "C" void arcade_usb_enumeration_reset(void)
{
    delay.reset();
    complete = nullptr;
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
