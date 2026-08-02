/*
 * USB Mass Storage host and FatFS integration.
 */

#include "usb_storage.h"

#include <cstdio>
#include <limits>

#include <hardware/watchdog.h>
#include <tusb.h>

extern "C"
{
#include <ff.h>
#include <diskio.h>
}

namespace
{
    constexpr BYTE DRIVE_NUMBER = 0;
    constexpr uint8_t LUN = 0;
    constexpr uint32_t SECTOR_SIZE = 512;
    constexpr const char *DRIVE_PATH = "0:";

    FATFS fatFs_;

    volatile uint8_t deviceAddress_ = 0;
    volatile bool attachPending_ = false;
    volatile bool detachPending_ = false;
    volatile bool ioBusy_ = false;
    volatile bool ioSucceeded_ = false;
    bool mounted_ = false;

    bool isActiveDeviceReady()
    {
        auto devAddr = deviceAddress_;
        return devAddr != 0 && tuh_msc_mounted(devAddr);
    }

    bool diskIoComplete(uint8_t devAddr, const tuh_msc_complete_data_t *data)
    {
        if (devAddr == deviceAddress_)
        {
            ioSucceeded_ = data && data->csw &&
                           data->csw->status == MSC_CSW_STATUS_PASSED;
            ioBusy_ = false;
        }
        return true;
    }

    bool waitForDiskIo()
    {
        while (ioBusy_ && isActiveDeviceReady())
        {
            tuh_task();
            watchdog_update();
        }
        return !ioBusy_ && ioSucceeded_ && isActiveDeviceReady();
    }

    void unmount()
    {
        if (mounted_)
        {
            f_unmount(DRIVE_PATH);
            mounted_ = false;
            printf("USB storage unmounted\n");
        }
    }
}

void usbStorageInit()
{
    unmount();
    deviceAddress_ = 0;
    attachPending_ = false;
    detachPending_ = false;
    ioBusy_ = false;
    ioSucceeded_ = false;
}

void usbStorageTask()
{
    if (detachPending_)
    {
        unmount();
        deviceAddress_ = 0;
        detachPending_ = false;
    }

    if (!attachPending_)
    {
        return;
    }

    attachPending_ = false;
    const auto devAddr = deviceAddress_;
    if (devAddr == 0 || !tuh_msc_mounted(devAddr))
    {
        return;
    }

    const auto blockSize = tuh_msc_get_block_size(devAddr, LUN);
    const auto blockCount = tuh_msc_get_block_count(devAddr, LUN);
    printf("USB storage mount begin: address %u, block size %lu, count %lu\n",
           static_cast<unsigned>(devAddr), static_cast<unsigned long>(blockSize),
           static_cast<unsigned long>(blockCount));
    if (blockSize != SECTOR_SIZE || blockCount == 0)
    {
        printf("USB storage unsupported: block size %lu, count %lu\n",
               static_cast<unsigned long>(blockSize),
               static_cast<unsigned long>(blockCount));
        return;
    }

    const auto result = f_mount(&fatFs_, DRIVE_PATH, 1);
    mounted_ = result == FR_OK;
    if (mounted_)
    {
        printf("USB storage mounted: %lu MiB\n",
               static_cast<unsigned long>(blockCount / 2048));
    }
    else
    {
        printf("USB storage mount failed: FatFS error %d\n", result);
    }
}

void usbStorageDeinit()
{
    unmount();
    deviceAddress_ = 0;
    attachPending_ = false;
    detachPending_ = false;
    ioBusy_ = false;
    ioSucceeded_ = false;
}

bool usbStorageMounted()
{
    return mounted_ && isActiveDeviceReady();
}

uint8_t usbStorageDeviceAddress()
{
    return deviceAddress_;
}

const char *usbStorageRootPath()
{
    return DRIVE_PATH;
}

extern "C" void tuh_msc_mount_cb(uint8_t devAddr)
{
    // A single FatFS volume is intentionally supported to minimize memory use.
    if (deviceAddress_ == 0)
    {
        printf("USB storage detected: address %u, mount pending\n",
               static_cast<unsigned>(devAddr));
        deviceAddress_ = devAddr;
        attachPending_ = true;
        detachPending_ = false;
    }
    else
    {
        printf("USB storage ignored: only one drive is supported\n");
    }
}

extern "C" void tuh_msc_umount_cb(uint8_t devAddr)
{
    if (devAddr == deviceAddress_)
    {
        printf("USB storage removed: address %u, unmount pending\n",
               static_cast<unsigned>(devAddr));
        attachPending_ = false;
        detachPending_ = true;
        ioSucceeded_ = false;
        ioBusy_ = false;
    }
}

extern "C" DSTATUS disk_status(BYTE drive)
{
    if (drive != DRIVE_NUMBER || !isActiveDeviceReady())
    {
        return STA_NODISK;
    }
    return 0;
}

extern "C" DSTATUS disk_initialize(BYTE drive)
{
    return disk_status(drive);
}

extern "C" DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    if (drive != DRIVE_NUMBER || !buffer || count == 0 ||
        count > std::numeric_limits<uint16_t>::max())
    {
        return RES_PARERR;
    }
    if (!isActiveDeviceReady())
    {
        return RES_NOTRDY;
    }

    ioSucceeded_ = false;
    ioBusy_ = true;
    if (!tuh_msc_read10(deviceAddress_, LUN, buffer, sector,
                        static_cast<uint16_t>(count), diskIoComplete, 0))
    {
        ioBusy_ = false;
        printf("USB storage read submit failed: LBA %lu, count %u\n",
               static_cast<unsigned long>(sector), count);
        return RES_ERROR;
    }

    if (!waitForDiskIo())
    {
        printf("USB storage read failed: LBA %lu, count %u\n",
               static_cast<unsigned long>(sector), count);
        return RES_ERROR;
    }
    return RES_OK;
}

#if FF_FS_READONLY == 0
extern "C" DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
    if (drive != DRIVE_NUMBER || !buffer || count == 0 ||
        count > std::numeric_limits<uint16_t>::max())
    {
        return RES_PARERR;
    }
    if (!isActiveDeviceReady())
    {
        return RES_NOTRDY;
    }

    ioSucceeded_ = false;
    ioBusy_ = true;
    if (!tuh_msc_write10(deviceAddress_, LUN, buffer, sector,
                         static_cast<uint16_t>(count), diskIoComplete, 0))
    {
        ioBusy_ = false;
        printf("USB storage write submit failed: LBA %lu, count %u\n",
               static_cast<unsigned long>(sector), count);
        return RES_ERROR;
    }

    if (!waitForDiskIo())
    {
        printf("USB storage write failed: LBA %lu, count %u\n",
               static_cast<unsigned long>(sector), count);
        return RES_ERROR;
    }
    return RES_OK;
}
#endif

extern "C" DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
    if (drive != DRIVE_NUMBER || !isActiveDeviceReady())
    {
        return RES_NOTRDY;
    }

    switch (command)
    {
    case CTRL_SYNC:
        return ioBusy_ ? RES_NOTRDY : RES_OK;

    case GET_SECTOR_COUNT:
        if (!buffer)
        {
            return RES_PARERR;
        }
        *static_cast<LBA_t *>(buffer) =
            tuh_msc_get_block_count(deviceAddress_, LUN);
        return RES_OK;

    case GET_SECTOR_SIZE:
        if (!buffer)
        {
            return RES_PARERR;
        }
        *static_cast<WORD *>(buffer) = SECTOR_SIZE;
        return RES_OK;

    case GET_BLOCK_SIZE:
        if (!buffer)
        {
            return RES_PARERR;
        }
        *static_cast<DWORD *>(buffer) = 1;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
