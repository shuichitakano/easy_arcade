// Exercise the production recovery hook with queued events and HCD races.
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstdio>
struct Request { uint8_t bRequest; uint16_t wValue; };
struct tuh_xfer_t { uintptr_t user_data; uint8_t daddr, result; Request* setup; uint32_t actual_len; uint8_t* buffer; };
using Callback = void(*)(tuh_xfer_t*);
static void process_enumeration(tuh_xfer_t*) {}
static void unrelated(tuh_xfer_t*) {}
constexpr uint8_t CONTROL_STAGE_IDLE=0, XFER_RESULT_FAILED=1;
static struct { bool enumerating; uint8_t hub_port, hub_addr, rhport; } _dev0;
static struct Device { bool connected; } parent{true};
static Device* get_device(uint8_t addr) { assert(addr==9); return &parent; }
static unsigned closes, cancels;
static void hcd_device_close(uint8_t rhport, uint8_t addr) {
    assert(rhport==0 && addr==0); ++closes;
}
static void tuh_enumeration_cancel_cb() { ++cancels; }
static struct { uint8_t stage,daddr; Callback complete_cb; } _ctrl_xfer;
static uint32_t now,_control_progress_ms;
static bool initialized=true, active=true, abortAllowed=true;
static unsigned abortCalls;
static std::vector<int> events;
static bool tuh_inited() { return initialized; }
static uint32_t tusb_time_millis_api() { return now; }
static uint8_t usbh_get_rhport(uint8_t) { return 0; }
static bool hcd_edpt_abort_xfer(uint8_t,uint8_t,uint8_t) {
    ++abortCalls;
    if(!active || !abortAllowed) return false;
    active=false;
    return true;
}
static void hcd_event_xfer_complete(uint8_t addr,uint8_t ep,uint32_t len,uint8_t result,bool isr) {
    assert(addr==1 && ep==0 && len==0 && result==XFER_RESULT_FAILED && !isr);
    events.push_back(result);
}
#include "tinyusb_recovery_under_test.inc"
int main() {
    _dev0={true,1,0,0}; _ctrl_xfer={3,1,process_enumeration};
    now=999; tuh_enumeration_recovery_task(); assert(abortCalls==0);
    // An unrelated attach pending in the queue must not starve recovery.
    events.push_back(99);
    now=1000; tuh_enumeration_recovery_task();
    assert(events.size()==2 && events[0]==99 && events[1]==XFER_RESULT_FAILED);
    assert(_ctrl_xfer.stage==3); // only the normal event consumer releases it
    tuh_enumeration_recovery_task(); assert(events.size()==2); // completion already queued
    active=true; _ctrl_xfer.complete_cb=unrelated;
    unsigned calls=abortCalls; tuh_enumeration_recovery_task(); assert(abortCalls==calls);
    _ctrl_xfer.complete_cb=process_enumeration; abortAllowed=false;
    tuh_enumeration_recovery_task(); assert(events.size()==2 && active);
    abortAllowed=true; _control_progress_ms=0xffffff00u; now=0x2e8;
    tuh_enumeration_recovery_task(); assert(events.size()==3); // wrap-safe 1000 ms
    // An upstream hub was removed during its child's enumeration, with a
    // different attach queued. IDLE EP0 must not leave enumeration locked.
    _dev0={true,1,9,0}; _ctrl_xfer={0,9,unrelated};
    tuh_enumeration_recovery_task(); assert(_dev0.enumerating && closes==0);
    parent.connected=false;
    _ctrl_xfer.stage=3;
    tuh_enumeration_recovery_task(); assert(_dev0.enumerating && closes==0);
    _ctrl_xfer.stage=0;
    initialized=false;
    tuh_enumeration_recovery_task(); assert(closes==0);
    initialized=true;
    tuh_enumeration_recovery_task();
    assert(!_dev0.enumerating && closes==1 && cancels==1);
    tuh_enumeration_recovery_task(); assert(closes==1 && cancels==1);
    _dev0={true,0,0,0};
    tuh_enumeration_recovery_task(); assert(_dev0.enumerating && closes==1);
    std::puts("USB recovery tests passed");
}
