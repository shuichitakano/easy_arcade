/*
 * author : Shuichi TAKANO
 * since  : Mon Feb 12 2024 00:03:27
 */

#include "tusb_option.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"

#include <usb_midi_host/usb_midi_host.h>
#include <tusb_xinput/xinput_host.h>

bool midih_init_(void)
{
    midih_init();
    return true;
}

extern "C" const usbh_class_driver_t *
usbh_app_driver_get_cb(uint8_t *driver_count)
{
    static usbh_class_driver_t drivers[] = {
        usbh_xinput_driver,
        {.init = midih_init_,
         .open = midih_open,
         .set_config = midih_set_config,
         .xfer_cb = midih_xfer_cb,
         .close = midih_close},
    };
    *driver_count = 2;
    return drivers;
}
