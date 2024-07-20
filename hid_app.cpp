/*
 * author : Shuichi TAKANO
 * since  : Sun Nov 20 2022 03:32:25
 */

#include <tusb.h>
#include <stdio.h>
#include <algorithm>
#include <map>
#include <tuple>
#include "pad_state.h"
#include "pad_manager.h"
#include "util.h"
#include "hid_info.h"
#include "debug.h"

#include <tusb_xinput/xinput_host.h>

extern "C"
{
    typedef struct
    {
        uint8_t rhport;
        uint8_t hub_addr;
        uint8_t hub_port;
        uint8_t speed;
    } hcd_devtree_info_t;

    void hcd_devtree_get_info(uint8_t dev_addr, hcd_devtree_info_t *devtree_info);

    static int getHubPort(uint8_t dev_addr)
    {
        hcd_devtree_info_t devInfo;
        hcd_devtree_get_info(dev_addr, &devInfo);
        // DPRINT((" hub: addr %d, port %d\n", devInfo.hub_addr, devInfo.hub_port));
        return std::min<int>(1, std::max<int>(0, static_cast<int>(devInfo.hub_port) - 1));
    }
    // 邪悪すぎるのでどうにかしたい
}

namespace
{
    std::array<HIDInfo, 2> hidInfos_; // port毎のHIDInfo
}

extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    DPRINT(("HID device address = %d, instance = %d is mounted\n", dev_addr, instance));
    DPRINT(("VID = %04x, PID = %04x\r\n", vid, pid));

    util::dumpBytes(desc_report, desc_len);

    // const char *protocol_str[] = {"None", "Keyboard", "Mouse"}; // hid_protocol_type_t
    uint8_t const interface_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    int port = getHubPort(dev_addr);
    DPRINT(("port = %d\n", port));
    if (port >= 0 && port < 2)
    {
        auto &hidInfo = hidInfos_[port];
        hidInfo.parseDesc(desc_report, desc_report + desc_len);
        hidInfo.setVID(vid);
        hidInfo.setPID(pid);
        PadManager::instance().resetLatestPadData(port);
    }

    if (!tuh_hid_receive_report(dev_addr, instance))
    {
        DPRINT(("Error: cannot request to receive report\r\n"));
    }
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    DPRINT(("HID device address = %d, instance = %d is unmounted\n", dev_addr, instance));
}

extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr,
                                           uint8_t instance, uint8_t const *report, uint16_t len)
{
    int port = getHubPort(dev_addr);

    // printf("report received: addr:%d, inst %d, port %d\n", dev_addr, instance, port);
    // util::dumpBytes(report, len);

    if (port >= 0 && port < 2)
    {
        auto &hidInfo = hidInfos_[port];

        PadManager::PadInput padInput;
        padInput.vid = hidInfo.getVID();
        padInput.pid = hidInfo.getPID();
        hidInfo.parseReport(report, len,
                            padInput.buttons[0], padInput.hat, padInput.analogs);
        PadManager::instance().setData(port, padInput);
    }

    if (!tuh_hid_receive_report(dev_addr, instance))
    {
        DPRINT(("Error: cannot request to receive report\r\n"));
    }
}

// XINPUT
extern "C"
{
#if 0
    const usbh_class_driver_t *usbh_app_driver_get_cb(uint8_t *driver_count)
    {
        *driver_count = 1;
        return &usbh_xinput_driver;
    }
#endif

    void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
    {
        auto *xid_itf = reinterpret_cast<const xinputh_interface_t *>(report);
        auto *p = &xid_itf->pad;

        if (xid_itf->connected && xid_itf->new_pad_data)
        {
            int port = getHubPort(dev_addr);

            uint16_t vid, pid;
            tuh_vid_pid_get(dev_addr, &vid, &pid);

            auto scale = [](int v)
            {
                return (v + 32768) / 256;
            };

            PadManager::PadInput pi;
            pi.vid = vid;
            pi.pid = pid;
            pi.buttons[0] = p->wButtons;
            pi.analogs[0] = scale(p->sThumbLX);
            pi.analogs[1] = scale(p->sThumbLY);
            pi.analogs[2] = scale(p->sThumbRX);
            pi.analogs[3] = p->bLeftTrigger;
            pi.analogs[4] = p->bRightTrigger;
            pi.analogs[5] = scale(p->sThumbRY);
            PadManager::instance().setData(port, pi);

            // TU_LOG1("%d: [%02x, %02x], Buttons %04x, LT: %02x RT: %02x, LX: %d, LY: %d, RX: %d, RY: %d\n",
            //         port, dev_addr, instance, p->wButtons, p->bLeftTrigger, p->bRightTrigger, p->sThumbLX, p->sThumbLY, p->sThumbRX, p->sThumbRY);
        }
        tuh_xinput_receive_report(dev_addr, instance);
    }

    void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xinput_itf)
    {
        uint16_t vid, pid;
        tuh_vid_pid_get(dev_addr, &vid, &pid);

        DPRINT(("XINPUT device address = %d, instance = %d is mounted\n", dev_addr, instance));
        DPRINT(("VID = %04x, PID = %04x\r\n", vid, pid));

        if (xinput_itf->connected ||
            xinput_itf->type != XBOX360_WIRELESS)
        {
            tuh_xinput_set_led(dev_addr, instance, 0, true);
            // tuh_xinput_set_led(dev_addr, instance, 1, true);
            // tuh_xinput_set_rumble(dev_addr, instance, 0, 0, true);
        }
        tuh_xinput_receive_report(dev_addr, instance);

        switch (xinput_itf->type)
        {
        case XBOX360_WIRELESS:
            DPRINT(("Xbox 360 Wireless Controller\n"));
            break;
        case XBOX360_WIRED:
            DPRINT(("Xbox 360 Wired Controller\n"));
            break;
        case XBOXONE:
            DPRINT(("Xbox One Controller\n"));
            break;
        case XBOXOG:
            DPRINT(("Xbox OG Controller\n"));
            break;
        default:
            DPRINT(("Unknown Controller\n"));
            break;
        }
    }

    void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance)
    {
        DPRINT(("XINPUT device address = %d, instance = %d is unmounted\n", dev_addr, instance));
    }
}