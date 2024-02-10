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

    printf("HID device address = %d, instance = %d is mounted\n", dev_addr, instance);
    printf("VID = %04x, PID = %04x\r\n", vid, pid);

    util::dumpBytes(desc_report, desc_len);

    const char *protocol_str[] = {"None", "Keyboard", "Mouse"}; // hid_protocol_type_t
    uint8_t const interface_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    int port = getHubPort(dev_addr);
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
        printf("Error: cannot request to receive report\r\n");
    }
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    printf("HID device address = %d, instance = %d is unmounted\n", dev_addr, instance);
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
                            padInput.buttons, padInput.hat, padInput.analogs);
        PadManager::instance().setData(port, padInput);
    }

    if (!tuh_hid_receive_report(dev_addr, instance))
    {
        printf("Error: cannot request to receive report\r\n");
    }
}
