/*
 * author : Shuichi TAKANO
 * since  : Sat Feb 24 2024 22:26:00
 */

#include "lcd.h"
#include <cassert>
#include <array>

namespace
{
    inline constexpr uint8_t LCD_ADDR = 0x7c >> 1;

    void enableI2CAddr(i2c_inst_t *i2c, uint8_t addr)
    {
        i2c->hw->enable = 0;
        i2c->hw->tar = addr;
        i2c->hw->enable = 1;
    }

    void writeI2CNonBloking(i2c_inst_t *i2c,
                            const uint8_t *data, size_t size)
    {
        if (size < 1)
        {
            return;
        }

        assert(i2c_get_write_available(i2c) >= size);
        for (size_t i = 0; i < size - 1; ++i)
        {
            i2c->hw->data_cmd = data[i];
        }
        i2c->hw->data_cmd = data[size - 1] | I2C_IC_DATA_CMD_STOP_BITS;
    }

    void waitForI2CNonBlocking(i2c_inst_t *i2c)
    {
        // while (i2c->hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS)
        while (!(i2c->hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_EMPTY_BITS))
        {
            tight_loop_contents();
        }
    }

    bool isI2CBusy(i2c_inst_t *i2c)
    {
        return !(i2c->hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_EMPTY_BITS);
        // return i2c->hw->status & I2C_IC_STATUS_ACTIVITY_BITS;
    } // namespace
}

LCD &LCD::instance()
{
    static LCD lcd;
    return lcd;
}

void LCD::init(i2c_inst_t *i2c)
{
    i2c_ = i2c;
    assert(i2c_);

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
    i2c_write_blocking(i2c_, LCD_ADDR, buf.data(), buf.size(), false);
    sleep_us(27);
    needSetAddr_ = true;
}

void LCD::writeData(uint8_t data)
{
    std::array<uint8_t, 2> buf = {0x40, data};
    i2c_write_blocking(i2c_, LCD_ADDR, buf.data(), buf.size(), false);
    needSetAddr_ = true;
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

int LCD::getAvailableNonBlockingSize()
{
    return i2c_get_write_available(i2c_) / 2;
}

void LCD::writeInstructionNonBlocking(uint8_t cmd)
{
    setAddrForNonBlocking();
    std::array<uint8_t, 2> buf = {0x00, cmd};
    writeI2CNonBloking(i2c_, buf.data(), buf.size());
}

void LCD::writeDataNonBlocking(uint8_t data)
{
    setAddrForNonBlocking();
    std::array<uint8_t, 2> buf = {0x40, data};
    writeI2CNonBloking(i2c_, buf.data(), buf.size());
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
    if (i2c_get_write_available(i2c_) < 2 + 1 + n)
    {
        return false;
    }

    locateNonBlocking(x, y);
    i2c_write_byte_raw(i2c_, 0x40);
    writeI2CNonBloking(i2c_, reinterpret_cast<const uint8_t *>(s), n);
    return true;
}

bool LCD::defineCharNonBlocking(int n, const uint8_t *data)
{
    if (i2c_get_write_available(i2c_) < 2 + 9)
    {
        return false;
    }

    writeInstructionNonBlocking(0x40 | (n << 3));

    i2c_write_byte_raw(i2c_, 0x40);
    writeI2CNonBloking(i2c_, data, 8);
    return true;
}

void LCD::waitForNonBlocking()
{
    waitForI2CNonBlocking(i2c_);
}

void LCD::setAddrForNonBlocking()
{
    if (needSetAddr_)
    {
        enableI2CAddr(i2c_, LCD_ADDR);
        needSetAddr_ = false;
    }
}

void LCD::setContrast(int v)
{
    writeInstruction(0x70 + (v & 15));
    //    writeInstruction(0x54 + (v >> 4));
}

void LCD::setDisplayOnOff(bool on)
{
    if (dispOn_ != on)
    {
        waitForNonBlocking();
        writeInstruction(on ? 0x0c : 0x08);
        writeInstruction(on ? 0x56 : 0x50);

        dispOn_ = on;
    }
}
