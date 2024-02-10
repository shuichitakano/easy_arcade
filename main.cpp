#include <stdio.h>
#include <pico/stdlib.h>
#include <hardware/watchdog.h>
#include <hardware/gpio.h>
#include <hardware/adc.h>
#include <hardware/dma.h>
#include <hardware/structs/systick.h>
#include <hardware/structs/ioqspi.h>
#include <stdint.h>
#include <tusb.h>
#include <algorithm>
#include "pad_manager.h"
#include "pad_state.h"
#include "led.h"

#ifdef RASPBERRYPI_PICO_W
#include "pico/cyw43_arch.h"
#endif

static constexpr bool REVERSE_STATE = false; // 541 なら true
// static constexpr bool REVERSE_STATE = true; // 541 なら true

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

static constexpr uint32_t BUTTON_GPIO_MASK = 0b1100011111111111111111111110;

namespace
{
    struct PortMap
    {
        PadStateButton padState;
        ButtonGPIO gpio;
    };
    const PortMap padPortMap_[][12] = {
        {
            {PadStateButton::LEFT, ButtonGPIO::LEFT1},
            {PadStateButton::RIGHT, ButtonGPIO::RIGHT1},
            {PadStateButton::UP, ButtonGPIO::UP1},
            {PadStateButton::DOWN, ButtonGPIO::DOWN1},
            {PadStateButton::A, ButtonGPIO::A1},
            {PadStateButton::B, ButtonGPIO::B1},
            {PadStateButton::C, ButtonGPIO::C1},
            {PadStateButton::D, ButtonGPIO::D1},
            {PadStateButton::E, ButtonGPIO::E1},
            {PadStateButton::F, ButtonGPIO::F1},
            {PadStateButton::COIN, ButtonGPIO::COIN1},
            {PadStateButton::START, ButtonGPIO::START1},
        },
        {
            {PadStateButton::LEFT, ButtonGPIO::LEFT2},
            {PadStateButton::RIGHT, ButtonGPIO::RIGHT2},
            {PadStateButton::UP, ButtonGPIO::UP2},
            {PadStateButton::DOWN, ButtonGPIO::DOWN2},
            {PadStateButton::A, ButtonGPIO::A2},
            {PadStateButton::B, ButtonGPIO::B2},
            {PadStateButton::C, ButtonGPIO::C2},
            {PadStateButton::D, ButtonGPIO::D2},
            {PadStateButton::E, ButtonGPIO::E2},
            {PadStateButton::F, ButtonGPIO::F2},
            {PadStateButton::COIN, ButtonGPIO::COIN2},
            {PadStateButton::START, ButtonGPIO::START2},
        }};
}

class VSyncDetector
{
    int flipDelay_ = 7; // update単位
    uint32_t counter_ = 0;

    int min_ = 0;
    int max_ = 0;

    bool prevLv_ = false;
    int delay_ = 0;

    int interval_ = 0;
    int curInterval_ = 0;

public:
    uint32_t getVSyncCounter() const { return counter_; }
    int getInterval() const { return interval_; }

    int getMin() const { return min_; }
    int getMax() const { return max_; }

    void update(const uint8_t *p, int n)
    {
        int th = (max_ + min_) >> 1;

        max_ -= th >> 8;
        min_ += th >> 8;

        bool pl = prevLv_;
        bool det = false;

        while (n >= 50)
        {
            int acc = 0;
            for (int ct = 5; ct; --ct)
            {
                acc += p[0];
                acc += p[1];
                acc += p[2];
                acc += p[3];
                acc += p[4];
                acc += p[5];
                acc += p[6];
                acc += p[7];
                acc += p[8];
                acc += p[9];
                p += 10;
            }

            max_ = std::max(max_, acc);
            min_ = std::min(min_, acc);

            bool lv = acc > th;

            det |= (pl ^ lv) & lv;
            pl = lv;

            n -= 50;
        }

        ++curInterval_;

        if (det)
        {
            interval_ = curInterval_;
            curInterval_ = 0;
            delay_ = flipDelay_;
        }

        if (curInterval_ == flipDelay_)
        {
            ++counter_;
            PadManager::instance().setVSyncCount(counter_);
        }

        prevLv_ = pl;
    }
};

namespace
{
    static constexpr int ADC_BUFFER_SIZE = 500;
    uint8_t adcBuffer_[2][ADC_BUFFER_SIZE];

    int adcDMACh_ = dma_claim_unused_channel(true);
    int adcDMADBID_ = 0;

    VSyncDetector vsyncDetector_;
}

void initADC()
{
    static constexpr int SYNC_ADC_CH = 2;

    adc_gpio_init(26 + SYNC_ADC_CH);
    adc_init();
    adc_select_input(SYNC_ADC_CH);
    adc_fifo_setup(
        true,  // Write each completed conversion to the sample FIFO
        true,  // Enable DMA data request (DREQ)
        1,     // DREQ (and IRQ) asserted when at least 1 sample present
        false, // We won't see the ERR bit because of 8 bit reads; disable.
        true   // Shift each sample to 8 bits when pushing to FIFO
    );

    adc_set_clkdiv(0);

    sleep_ms(100);

    dma_channel_config cfg = dma_channel_get_default_config(adcDMACh_);

    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, DREQ_ADC);

    dma_channel_configure(adcDMACh_, &cfg,
                          nullptr,         // dst
                          &adc_hw->fifo,   // src
                          ADC_BUFFER_SIZE, // transfer count
                          false            // start immediately
    );
}

void startADCTransfer(int dbid)
{
    dma_hw->ints0 = 1u << adcDMACh_;
    dma_channel_set_write_addr(adcDMACh_, adcBuffer_[dbid], true);
}

void enableADCIRQ()
{
    dma_channel_set_irq0_enabled(adcDMACh_, true);
}

void __isr __not_in_flash_func(irqHandler)()
{
    auto *p = adcBuffer_[adcDMADBID_];

    adcDMADBID_ ^= 1;
    startADCTransfer(adcDMADBID_);

    vsyncDetector_.update(p, ADC_BUFFER_SIZE);
}

void startADC()
{
    enableADCIRQ();

    irq_set_exclusive_handler(DMA_IRQ_0, irqHandler);
    irq_set_enabled(DMA_IRQ_0, true);

    startADCTransfer(0);

    adc_run(true);
}

void initSysTick()
{
    systick_hw->csr = 0x5;
    systick_hw->rvr = 0x00FFFFFF;
}

// tick counterを取得
// カウンタは減っていくのに注意
uint32_t getSysTickCounter24()
{
    return systick_hw->cvr;
}

bool __no_inline_not_in_flash_func(getBootButton)()
{
    const uint CS_PIN_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();

    // Set chip select to Hi-Z
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Note we can't call into any sleep functions in flash right now
    for (volatile int i = 0; i < 1000; ++i)
        ;

    bool button_state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));

    // Restore the state of chip select
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}

////////////////////////
////////////////////////
int main()
{
    stdio_init_all();

    initSysTick();
    initLED();

#ifdef RASPBERRYPI_PICO_W
    if (cyw43_arch_init())
    {
        printf("Wi-Fi init failed");
        return -1;
    }
#endif

    printf("start.\n");
    //    setLED(true);

    tusb_init();

    auto initOutputPin = [](int pin)
    {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, REVERSE_STATE ? 1 : 0);
    };

#if 0
    while (1)
        tuh_task();
#endif

    {
        int i = 0;
        for (auto b = BUTTON_GPIO_MASK; b; b >>= 1, ++i)
        {
            if (b & 1)
            {
                initOutputPin(i);
            }
        }
    }

    initADC();

    startADC();

    //    watchdog_enable(5000, true);

    bool prevBtCnf = false;
    bool prevBtCnfLong = false;
    auto prevCt = getSysTickCounter24();
    uint32_t ct32 = 0;
    uint32_t ctCnfPush = 0;

    while (1)
    {
        //        watchdog_update();

        auto cct = getSysTickCounter24();
        auto cdct = (prevCt - cct) & 0xffffff;
        ct32 += cdct;
        prevCt = cct;

        bool btCnf = getBootButton();
        bool btCnfTrigger = !prevBtCnf && btCnf;
        bool btCnfRelease = prevBtCnf && !btCnf;
        if (btCnfTrigger)
        {
            ctCnfPush = ct32;
        }
        constexpr uint32_t CLOCK = 125000000;
        constexpr uint32_t CT_LONGPUSH = CLOCK * 2; // 2s
        bool btCnfLong = btCnf && (ct32 - ctCnfPush > CT_LONGPUSH);
        bool btCnfLongTrigger = !prevBtCnfLong && btCnfLong;

        PadManager::instance().update(btCnf, btCnfRelease, btCnfLongTrigger);

        prevBtCnf = btCnf;
        prevBtCnfLong = btCnfLong;

        for (int port = 0; port < 2; ++port)
        {
            auto st = PadManager::instance().getButtons(port);
            if (REVERSE_STATE)
            {
                st = ~st;
            }

            auto &map = padPortMap_[port];
            for (auto &m : map)
            {
                gpio_put(static_cast<int>(m.gpio), st & (1u << static_cast<int>(m.padState)));
            }
        }

        tuh_task();

        static int ct = 0;
        if (++ct > 100000)
        {
            ct = 0;
            /*
            printf("ct%d, int %d, r%d,%d\n",
                   vsyncDetector_.getVSyncCounter(),
                   vsyncDetector_.getInterval(),
                   vsyncDetector_.getMin(),
                   vsyncDetector_.getMax());
*/
            static bool f = false;
            f ^= true;
            setLED(f);
        }
    }
    return 0;
}
