/*
 * author : Shuichi TAKANO
 * since  : Fri Feb 23 2024 15:02:41
 */
#pragma once

#define EA_V2 1

static constexpr bool REVERSE_STATE = false; // 541 なら true
                                             // static constexpr bool REVERSE_STATE = true; // 541 なら true

inline constexpr uint32_t CPU_CLOCK = 125000000;

#if EA_V2

#define BOARD_NAME "EA2"
#define BOARD_VERSION "V1.00"

inline constexpr bool HAS_LCD = true;
inline constexpr int I2C_SDA_PIN = 26;
inline constexpr int I2C_SCL_PIN = 27;

inline constexpr int PDPG_PIN = 0;

inline constexpr bool LED_ACTIVE_LOW = true;

inline constexpr bool HAS_POWER_BUTTON = true;
inline constexpr int POWER_EN_PIN = 29;

static constexpr uint32_t BUTTON_GPIO_MASK = 0b1111111111111111111111110;

enum class ButtonGPIO
{
    LEFT2 = 1,
    RIGHT1,
    RIGHT2,
    A1,
    A2,
    B1,
    B2,
    C1,
    C2,
    D1,
    D2,
    E1,
    E2,
    F1,
    F2,
    LEFT1,
    DOWN2,
    DOWN1,
    UP2,
    UP1,
    START2,
    START1,
    COIN2,
    COIN1,
};
#else

#define BOARD_NAME "EasyArcade"
#define BOARD_VERSION "V1.00"
inline constexpr bool HAS_LCD = false;
inline constexpr int I2C_SDA_PIN = 0;
inline constexpr int I2C_SCL_PIN = 0;

inline constexpr bool LED_ACTIVE_LOW = false;

inline constexpr bool HAS_POWER_BUTTON = false;
inline constexpr int POWER_EN_PIN = 0;

static constexpr uint32_t BUTTON_GPIO_MASK = 0b1100011111111111111111111110;

enum class ButtonGPIO
{
    C1 = 1,
    C2,
    D1,
    D2,
    E1,
    E2,
    F1,
    F2,
    B1,
    A2,
    A1,
    RIGHT2,
    RIGHT1,
    LEFT2,
    LEFT1,
    COIN1,
    COIN2,
    START1,
    START2,
    UP1,
    UP2,
    DOWN1,
    DOWN2 = 26,
    B2,
};
#endif
