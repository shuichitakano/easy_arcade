/*
 * author : Shuichi TAKANO
 * since  : Sat Feb 24 2024 22:26:00
 */

#include "lcd.h"
#include "i2c_manager.h"
#include <cassert>
#include <array>

namespace
{
    inline constexpr uint8_t LCD_ADDR = 0x7c >> 1;
    inline constexpr int I2C_PRIO = 0;
}

LCD &LCD::instance()
{
    static LCD lcd;
    return lcd;
}

void LCD::init()
{
    sleep_ms(40);

    uint8_t data[] = {
        0x38, // function set
        0x39, // function set
        0x14, // internal OSC frequency
        0x70, // contrast set
        0x56, // power/icon/contrast control
        0x6c, // follower control
        0x38, // function set
        0x0c, // display on
        0x01, // clear display
    };
    for (auto d : data)
    {
        writeInstruction(d);
    }
    sleep_ms(2); // clear display が遅い
}

void LCD::writeInstruction(uint8_t cmd)
{
    std::array<uint8_t, 2> buf = {0x00, cmd};
    getI2CManager().sendBlocking(LCD_ADDR, buf.data(), buf.size());
    sleep_us(27);
}

void LCD::writeData(uint8_t data)
{
    std::array<uint8_t, 2> buf = {0x40, data};
    getI2CManager().sendBlocking(LCD_ADDR, buf.data(), buf.size());
}

void LCD::puts(const char *s)
{
    while (char ch = *s++)
    {
        if (ch == 160)
        {
            ch = 0;
        }
        writeData(ch);
    }
}

void LCD::locate(int x, int y)
{
    writeInstruction(0x80 | ((y ? 0x40 : 0) + x));
}

void LCD::clear()
{
    writeInstruction(0x01);
    sleep_ms(2);
}

void LCD::defineChar(int n, const uint8_t *data)
{
    writeInstruction(0x40 | (n << 3));
    for (int i = 0; i < 8; ++i)
    {
        writeData(data[i]);
    }
}

void LCD::writeInstructionNonBlocking(uint8_t cmd)
{
    std::array<uint8_t, 2> buf = {0x00, cmd};
    getI2CManager().sendNonBlocking(I2C_PRIO, LCD_ADDR, buf.data(), buf.size());
}

void LCD::writeDataNonBlocking(uint8_t data)
{
    std::array<uint8_t, 2> buf = {0x40, data};
    getI2CManager().sendNonBlocking(I2C_PRIO, LCD_ADDR, buf.data(), buf.size());
}

void LCD::locateNonBlocking(int x, int y)
{
    writeInstructionNonBlocking(0x80 | ((y ? 0x40 : 0) + x));
}

void LCD::putCharNonBlocking(char ch)
{
    writeDataNonBlocking(ch);
}

bool LCD::printNonBlocking(int x, int y, const char *s, int n)
{
    auto &i2c = getI2CManager();
    if (!i2c.reserve(I2C_PRIO, LCD_ADDR, 2 + 1 + n))
    {
        return false;
    }

    locateNonBlocking(x, y);

    // i2c_write_byte_raw(i2c_, 0x40);
    uint8_t cmd = 0x40;
    i2c.sendNonBlockingUnsafe(LCD_ADDR, &cmd, 1, true);
    i2c.sendNonBlockingUnsafe(LCD_ADDR, reinterpret_cast<const uint8_t *>(s), n);
    return true;
}

bool LCD::defineCharNonBlocking(int n, const uint8_t *data)
{
    auto &i2c = getI2CManager();
    if (!i2c.reserve(I2C_PRIO, LCD_ADDR, 2 + 9))
    {
        return false;
    }

    writeInstructionNonBlocking(0x40 | (n << 3));

    uint8_t cmd = 0x40;
    i2c.sendNonBlockingUnsafe(LCD_ADDR, &cmd, 1, true);
    i2c.sendNonBlockingUnsafe(LCD_ADDR, data, 8);
    return true;
}

void LCD::setContrast(int v)
{
    writeInstruction(0x39); // function set IS=1
    writeInstruction(0x70 + (v & 15));
    writeInstruction(0x38); // function set IS=0
}

void LCD::setDisplayOnOff(bool on)
{
    if (dispOn_ != on)
    {
        writeInstruction(on ? 0x0c : 0x08);
        writeInstruction(on ? 0x56 : 0x50);

        dispOn_ = on;
    }
}
