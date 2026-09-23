#include <cassert>
#include <cstdint>
#include <tuple>
#include <cstdio>
#define DPRINT(x) do {} while (false)
constexpr uint8_t CFG_TUH_DEVICE_MAX=7, CFG_TUH_HUB=2, HUB1_ADDR=9;
static struct Hub { uint8_t port_count; } hubs[2];
static Hub* get_itf(uint8_t addr) { assert(addr>=8 && addr<=9); return &hubs[addr-8]; }
static bool usbInitialized_=true;
static uint8_t hub0Port_[2]{0,1}, hub1PortOffset_,hub1PortCount_;
static int parentPort;
static std::tuple<int,int> getHubPort(uint8_t addr) { assert(addr==9); return {0,parentPort}; }
static void resetExtHubPortInfo() {
    hub0Port_[0]=0; hub0Port_[1]=1; hub1PortOffset_=hub1PortCount_=0;
}
// No USB submission stub: production code must only read cached data.
#include "ext_hub_cache_under_test.inc"
int main() {
    for (unsigned i=0;i<256;++i) assert(hub_port_count(i)==0);
    hubs[1].port_count=4;
    checkExtHub(); assert(hub1PortCount_==4 && hub1PortOffset_==0 && hub0Port_[1]==4);
    parentPort=1;
    checkExtHub(); assert(hub1PortOffset_==1 && hub0Port_[1]==1);
    hubs[1].port_count=0;
    usbInitialized_=false; checkExtHub(); assert(hub1PortCount_==4);
    usbInitialized_=true; checkExtHub();
    assert(hub1PortCount_==0 && hub1PortOffset_==0 && hub0Port_[1]==1);
    std::puts("External hub cached mapping tests passed");
}
