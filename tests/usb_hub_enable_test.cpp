#include <cassert>
#include <cstdint>
#include <cstdio>
#define TU_ASSERT(condition, ...) do { if (!(condition)) return; } while (false)
#define TU_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
constexpr unsigned XFER_RESULT_SUCCESS = 0;
enum { HUB_FEATURE_PORT_CONNECTION_CHANGE, HUB_FEATURE_PORT_ENABLE_CHANGE,
       HUB_FEATURE_PORT_SUSPEND_CHANGE, HUB_FEATURE_PORT_OVER_CURRENT_CHANGE,
       HUB_FEATURE_PORT_RESET_CHANGE };
struct Bits {
    bool connection{}, port_enable{}, port_power{}, over_current{}, suspend{}, reset{};
};
struct hub_interface_t { uint8_t enable_recovery_count[8]{}; struct { Bits status, change; } port_status; } hub;
struct Request { uint16_t wIndex; } request{2};
struct tuh_xfer_t { unsigned result; uint8_t daddr; Request* setup; } transfer{0, 8, &request};
using Callback = void (*)(tuh_xfer_t*);
static unsigned clears, polls, feature;
static Callback completion;
static bool accepted = true;
static uint16_t tu_le16toh(uint16_t v) { return v; }
static hub_interface_t* get_itf(uint8_t addr) { assert(addr == 8); return &hub; }
static void connection_clear_conn_change_complete(tuh_xfer_t*) {}
static void hub_clear_feature_complete_stub(tuh_xfer_t*) {}
static bool hub_port_clear_feature(uint8_t addr, uint8_t port, unsigned f, Callback cb, unsigned data) {
    assert(addr == 8 && port == request.wIndex && data == 0);
    ++clears; feature = f; completion = cb; return accepted;
}
static bool hub_edpt_status_xfer(uint8_t addr) { assert(addr == 8); ++polls; return true; }
#include "hub_enable_under_test.inc"
int main() {
    // Exhaust the status combinations: reset only powered, connected,
    // disabled ports with no over-current, suspend or reset in progress.
    for (unsigned mask = 0; mask < 64; ++mask) {
        hub = {};
        hub.port_status.status = {bool(mask&1), bool(mask&2), bool(mask&4),
                                  bool(mask&8), bool(mask&16), bool(mask&32)};
        hub.port_status.change.port_enable = true;
        clears = polls = 0;
        hub_port_get_status_complete(&transfer);
        assert(clears == 1 && polls == 0 && feature == HUB_FEATURE_PORT_ENABLE_CHANGE);
        assert(completion == (mask == 5 ? connection_clear_conn_change_complete : hub_clear_feature_complete_stub));
    }
    hub = {};
    hub.port_status.status = {true, false, true, false, false, false};
    hub.port_status.change.port_enable = true;
    for (unsigned i = 0; i < 3; ++i) {
        hub_port_get_status_complete(&transfer);
        assert(completion == (i < 2 ? connection_clear_conn_change_complete : hub_clear_feature_complete_stub));
    }
    assert(hub.enable_recovery_count[2] == 2);
    request.wIndex = 1; // Another port has a separate budget.
    hub_port_get_status_complete(&transfer);
    assert(completion == connection_clear_conn_change_complete);
    assert(hub.enable_recovery_count[1] == 1 && hub.enable_recovery_count[2] == 2);
    accepted = false;
    hub_port_get_status_complete(&transfer);
    assert(hub.enable_recovery_count[1] == 1);
    accepted = true;
    request.wIndex = 8; // Outside bitmap: never index the array.
    hub_port_get_status_complete(&transfer);
    assert(completion == hub_clear_feature_complete_stub);
    request.wIndex = 2;
    hub.port_status.change.connection = hub.port_status.change.port_enable = true;
    hub_port_get_status_complete(&transfer);
    assert(feature == HUB_FEATURE_PORT_CONNECTION_CHANGE);
    assert(completion == connection_clear_conn_change_complete);
    assert(hub.enable_recovery_count[2] == 0 && hub.enable_recovery_count[1] == 1);
    hub = {}; clears = polls = 0;
    hub_port_get_status_complete(&transfer);
    assert(clears == 0 && polls == 1);
    transfer.result = 1;
    hub.port_status.change.port_enable = true;
    hub_port_get_status_complete(&transfer);
    assert(clears == 0 && polls == 1);
    std::puts("USB hub disabled-port recovery tests passed");
}
