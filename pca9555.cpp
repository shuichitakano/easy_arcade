/*
 * author : Shuichi TAKANO
 * since  : Mon Aug 29 2022 02:42:51
 */

#include "pca9555.h"

#include "i2c_manager.h"
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

        inline constexpr int I2C_PRIO = 1;
    }

    bool
    PCA9555::init(int addrSel)
    {
        int addr = 0b0100000 | addrSel;

        addr_ = addr;
        setPortDir(0xffff);

        uint8_t rxdata{};
        auto r = getI2CManager().readBlocking(addr, &rxdata, 1);

        DPRINT(("PCA9555 init[%02x]: %d\n", addr, r));
        if (r < 0)
        {
            addr_ = 0;
            return false;
        }
        addr_ = addr;
        return true;
    }

    bool
    PCA9555::setPortDirNonBlocking(uint16_t inputPortBits) const
    {
        assert(*this);
        uint8_t data[] =
            {
                static_cast<uint8_t>(Command::CONFIGURATION_0),
                static_cast<uint8_t>(inputPortBits & 0xFF),
                static_cast<uint8_t>(inputPortBits >> 8),
            };
        return getI2CManager().sendNonBlocking(I2C_PRIO, addr_, data, 3);
    }

    void
    PCA9555::setPortDir(uint16_t inputPortBits) const
    {
        assert(*this);
        uint8_t data[] =
            {
                static_cast<uint8_t>(Command::CONFIGURATION_0),
                static_cast<uint8_t>(inputPortBits & 0xFF),
                static_cast<uint8_t>(inputPortBits >> 8),
            };
        getI2CManager().sendBlocking(addr_, data, 3, false);
    }

    void
    PCA9555::output(uint16_t bits) const
    {
        assert(*this);
        uint8_t data[] =
            {
                static_cast<uint8_t>(Command::OUTPUT_PORT_0),
                static_cast<uint8_t>(bits & 0xFF),
                static_cast<uint8_t>(bits >> 8),
            };
        getI2CManager().sendBlocking(addr_, data, 3, false);
    }

    int
    PCA9555::input() const
    {
        assert(*this);
        uint8_t cmd = static_cast<uint8_t>(Command::INPUT_PORT_0);
        getI2CManager().sendBlocking(addr_, &cmd, 1, true);

        uint8_t data[2] = {};
        getI2CManager().readBlocking(addr_, data, 2);
        return data[0] | (data[1] << 8);
    }

}
