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
    static inline constexpr int VERSION = 0;

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

    int initPowerOn = 0;
    int dispFPS = 1;
    int buttonDispMode = 0;
    int backLight = 1;
    // int LCDContrast = 0;
    int rapidModeSynchro = 1;
    int softwareRapidSpeed = 10;
    std::array<int, 6> rapidPhase = {0, 1, 0, 1, 0, 1};

    RapidSetting rapidSettings[2];

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
