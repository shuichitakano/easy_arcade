// Check ownership, completion races and bounded stop using the fork implementation.
// Register simulation verifies policy, not RP2040 silicon timing.
#include <cassert>
#include <cstdint>
#include <cstdio>
constexpr uint32_t USB_SIE_CTRL_STOP_TRANS_BITS=16, SIE_CTRL_BASE=0x100;
constexpr uint32_t USB_SIE_STATUS_TRANS_COMPLETE_BITS=1, USB_SIE_STATUS_STALL_REC_BITS=2, USB_SIE_STATUS_RX_TIMEOUT_BITS=4;
static bool stuck;
struct StopRegister {
    uint32_t value=0;
    operator uint32_t() const { return value; }
    void operator=(uint32_t v) { value=stuck?v:v & ~USB_SIE_CTRL_STOP_TRANS_BITS; }
};
struct Registers { StopRegister sie_ctrl; uint32_t buf_status=0,sie_status=0; } regs,clearRegs;
static Registers *usb_hw=&regs, *usb_hw_clear=&clearRegs;
static uint32_t buffer=64,clockUs,irqMask;
static struct { bool active; uint8_t dev_addr; uint32_t *buffer_control; } epx{true,1,&buffer};
static uint8_t tu_edpt_number(uint8_t a) { return a & 15; }
static uint32_t save_and_disable_interrupts() { auto old=irqMask; irqMask=1; return old; }
static void restore_interrupts(uint32_t v) { irqMask=v; }
static uint32_t time_us_32() { return clockUs++; }
static void hw_endpoint_reset_transfer(decltype(epx)* ep) { ep->active=false; }
#include "tinyusb_abort_under_test.inc"
int main() {
    assert(!hcd_edpt_abort_xfer(0,1,0x81) && epx.active);
    assert(!hcd_edpt_abort_xfer(0,2,0) && epx.active && !irqMask);
    regs.buf_status=1;
    assert(!hcd_edpt_abort_xfer(0,1,0) && buffer==64);
    regs.buf_status=0; regs.sie_status=USB_SIE_STATUS_TRANS_COMPLETE_BITS;
    assert(!hcd_edpt_abort_xfer(0,1,0) && epx.active);
    regs.sie_status=0; stuck=true;
    assert(!hcd_edpt_abort_xfer(0,1,0) && epx.active && buffer==64 && !irqMask);
    assert(clockUs>=1000 && clockUs<1010);
    stuck=false; irqMask=1;
    assert(hcd_edpt_abort_xfer(0,1,0) && !epx.active && buffer==0 && irqMask==1);
    assert(clearRegs.buf_status==1);
    assert(!hcd_edpt_abort_xfer(0,1,0)); // cannot cancel a queued completion twice
    std::puts("RP2040 EP0 abort tests passed");
}
