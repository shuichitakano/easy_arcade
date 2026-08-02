#pragma once

#include "macro_profile.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Macro
{
inline constexpr int PROFILE_SLOTS = 8;

class Storage
{
public:
    void init();

    std::vector<std::string> listUsbFiles() const;
    Result importUsbFile(const std::string &filename);

    bool select(int slot);
    int activeSlot() const { return activeSlot_; }
    bool enabled() const { return activeSlot_ >= 0; }
    const Profile *activeProfile() const { return enabled() ? &activeProfile_ : nullptr; }
    const char *slotFilename(int slot) const;
    const char *slotProfileName(int slot) const;
    bool slotUsed(int slot) const;

private:
    bool rewriteBank(int newActiveSlot, int replacementSlot,
                     const std::string *filename,
                     const std::vector<uint8_t> *replacementData,
                     const Profile *replacementProfile);
    bool loadActiveProfile();
    void loadState();
    void saveState();

    struct SlotInfo
    {
        bool used = false;
        uint32_t size = 0;
        uint32_t crc = 0;
        uint32_t lastUse = 0;
        std::string filename;
        std::string profileName;
    };

    std::array<SlotInfo, PROFILE_SLOTS> slots_{};
    Profile activeProfile_;
    int activeSlot_ = -1;
    int activeBank_ = -1;
    uint32_t generation_ = 0;
    uint32_t useCounter_ = 0;
};
}
