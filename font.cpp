/*
 * author : Shuichi TAKANO
 * since  : Mon Mar 04 2024 05:02:01
 */

#include "font.h"
#include <array>

// 1/Nのフォント
const uint8_t *get1_NFont(int i)
{
    static constexpr uint8_t data[][8] = {
        // 1/1
        {
            0b10000,
            0b10010,
            0b10100,
            0b01010,
            0b10110,
            0b00010,
            0b00010,
            0b00111,
        },
        // 1/2
        {
            0b10000,
            0b10010,
            0b10100,
            0b01110,
            0b10001,
            0b00010,
            0b00100,
            0b01111,
        },
        // 1/3
        {
            0b10000,
            0b10010,
            0b10100,
            0b01110,
            0b10001,
            0b00110,
            0b00001,
            0b01110,
        },
        // 1/4
        {
            0b10000,
            0b10010,
            0b10100,
            0b01001,
            0b10011,
            0b00101,
            0b01111,
            0b00001,
        },
    };
    return i >= 1 && i <= 4 ? data[i - 1] : nullptr;
}

// ピリオド付き数値
const uint8_t *getNumberWithDotFont(int i)
{
    static constexpr uint8_t data[][8] = {
        {
            0b01110,
            0b10001,
            0b10011,
            0b10101,
            0b11001,
            0b10001,
            0b01110,
            0b00001,
        },
        {
            0b00100,
            0b01100,
            0b00100,
            0b00100,
            0b00100,
            0b00100,
            0b01110,
            0b00001,
        },
        {
            0b01110,
            0b10001,
            0b00001,
            0b00010,
            0b00100,
            0b01000,
            0b11110,
            0b00001,
        },
        {
            0b11111,
            0b00010,
            0b00100,
            0b00010,
            0b00001,
            0b10001,
            0b01110,
            0b00001,
        },
        {
            0b00010,
            0b00110,
            0b01010,
            0b10010,
            0b11111,
            0b00010,
            0b00010,
            0b00001,
        },
        {
            0b11111,
            0b10000,
            0b11110,
            0b00001,
            0b00001,
            0b10001,
            0b01110,
            0b00001,
        },
        {
            0b00110,
            0b01000,
            0b10000,
            0b11110,
            0b10001,
            0b10001,
            0b01110,
            0b00001,
        },
        {
            0b11111,
            0b00001,
            0b00010,
            0b00100,
            0b01000,
            0b01000,
            0b01000,
            0b00001,
        },
        {
            0b01110,
            0b10001,
            0b10001,
            0b01110,
            0b10001,
            0b10001,
            0b01110,
            0b00001,
        },
        {
            0b01110,
            0b10001,
            0b10001,
            0b01111,
            0b00001,
            0b00010,
            0b01100,
            0b00001,
        },
    };
    return data[i];
}

// プレイヤー枠のフォント
const uint8_t *getPlayerFont(int i)
{
    static constexpr uint8_t data[][8] = {
        {
            0b01001,
            0b11001,
            0b01001,
            0b01001,
            0b01001,
            0b11101,
            0b00001,
            0b11111,
        },
        {
            0b11001,
            0b00101,
            0b00101,
            0b01001,
            0b10001,
            0b11101,
            0b00001,
            0b11111,
        },
    };
    return i >= 0 && i < 2 ? data[i] : nullptr;
}

const uint8_t *getLeftRightFont()
{
    static constexpr uint8_t data[8] = {
        0b00000,
        0b00000,
        0b01010,
        0b11011,
        0b01010,
        0b00000,
        0b00000,
        0b00000,
    };
    return data;
}

const uint8_t *getLeftFont()
{
    static constexpr uint8_t data[8] = {
        0b00000,
        0b00000,
        0b01000,
        0b11000,
        0b01000,
        0b00000,
        0b00000,
        0b00000,
    };
    return data;
}

const uint8_t *getRightFont()
{
    static constexpr uint8_t data[8] = {
        0b00000,
        0b00000,
        0b00010,
        0b00011,
        0b00010,
        0b00000,
        0b00000,
        0b00000,
    };
    return data;
}

const uint8_t *getUpDownFont()
{
    static constexpr uint8_t data[8] = {
        0b00000,
        0b00100,
        0b01110,
        0b00000,
        0b01110,
        0b00100,
        0b00000,
        0b00000,
    };
    return data;
}

const uint8_t *getUpFont()
{
    static constexpr uint8_t data[8] = {
        0b00000,
        0b00100,
        0b01110,
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b00000,
    };
    return data;
}

const uint8_t *getDownFont()
{
    static constexpr uint8_t data[8] = {
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b01110,
        0b00100,
        0b00000,
        0b00000,
    };
    return data;
}

const uint8_t *get_sFont()
{
    static constexpr uint8_t data[8] = {
        0b00010,
        0b00100,
        0b01000,
        0b10111,
        0b01000,
        0b00110,
        0b00001,
        0b01110,
    };
    return data;
}

const uint8_t *getUpArrowFont()
{
    static constexpr uint8_t data[8] = {
        0b00100,
        0b01110,
        0b10101,
        0b00100,
        0b00100,
        0b00100,
        0b00100,
        0b00000,
    };
    return data;
}