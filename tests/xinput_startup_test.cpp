#include "tusb.h"
#include <cassert>
#include <array>
#include <cstdio>
#include <initializer_list>
static unsigned leds, inputs, vendors;
static bool mounted=true, accept=true;
bool tuh_mounted(uint8_t) { return mounted; }
bool tuh_xinput_send_report(uint8_t,uint8_t,const uint8_t *,uint16_t) {
    ++leds;
    assert(false && "XInput startup must not send an LED command");
    return false;
}
bool tuh_xinput_receive_report(uint8_t addr,uint8_t instance) {
    assert(addr==3 && instance==0 && leds==0); ++inputs; return accept;
}
bool tuh_control_xfer(tuh_xfer_t *) {
    ++vendors;
    assert(false && "XInput startup must not send a vendor request");
    return false;
}
#include "xinput_startup_under_test.inc"
int main() {
    for (bool accepted : {false,true}) {
        auto &pending=xinputInputPending_[2]; pending=true;
        accept=accepted;
        leds=inputs=vendors=0;
        xinputStartupTask(); assert(leds==0 && inputs==1 && vendors==0 && !pending);
        xinputStartupTask(); assert(inputs==1);
        pending=true; xinputStartupTask(); assert(inputs==2); // remount
        pending=false; xinputStartupTask(); assert(inputs==2); // cancelled mount
    }
    auto &pending=xinputInputPending_[2]; pending=true;
    leds=inputs=vendors=0;
    mounted=false; xinputStartupTask(); assert(leds==0 && inputs==0);
    mounted=true; accept=false;
    xinputStartupTask(); assert(leds==0 && inputs==1 && vendors==0 && !pending);
    xinputStartupTask(); assert(leds==0 && inputs==1);
    puts("XInput startup sequence tests passed");
}
