/*
 * author : Shuichi TAKANO
 * since  : Mon Aug 29 2022 02:42:51
 */

#include "pca9555.h"

#include "debug.h"
#include <cassert>

namespace device
{
    namespace
    {
        enum class Command
        {
            INPUT_PORT_0,
            INPUT_PORT_1,
            OUTPUT_PORT_0,
            OUTPUT_PORT_1,
            POLARITY_INVERSION_0,
            POLARITY_INVERSION_1,
            CONFIGURATION_0,
            CONFIGURATION_1,
        };
    }

    void
    PCA9555::init(i2c_inst_t *i2cInst, int addrSel)
    {
        addr_ = 0b0100000 | addrSel;

        uint8_t rxdata{};
        // auto r = i2c_read_timeout_per_char_us(i2cInst, addr_, &rxdata, 1, false, 100);
        auto r = i2c_read_blocking(i2cInst, addr_, &rxdata, 1, false);

        DPRINT(("PCA9555 init[%02x]: %d\n", addr_, r));
        if (r >= 0)
        {
            i2c_ = i2cInst;
        }
        {
            uint8_t wd[] = {6, 0xaa};
            i2c_write_blocking(i2cInst, addr_, wd, 2, false);
        }
        {
            static uint8_t v = 0;
            v ^= 255;
            uint8_t wd[] = {2, v};
            i2c_write_blocking(i2cInst, addr_, wd, 2, false);
        }
    }

    void
    PCA9555::setPortDir(uint16_t inputPortBits) const
    {
        assert(i2c_);
        uint8_t data[] = {static_cast<uint8_t>(Command::CONFIGURATION_0), static_cast<uint8_t>(inputPortBits & 0xFF)};
        i2c_write_blocking(i2c_, addr_, data, 2, false);

        data[0] = static_cast<uint8_t>(Command::CONFIGURATION_1);
        data[1] = static_cast<uint8_t>((inputPortBits >> 8) & 0xFF);
        i2c_write_blocking(i2c_, addr_, data, 2, false);
    }

    void
    PCA9555::output(uint16_t bits) const
    {
        assert(i2c_);
        uint8_t data[] = {static_cast<uint8_t>(Command::OUTPUT_PORT_0), static_cast<uint8_t>(bits & 0xFF)};
        i2c_write_blocking(i2c_, addr_, data, 2, false);

        data[0] = static_cast<uint8_t>(Command::OUTPUT_PORT_1);
        data[1] = static_cast<uint8_t>((bits >> 8) & 0xFF);
        i2c_write_blocking(i2c_, addr_, data, 2, false);
    }

    int
    PCA9555::input() const
    {
        assert(i2c_);
        uint8_t cmd = static_cast<uint8_t>(Command::INPUT_PORT_0);
        i2c_write_blocking(i2c_, addr_, &cmd, 1, true);

        uint8_t data[2] = {0};
        i2c_read_blocking(i2c_, addr_, data, 2, false);
        return data[0] | (data[1] << 8);
    }

}
