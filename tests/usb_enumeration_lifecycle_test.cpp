#include "usb_enumeration.h"
#include <cassert>
#include <cstdio>
static uint64_t now;
static unsigned submitted;
static uint8_t lastAddress;
static uintptr_t lastData;
static bool accept = true;
uint64_t get_absolute_time() { return now; }
extern "C" void tuh_enumeration_recovery_task() {}
bool tuh_vid_pid_get(uint8_t, uint16_t *vid, uint16_t *pid) { *vid=*pid=0; return true; }
bool tuh_control_xfer(tuh_xfer_t*) { assert(false); return false; }
bool tuh_configuration_set(uint8_t a, uint8_t, tuh_xfer_cb_t, uintptr_t d)
{
    if (!accept) return false;
    ++submitted; lastAddress = a; lastData = d; return true;
}
int main()
{
    const uint8_t desc[] = {9,2,18,0,1,1,0,0x80,50,9,4,0,0,2,0xff,0x5d,1,0};
    tuh_configuration_set_cb(1,1,nullptr,12,desc,sizeof desc);
    now=1499; arcade_usb_enumeration_task(); assert(submitted==0);
    arcade_usb_enumeration_removed(1);
    now=1500; arcade_usb_enumeration_task(); assert(submitted==0);
    tuh_configuration_set_cb(1,1,nullptr,99,desc,sizeof desc);
    arcade_usb_enumeration_removed(2);
    now=2999; arcade_usb_enumeration_task(); assert(submitted==0);
    now=3000; accept=false; arcade_usb_enumeration_task(); assert(submitted==0);
    accept=true; arcade_usb_enumeration_task();
    assert(submitted==1 && lastAddress==1 && lastData==99);
    now=5000; arcade_usb_enumeration_task(); assert(submitted==1);
    tuh_configuration_set_cb(2,1,nullptr,77,desc,sizeof desc);
    tuh_enumeration_cancel_cb();
    now=8000; arcade_usb_enumeration_task(); assert(submitted==1);
    std::puts("USB enumeration lifecycle tests passed");
}
