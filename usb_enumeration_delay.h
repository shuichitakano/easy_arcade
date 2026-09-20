#pragma once

#include <stddef.h>
#include <stdint.h>

// Match XInput or a non-boot HID interface with 64-byte interrupt IN and
// OUT endpoints, or multiple input-only non-boot HID interfaces with
// 64-byte interrupt IN endpoints. Some wireless receivers need a settle interval before
// SET_CONFIGURATION; retrying after that request is already too late.
// This is a transport heuristic, not a universal Switch protocol identifier.
inline bool usbNeedsConfigurationDelay(const uint8_t *descriptor, size_t length)
{
    if (!descriptor || length < 9 || descriptor[0] != 9 || descriptor[1] != 2)
        return false;
    const size_t total = descriptor[2] | (size_t(descriptor[3]) << 8);
    if (total < 9 || total > length)
        return false;
    bool needsDelay = false;
    bool bidirectionalHid = false;
    bool input64 = false, output64 = false;
    unsigned inputOnlyInterfaces = 0;
    bool singleEndpoint = false;
    for (size_t offset = 9; offset < total;)
    {
        if (total - offset < 2)
            return false;
        const auto *item = descriptor + offset;
        if (item[0] < 2 || item[0] > total - offset)
            return false;
        if (item[1] == 4)
        {
            if (item[0] < 9)
                return false;
            needsDelay |= bidirectionalHid && input64 && output64;
            inputOnlyInterfaces += bidirectionalHid && singleEndpoint && input64 && !output64;
            needsDelay |= item[5] == 0xff && item[6] == 0x5d && item[7] == 0x01;
            bidirectionalHid = item[3] == 0 && item[5] == 3 && item[6] == 0 && item[7] == 0;
            singleEndpoint = item[4] == 1;
            input64 = output64 = false;
        }
        else if (item[1] == 5)
        {
            if (item[0] < 7) return false;
            const unsigned packet = item[4] | (unsigned(item[5]) << 8);
            if (bidirectionalHid && (item[3] & 3) == 3 && packet == 64 && (item[2] & 15))
            {
                if (item[2] & 0x80) input64 = true;
                else output64 = true;
            }
        }
        offset += item[0];
    }
    inputOnlyInterfaces += bidirectionalHid && singleEndpoint && input64 && !output64;
    return needsDelay || (bidirectionalHid && input64 && output64) || inputOnlyInterfaces >= 2;
}

class UsbConfigurationDelay
{
public:
    void start(uint32_t now) { started_ = now; active_ = true; }
    void reset() { active_ = false; }
    bool ready(uint32_t now, uint32_t milliseconds = 1500) const
    {
        return active_ && uint32_t(now - started_) >= milliseconds;
    }

private:
    uint32_t started_ = 0;
    bool active_ = false;
};
