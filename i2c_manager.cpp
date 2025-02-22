/*
 * author : Shuichi TAKANO
 * since  : Wed Feb 12 2025 01:53:13
 */

#include "i2c_manager.h"

namespace
{
    I2CManager inst_;

    bool isI2CBusy(i2c_inst_t *i2c)
    {
        return !(i2c->hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_EMPTY_BITS);
    }

    void wait(i2c_inst_t *i2c)
    {
        while (isI2CBusy(i2c))
        {
            tight_loop_contents();
        }
    }
}

I2CManager &getI2CManager()
{
    return inst_;
}

void I2CManager::init(i2c_inst_t *i2c)
{
    i2c_ = i2c;
}

bool I2CManager::reserve(int priority, uint8_t addr, size_t size)
{
    if (size < 1)
    {
        return true;
    }

    if (priority < priorityThreshold_)
    {
        return false;
    }

    if (i2c_get_write_available(i2c_) < size)
    {
        return false;
    }

    if (curAddr_ != addr)
    {
        if (isI2CBusy(i2c_))
        {
            priorityThreshold_ = priority;
            return false;
        }
    }
    return true;
}

bool I2CManager::sendNonBlocking(int priority,
                                 uint8_t addr, const uint8_t *data, size_t size,
                                 bool cont)
{
    if (size && reserve(priority, addr, size))
    {
        if (curAddr_ != addr)
        {
            i2c_->hw->enable = 0;
            i2c_->hw->tar = addr;
            i2c_->hw->enable = 1;
            curAddr_ = addr;
        }

        for (size_t i = 0; i < size - 1; ++i)
        {
            i2c_->hw->data_cmd = data[i];
        }
        i2c_->hw->data_cmd = data[size - 1] | (cont ? 0 : I2C_IC_DATA_CMD_STOP_BITS);
        priorityThreshold_ = 0;
        return true;
    }
    return false;
}

void I2CManager::sendNonBlockingUnsafe(uint8_t addr, const uint8_t *data, size_t size,
                                       bool cont)
{
    if (size)
    {
        if (curAddr_ != addr)
        {
            i2c_->hw->enable = 0;
            i2c_->hw->tar = addr;
            i2c_->hw->enable = 1;
            curAddr_ = addr;
        }

        for (size_t i = 0; i < size - 1; ++i)
        {
            i2c_->hw->data_cmd = data[i];
        }
        i2c_->hw->data_cmd = data[size - 1] | (cont ? 0 : I2C_IC_DATA_CMD_STOP_BITS);
        priorityThreshold_ = 0;
    }
}

void I2CManager::sendBlocking(uint8_t addr, const uint8_t *data, size_t size, bool cont)
{
    wait(i2c_);
    i2c_write_blocking(i2c_, addr, data, size, cont);
    resetAddr();
}

int I2CManager::readBlocking(uint8_t addr, uint8_t *data, size_t size)
{
    wait(i2c_);
    int r = i2c_read_blocking(i2c_, addr, data, size, false);
    resetAddr();
    return r;
}
