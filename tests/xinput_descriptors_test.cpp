#include "usb_enumeration.h"
#include <cassert>
#include <cstdio>
static uint64_t now;
static bool accept=true;
static unsigned submissions, configurations;
static tuh_xfer_t pending;
static tusb_control_request_t request;
uint64_t get_absolute_time() { return now; }
extern "C" void tuh_enumeration_recovery_task() {}
bool tuh_vid_pid_get(uint8_t, uint16_t *v,uint16_t *p) { *v=0x045e; *p=0x028e; return true; }
bool tuh_control_xfer(tuh_xfer_t *xfer) {
    if (!accept) return false;
    pending=*xfer; request=*xfer->setup; ++submissions; return true;
}
bool tuh_configuration_set(uint8_t addr,uint8_t cfg,tuh_xfer_cb_t,uintptr_t data) {
    assert(addr==3 && cfg==1 && data==42);
    if (!accept) return false;
    ++configurations; return true;
}
int main() {
    uint8_t descriptor[16]={};
    for (uint8_t result=0; result<3; ++result) {
        submissions=configurations=0;
        tuh_configuration_set_cb(3,1,nullptr,42,descriptor,sizeof descriptor);
        accept=false; arcade_usb_enumeration_task(); assert(submissions==0);
        accept=true;
        arcade_usb_enumeration_task(); assert(submissions==1 && configurations==0);
        assert(request.bmRequestType==0x80 && request.bRequest==6 && request.wValue==0x600);
        assert(request.wLength==10 && request.wIndex==0);
        assert(pending.buffer!=descriptor && pending.daddr==3);
        arcade_usb_enumeration_task(); assert(submissions==1 && configurations==0);
        pending.result=result; pending.complete_cb(&pending); // Includes STALL.
        // The descriptor completed at now == 0: no additional delay.
        accept=false; arcade_usb_enumeration_task(); assert(configurations==0);
        accept=true; arcade_usb_enumeration_task(); assert(configurations==1);
        arcade_usb_enumeration_task(); assert(configurations==1);
        tuh_configuration_set_cb(3,1,nullptr,42,descriptor,sizeof descriptor);
        arcade_usb_enumeration_task();
        arcade_usb_enumeration_removed(3);
        pending.complete_cb(&pending); // Late callback must not restart after cancellation.
        arcade_usb_enumeration_task(); assert(submissions==2 && configurations==1);
    }
    puts("XInput descriptor startup sequence tests passed");
}
