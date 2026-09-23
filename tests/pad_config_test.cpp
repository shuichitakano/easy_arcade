#include "pad_state.h"
#include <cassert>
#include <cstdio>

// Only the UI/save side effects are stubbed; indexing and ButtonSet are production code.
class PadManager {
public:
    static constexpr int N_BUTTONS = 128;
    bool twinPortMode_ = false, virtualButtonConfig_ = false;
#include "button_set.inc"
    class ButtonConfigMode {
    public:
        virtual ~ButtonConfigMode() = default;
        int curButton_ = 0, saves = 0;
        ButtonSet curButtonSet_;
        std::vector<ButtonSet> buttonSets_;
        virtual int nButtons(const PadManager &) const;
        virtual void next(PadManager &);
        void appendUnit(const PadManager &, std::vector<PadConfig::Unit>[2], PadConfig::Unit &, int);
        void saveAndExit(PadManager &) { ++saves; }
        void printMessage(const PadManager &) const {}
    };
    class AnalogConfigMode : public ButtonConfigMode {
    public:
        int count = 4;
        int nButtons(const PadManager &) const override { return count; }
        void next(PadManager &) override;
    };
};
#include "button_config.inc"

int main() {
    for (bool twin : {false, true}) for (bool virt : {false, true}) {
        PadManager mgr; mgr.twinPortMode_ = twin; mgr.virtualButtonConfig_ = virt;
        PadManager::ButtonConfigMode mode;
        std::vector<int> visited;
        while (!mode.saves) {
            const int logical = mode.curButton_;
            assert(logical < mode.nButtons(mgr));
            visited.push_back(logical);
            mode.curButtonSet_.buttons[0] = 1;
            mode.next(mgr);
            assert(mode.buttonSets_.size() == static_cast<size_t>(logical + 1));
            assert(mode.buttonSets_[logical].getButton(0));
            assert(!mode.curButtonSet_.hasData());
        }
        const int perPort = virt ? 16 : 12; // COIN through J/F, excluding shared CMD.
        assert(visited.size() == static_cast<size_t>(1 + perPort * (twin ? 2 : 1)));
        std::vector<PadConfig::Unit> units[2];
        for (size_t index = 0; index < mode.buttonSets_.size(); ++index) {
            if (!mode.buttonSets_[index].hasData()) continue;
            PadConfig::Unit u{};
            mode.appendUnit(mgr, units, u, index);
        }
        for (int port = 0; port < (twin ? 2 : 1); ++port) {
            assert(units[port].size() == static_cast<size_t>(1 + perPort));
            for (size_t i = 0; i < units[port].size(); ++i)
                assert(units[port][i].index == i); // Includes correct 2P COIN/A and shared CMD.
        }
        if (!twin) assert(units[1].empty());
        if (twin && !virt) {
            for (int i = static_cast<int>(PadStateButton::G); i <= static_cast<int>(PadStateButton::J); ++i)
                assert(!mode.buttonSets_[i].hasData());
        }
    }
    for (int count : {4, 8}) for (bool virt : {false, true}) {
        PadManager mgr; mgr.virtualButtonConfig_ = virt;
        PadManager::AnalogConfigMode mode; mode.count = count;
        for (int i = 0; i < count; ++i) {
            mode.curButtonSet_.analogIndex = i / 2;
            mode.curButtonSet_.analogOn = PadConfig::AnalogPos::H;
            mode.curButtonSet_.analogOff = PadConfig::AnalogPos::MID;
            mode.curButtonSet_.buttons[0] = 1;
            mode.next(mgr);
            assert(mode.buttonSets_[i].analogIndex == i / 2);
            assert(!mode.buttonSets_[i].getButton(0));
        }
        assert(mode.saves == 1 && mode.buttonSets_.size() == static_cast<size_t>(count));
    }
    puts("Pad configuration indexing tests passed");
}
