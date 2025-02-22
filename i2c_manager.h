/*
 * author : Shuichi TAKANO
 * since  : Wed Feb 12 2025 01:41:44
 */

#include <cstdint>
#include <cstring>
#include <hardware/i2c.h>

class I2CManager
{
public:
    void init(i2c_inst_t *i2c);

    bool reserve(int priority, uint8_t addr, size_t size);
    bool sendNonBlocking(int priority,
                         uint8_t addr, const uint8_t *data, size_t size, bool cont = false);
    void sendNonBlockingUnsafe(uint8_t addr, const uint8_t *data, size_t size, bool cont = false);
    void sendBlocking(uint8_t addr, const uint8_t *data, size_t size, bool cont = false);
    int readBlocking(uint8_t addr, uint8_t *data, size_t size);

protected:
    void resetAddr() { curAddr_ = 0; }

private:
    static constexpr size_t DATA_SIZE_MAX = 16;

    i2c_inst_t *i2c_{};
    uint8_t curAddr_{};

    int priorityThreshold_{}; // 今受け付けるpriorityの最小値
};

I2CManager &getI2CManager();
