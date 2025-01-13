/*
 * author : Shuichi TAKANO
 * since  : Sun Apr 07 2024 13:49:27
 */
#pragma once
#include <cstdint>
#include <array>

class Serializer;
class Deserializer;

struct AppConfig
{
    static inline constexpr int VERSION = 3;

    struct RapidSetting
    {
        uint32_t mask = 0;
        int div = 1;
    };

    enum class ButtonDispMode
    {
        INPUT_BUTTONS,
        RAPID_BUTTONS,
        NONE,
    };

    enum class RotEncAxis
    {
        NONE,
        X,
        Y,
        Z,
        RX,
        RY,
        RZ,
        SLIDER,
        DIAL,
        WHEEL,
    };

    struct RotEnc
    {
        int reverse = true;
        int scale = 50;
        int axis = 0;
    };

    int initPowerOn = 0;
    int dispFPS = 1;
    int buttonDispMode = 0;
    int backLight = 1;
    // int LCDContrast = 0;
    int rapidModeSynchro = 1;
    int synchroFetchPhase = 5;
    int softwareRapidSpeed = 10;
    std::array<int, 6> rapidPhase = {0, 1, 0, 1, 0, 1};

    RapidSetting rapidSettings[2];

    std::array<RotEnc, 2> rotEnc{};

    int twinPortMode = false; // 1P, 2P混在モード
    int nAnalogAxis = 0;

public:
    ButtonDispMode getButtonDispMode() const
    {
        return static_cast<ButtonDispMode>(buttonDispMode);
    }

    void resetRapidPhase()
    {
        rapidPhase = {0, 1, 0, 1, 0, 1};
    }

    void serialize(Serializer &s);
    bool deserialize(Deserializer &s);
};
