#include "usb_enumeration_delay.h"
#include <array>
#include <cassert>
#include <cstdio>

int main()
{
    // Configuration captured from the adapter; deliberately no VID/PID input.
    std::array<uint8_t, 48> descriptor{
        9,2,48,0,1,1,0,0x80,0xfa,
        9,4,0,0,2,0xff,0x5d,1,0,
        16,0x21,0x10,1,1,0x24,0x81,0x14,3,0,3,0x13,2,0,3,0,
        7,5,0x81,3,0x20,0,4, 7,5,2,3,0x20,0,8
    };
    assert(usbNeedsConfigurationDelay(descriptor.data(), descriptor.size()));
    assert(!usbNeedsConfigurationDelay(nullptr, 48));
    for (size_t n = 0; n < descriptor.size(); ++n)
        assert(!usbNeedsConfigurationDelay(descriptor.data(), n));
    auto other = descriptor;
    other[14] = 3; // A different subclass / packet size must not match HID.
    assert(!usbNeedsConfigurationDelay(other.data(), other.size()));
    other = descriptor;
    other[16] = 2; // A different vendor protocol is not assumed to be XInput.
    assert(!usbNeedsConfigurationDelay(other.data(), other.size()));
    other = descriptor;
    other[18] = 0; // Malformed descriptor must not loop or match.
    assert(!usbNeedsConfigurationDelay(other.data(), other.size()));
    other = descriptor;
    other[41] = 8; // Descriptor overruns the configuration.
    assert(!usbNeedsConfigurationDelay(other.data(), other.size()));

    std::array<uint8_t, 41> hid{
        9,2,41,0,1,1,0,0xc0,250,
        9,4,0,0,2,3,0,0,0,
        9,0x21,0x11,1,0,1,0x22,203,0,
        7,5,0x81,3,64,0,8, 7,5,2,3,64,0,8
    };
    assert(usbNeedsConfigurationDelay(hid.data(), hid.size()));
    // Idle and Switch HID report descriptor lengths need no ID allowlist.
    hid[25]=37;
    assert(usbNeedsConfigurationDelay(hid.data(), hid.size()));
    for (size_t n=0; n<hid.size(); ++n) assert(!usbNeedsConfigurationDelay(hid.data(), n));
    auto ordinary=hid;
    ordinary[15]=1; ordinary[16]=1; // boot keyboard
    assert(!usbNeedsConfigurationDelay(ordinary.data(), ordinary.size()));
    ordinary=hid; ordinary[38]=32; // small output endpoint
    assert(!usbNeedsConfigurationDelay(ordinary.data(), ordinary.size()));
    ordinary=hid; ordinary[36]=0x82; // two IN endpoints, no OUT
    assert(!usbNeedsConfigurationDelay(ordinary.data(), ordinary.size()));
    ordinary=hid; ordinary[37]=2; // bulk OUT, not interrupt
    assert(!usbNeedsConfigurationDelay(ordinary.data(), ordinary.size()));
    ordinary=hid; ordinary[12]=1; // alternate setting, not active at configure
    assert(!usbNeedsConfigurationDelay(ordinary.data(), ordinary.size()));
    const uint8_t split[]={9,2,41,0,2,1,0,0x80,50,
        9,4,0,0,1,3,0,0,0, 7,5,0x81,3,64,0,8,
        9,4,1,0,1,3,0,0,0, 7,5,2,3,64,0,8};
    assert(!usbNeedsConfigurationDelay(split,sizeof split)); // must be one interface

    const uint8_t psc[]={9,2,59,0,2,1,0,0xa0,50,
        9,4,0,0,1,3,0,0,0, 9,0x21,0x11,1,0,1,0x22,49,0,
        7,5,0x81,3,64,0,10,
        9,4,1,0,1,3,0,0,0, 9,0x21,0x10,1,0,1,0x22,50,0,
        7,5,0x82,3,64,0,10};
    assert(usbNeedsConfigurationDelay(psc,sizeof psc));
    for (size_t n=0;n<sizeof psc;++n) assert(!usbNeedsConfigurationDelay(psc,n));
    const uint8_t single[]={9,2,25,0,1,1,0,0x80,50,
        9,4,0,0,1,3,0,0,0, 7,5,0x81,3,64,0,10};
    assert(!usbNeedsConfigurationDelay(single,sizeof single));

    UsbConfigurationDelay delay;
    assert(!delay.ready(5000));
    delay.start(100);
    assert(!delay.ready(1599));
    assert(delay.ready(1600));
    delay.reset(); // Power-off must cancel the pending configuration.
    assert(!delay.ready(9999));
    delay.start(0xffffff00u);
    assert(!delay.ready(uint32_t(0xffffff00u + 1499u)));
    assert(delay.ready(uint32_t(0xffffff00u + 1500u)));
    delay.start(2000); // Re-enumeration starts a new deadline.
    assert(!delay.ready(3499));
    assert(delay.ready(3500));
    std::puts("USB enumeration tests passed");
}
