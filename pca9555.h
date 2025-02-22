/*
 * author : Shuichi TAKANO
 * since  : Mon Aug 29 2022 02:41:05
 */

#include <stdint.h>

namespace device
{
    class PCA9555
    {
    private:
        uint8_t addr_ = 0;

    public:
        bool init(int addrSel = 0);

        void setPortDir(uint16_t inputPortBits) const;
        void output(uint16_t bits) const;
        int input() const;

        bool setPortDirNonBlocking(uint16_t inputPortBits) const;

        explicit operator bool() const { return addr_; }
    };
}
