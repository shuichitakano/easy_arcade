#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#define TU_ASSERT(condition, ...) do { assert(condition); } while(false)
constexpr unsigned ENUM_ADDR0_DEVICE_DESC=3, ENUM_SET_ADDR=7;
constexpr unsigned ENUM_RESET_DELAY_MS=50;
constexpr unsigned TUSB_SPEED_FULL=0, TUSB_SPEED_LOW=1, TUSB_SPEED_HIGH=2;
struct Bits { bool connection, port_enable, reset, high_speed, low_speed; };
struct hub_port_status_response_t { Bits status, change; };
struct tuh_xfer_t { unsigned user_data; };
static struct { uint8_t hub_addr=9, hub_port=1, speed=0, rhport=0; } _dev0;
static struct { uint8_t ctrl[sizeof(hub_port_status_response_t)]; } _usbh_epbuf;
static unsigned advanced, acknowledged, finished;
static unsigned polled, cancelled, removed, elapsed;
static uintptr_t nextPoll;
static bool accepts=true;
static void process_enumeration(tuh_xfer_t* xfer) { advanced=xfer->user_data; }
static void enum_full_complete() { ++finished; }
static void tuh_enumeration_cancel_cb() { ++cancelled; }
static void process_removing_device(uint8_t rhport,uint8_t hub,uint8_t port) {
    assert(rhport==0 && hub==9 && port==1); ++removed;
}
static void tusb_time_delay_ms_api(unsigned ms) { assert(ms==50); elapsed+=ms; }
static bool hub_port_get_status(uint8_t hub,uint8_t port,uint8_t* buffer,
                               void(*cb)(tuh_xfer_t*),uintptr_t state) {
    assert(hub==9 && port==1 && buffer==_usbh_epbuf.ctrl && cb==process_enumeration);
    ++polled; nextPoll=state; return accepts;
}
static bool hub_port_clear_reset_change(uint8_t hub,uint8_t port,void(*cb)(tuh_xfer_t*),unsigned state) {
    assert(hub==9 && port==1 && cb==process_enumeration); acknowledged=state; return true;
}
#include "hub_reset_status_under_test.inc"
int main() {
    for (unsigned second=0;second<2;++second) for(unsigned mask=0;mask<16;++mask) {
        hub_port_status_response_t status{};
        status.status.connection=mask&1; status.status.port_enable=mask&2;
        status.status.reset=mask&4; status.change.reset=mask&8;
        memcpy(_usbh_epbuf.ctrl,&status,sizeof status);
        advanced=acknowledged=finished=polled=0; tuh_xfer_t xfer{second ? 6u : 2u};
        (second ? reset_status_2 : reset_status_1)(&xfer);
        unsigned next=second ? ENUM_SET_ADDR : ENUM_ADDR0_DEVICE_DESC;
        if (!status.status.connection) assert(finished==1);
        else if(status.status.reset || !status.status.port_enable) assert(polled==1);
        else if(status.change.reset) assert(acknowledged==next);
        else if(status.status.connection && status.status.port_enable && !status.status.reset)
            assert(advanced==next);
        else assert(finished==1);
        assert((advanced!=0)+(acknowledged!=0)+finished+polled==1);
    }
    for (unsigned second=0;second<2;++second) {
        auto run=second ? reset_status_2 : reset_status_1;
        hub_port_status_response_t status{};
        status.status.connection=true; status.status.reset=true;
        memcpy(_usbh_epbuf.ctrl,&status,sizeof status);
        advanced=acknowledged=finished=polled=cancelled=removed=elapsed=0;
        tuh_xfer_t xfer{second ? 6u : 2u};
        for(unsigned i=0;i<10;++i) {
            run(&xfer); assert(finished==0 && polled==i+1);
            assert((nextPoll&255)==(second ? 6u : 2u));
            xfer.user_data=nextPoll;
        }
        run(&xfer);
        assert(finished==1 && cancelled==1 && removed==1 && elapsed==500 && polled==10);
        // A ready response at the last poll must still advance.
        status.status.reset=false; status.status.port_enable=true;
        memcpy(_usbh_epbuf.ctrl,&status,sizeof status);
        run(&xfer); assert(advanced==(second ? ENUM_SET_ADDR : ENUM_ADDR0_DEVICE_DESC));
        // Each new enumeration starts with an unencoded state (fresh budget).
        status.status.reset=true;
        memcpy(_usbh_epbuf.ctrl,&status,sizeof status);
        xfer.user_data=second ? 6u : 2u; run(&xfer);
        assert((nextPoll>>8)==1);
        accepts=false; run(&xfer);
        assert(finished==2 && cancelled==2 && removed==2);
        accepts=true;
    }
    puts("Hub reset status progress tests passed");
}
