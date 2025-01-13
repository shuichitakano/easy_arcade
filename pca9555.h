/*
 * author : Shuichi TAKANO
 * since  : Mon Aug 29 2022 02:41:05
 */

#include <stdint.h>
#include <hardware/i2c.h>

namespace device
{
    class PCA9555
    {
    private:
        i2c_inst_t *i2c_{};
        uint8_t addr_ = 0;

    public:
        void init(i2c_inst_t *i2cInst, int addrSel = 0);

        void setPortDir(uint16_t inputPortBits) const;
        void output(uint16_t bits) const;
        int input() const;

        explicit operator bool() const { return i2c_; }
    };
}
