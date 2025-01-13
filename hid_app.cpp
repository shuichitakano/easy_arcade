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

#include <host/hub.h>

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
    // 邪悪すぎるのでどうにかしたい
}

namespace
{
    inline constexpr size_t HUB0_PORT_COUNT = 2;
    inline constexpr size_t MAX_PORTS = 4;
    inline constexpr uint8_t HUB0_ADDR = CFG_TUH_DEVICE_MAX + 1;
    inline constexpr uint8_t HUB1_ADDR = CFG_TUH_DEVICE_MAX + 2; // ポートに差しているHUB

    std::array<HIDInfo, MAX_PORTS> hidInfos_; // port毎のHIDInfo
    descriptor_hub_desc_t extHubDesc_;        // 追加HUBのdescriptor

    uint8_t hub0Port_[HUB0_PORT_COUNT]{0, 1};
    uint8_t hub1PortOffset_ = 0;
    uint8_t hub1PortCount_ = 0;

    void resetExtHubPortInfo()
    {
        hub0Port_[0] = 0;
        hub0Port_[1] = 1;
        hub1PortOffset_ = 0;
        hub1PortCount_ = 0;
    }

    std::tuple<int, int>
    getHubPort(uint8_t dev_addr)
    {
        hcd_devtree_info_t devInfo;
        hcd_devtree_get_info(dev_addr, &devInfo);
        // DPRINT((" hub: addr %d, port %d, rhport %d\n", devInfo.hub_addr, devInfo.hub_port, devInfo.rhport));
        return {devInfo.hub_addr - HUB0_ADDR, devInfo.hub_port - 1};
    }

    // 正規化したコントローラーポート番号を取得
    int getControllerPortID(uint8_t dev_addr)
    {
        auto [hubAddr, hubPort] = getHubPort(dev_addr);

        int r = 0;
        if (hubAddr == 0)
        {
            r = hub0Port_[std::clamp<int>(hubPort, 0, HUB0_PORT_COUNT - 1)];
        }
        else
        {
            r = std::clamp<int>(hub1PortOffset_ + hubPort, 0, MAX_PORTS - 1);
        }

        // printf("hub[Addr %d, Port %d]: port %d\n", hubAddr, hubPort, r);
        return r;
    }

    void checkExtHubCb(tuh_xfer_t *xfer)
    {
        if (XFER_RESULT_SUCCESS != xfer->result)
        {
            DPRINT(("Failed to get hub descriptor\n"));
            resetExtHubPortInfo();
            return;
        }

        hub1PortCount_ = extHubDesc_.bNbrPorts;

        auto [extHubHub, extHubPort] = getHubPort(HUB1_ADDR);
        DPRINT(("extHub: hub %d, port %d, %d ports.\n",
                extHubHub, extHubPort, hub1PortCount_));

        assert(extHubHub == 0 && extHubPort >= 0);
        if (extHubPort == 0)
        {
            hub1PortOffset_ = 0;
            hub0Port_[1] = hub1PortCount_;
        }
        else
        {
            hub1PortOffset_ = 1;
            hub0Port_[1] = 1; // 読まれることはないはず
        }

        DPRINT(("hub0port: { %d, %d }, hub1ofs: %d \n",
                hub0Port_[0], hub0Port_[1], hub1PortOffset_));
    }

    void checkExtHub()
    {
        tusb_control_request_t const request = {
            .bmRequestType_bit = {
                .recipient = TUSB_REQ_RCPT_DEVICE,
                .type = TUSB_REQ_TYPE_CLASS,
                .direction = TUSB_DIR_IN},
            .bRequest = HUB_REQUEST_GET_DESCRIPTOR,
            .wValue = 0,
            .wIndex = 0,
            .wLength = sizeof(descriptor_hub_desc_t)};

        tuh_xfer_t xfer = {
            .daddr = HUB1_ADDR,
            .ep_addr = 0,
            .setup = &request,
            .buffer = (uint8_t *)&extHubDesc_,
            .complete_cb = checkExtHubCb,
            .user_data = 0};

        if (!tuh_control_xfer(&xfer))
        {
            DPRINT(("ext hub is not connected.\n"));
            resetExtHubPortInfo();
        }
    }
}

extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    DPRINT(("HID device address = %d, instance = %d is mounted\n", dev_addr, instance));
    DPRINT(("VID = %04x, PID = %04x\r\n", vid, pid));

#ifndef NDEBUG
    util::dumpBytes(desc_report, desc_len);
#endif

    // const char *protocol_str[] = {"None", "Keyboard", "Mouse"}; // hid_protocol_type_t
    uint8_t const interface_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    int port = getControllerPortID(dev_addr);
    DPRINT(("port = %d\n", port));
    if (port >= 0 && port < MAX_PORTS)
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
    int port = getControllerPortID(dev_addr);

    // printf("report received: addr:%d, inst %d, port %d\n", dev_addr, instance, port);
    // util::dumpBytes(report, len);

    if (port >= 0 && port < MAX_PORTS)
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

    void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xid_itf, uint16_t len)
    {
        auto *p = &xid_itf->pad;

        if (xid_itf->connected && xid_itf->new_pad_data)
        {
            int port = getControllerPortID(dev_addr);

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

void tuh_mount_cb(uint8_t dev_addr)
{
    DPRINT(("A device with address %d is mounted\n", dev_addr));
    checkExtHub();
}

void tuh_umount_cb(uint8_t dev_addr)
{
    DPRINT(("A device with address %d is unmounted\n", dev_addr));
    checkExtHub();
}