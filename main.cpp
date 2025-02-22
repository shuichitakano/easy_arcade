#include <stdio.h>
#include <pico/stdlib.h>
#include <hardware/watchdog.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/adc.h>
#include <hardware/dma.h>
#include <hardware/pwm.h>
#include <hardware/structs/ioqspi.h>
#include <hardware/divider.h>
#include <stdint.h>
#include <tusb.h>
#include <algorithm>
#include "pad_manager.h"
#include "pad_state.h"
#include "led.h"
#include "util.h"
#include "board.h"
#include "lcd.h"
#include "text_screen.h"
#include "menu.h"
#include "rot_encoder.h"
#include "app_config.h"
#include "font.h"
#include "serializer.h"
#include "pca9555.h"
#include "i2c_manager.h"
#include "debug.h"
#include <cmath>

#ifdef RASPBERRYPI_PICO_W
#include "pico/cyw43_arch.h"
#endif

namespace
{
    TextScreen textScreen_;
    Menu menu_{&textScreen_};

    AppConfig appConfig_;

    struct PortMap
    {
        PadStateButton padState;
        ButtonGPIO gpio;
    };
    const PortMap padPortMap_[][12] = {
        {
            {PadStateButton::COIN, ButtonGPIO::COIN1},
            {PadStateButton::START, ButtonGPIO::START1},
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
        },
        {
            {PadStateButton::COIN, ButtonGPIO::COIN2},
            {PadStateButton::START, ButtonGPIO::START2},
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
        }};
    inline static constexpr int N_PAD_PORT_MAP = 12;
    inline static constexpr int N_PAD_PORT_MAP_WITH_MPA = 10;

    const PortMap padPortMap34_[][2] = {
        {
            {PadStateButton::C, ButtonGPIO::E1},
            {PadStateButton::D, ButtonGPIO::F1},
        },
        {
            {PadStateButton::C, ButtonGPIO::E2},
            {PadStateButton::D, ButtonGPIO::F2},
        }};
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

class ButtonWatcher
{
    uint32_t prevCt{};
    uint32_t ct32 = 0;
    uint32_t ctCnfPush = 0;

    bool prevBtCnf = false;
    bool prevBtCnfMiddle = false;
    bool prevBtCnfLong = false;

    bool btCnf = false;
    bool btCnfTrigger = false;
    bool btCnfRelease = false;

    bool btCnfMiddle = false;
    bool btCnfMiddleTrigger = false;

    bool btCnfLong = false;
    bool btCnfLongTrigger = false;

public:
    void init()
    {
        prevCt = util::getSysTickCounter24();
    }

    uint32_t update()
    {
        prevBtCnf = btCnf;
        prevBtCnfMiddle = btCnfMiddle;
        prevBtCnfLong = btCnfLong;

        auto cct = util::getSysTickCounter24();
        auto cdct = (prevCt - cct) & 0xffffff;
        ct32 += cdct;
        prevCt = cct;

        btCnf = getBootButton();
        btCnfTrigger = !prevBtCnf && btCnf;
        btCnfRelease = prevBtCnf && !btCnf;
        if (btCnfTrigger)
        {
            ctCnfPush = ct32;
        }
        constexpr uint32_t CT_MIDDLEPUSH = CPU_CLOCK * 1 /*s*/;
        constexpr uint32_t CT_LONGPUSH = CPU_CLOCK * 3 /*s*/;

        btCnfMiddle = btCnf && (ct32 - ctCnfPush > CT_MIDDLEPUSH);
        btCnfMiddleTrigger = !prevBtCnfMiddle && btCnfMiddle;

        btCnfLong = btCnf && (ct32 - ctCnfPush > CT_LONGPUSH);
        btCnfLongTrigger = !prevBtCnfLong && btCnfLong;

        return cdct;
    }

    void resetLongPush()
    {
        ctCnfPush = ct32;
    }

    uint32_t getPushCounter() const
    {
        return ct32 - ctCnfPush;
    }

    bool isPushed() const
    {
        return btCnf;
    }

    bool isReleaseEdge() const
    {
        return btCnfRelease;
    }

    bool isMiddleEdge() const
    {
        return btCnfMiddleTrigger;
    }

    bool isPrevLongPushed() const
    {
        return prevBtCnfLong;
    }

    bool isLongEdge() const
    {
        return btCnfLongTrigger;
    }
};

class VSyncDetector
{
    // int flipDelay_ = 7; // update単位
    uint32_t counter_ = 0;

    int min_ = 256;
    int max_ = 0;

    bool prevLv_ = false;
    // int delay_ = 0;

    int interval_ = 0;
    int curInterval_ = 0;

    bool enableFPS_ = false;
    uint32_t prevVClock_ = 0;
    uint32_t curFPS100_ = 0;
    uint32_t aveFrameClock = 0;
    uint32_t accumVClock_ = 0;
    int fpsUpdateCounter_ = 0;
    int fpsStableCounter_ = 0;

public:
    uint32_t getCounter() const { return counter_; }

    void reset()
    {
        min_ = 256;
        max_ = 0;
        prevLv_ = false;
        interval_ = 0;
        curInterval_ = 0;

        prevVClock_ = 0;
        curFPS100_ = 0;
        aveFrameClock = 0;
        accumVClock_ = 0;
        fpsUpdateCounter_ = 0;
        fpsStableCounter_ = 0;
    }

    void setEnableFPSCount(bool f) { enableFPS_ = f; }

    int getFPS100() const { return curFPS100_; }
    uint32_t getVSyncCounter() const { return counter_; }
    int getInterval() const { return interval_; }

    int getMin() const { return min_; }
    int getMax() const { return max_; }

    void update(const uint8_t *p, int n)
    {
        uint32_t clk = util::getSysTickCounter24();
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
            // delay_ = flipDelay_;

            // update FPS
            if (enableFPS_)
            {
                if (prevVClock_)
                {
                    uint32_t vclk = (prevVClock_ - clk) & 0xffffff;
                    accumVClock_ += vclk;
                    // 8frame 程度しか24bitカウンタが持たないので
                    // 素直に64frame毎に処理するわけにはいかない

                    if (++fpsUpdateCounter_ == 64)
                    {
                        vclk = accumVClock_ >> 6;

                        if (aveFrameClock == 0 ||
                            vclk < (aveFrameClock >> 1) ||
                            (vclk >> 1) > aveFrameClock)
                        {
                            fpsStableCounter_ = 0;
                            curFPS100_ = 0;
                        }
                        // printf("ave %d, %d\n", aveFrameClock, vclk);
                        if (fpsStableCounter_ > 4)
                        {
                            // aveFrameClock = (aveFrameClock * 1023 + vclk) >> 10;
                            aveFrameClock = (aveFrameClock * 255 + vclk) >> 8;
                            //  2^32 / (125000000/60) = 2062
                            curFPS100_ = 25 * CPU_CLOCK / (aveFrameClock >> 2);
                        }
                        else
                        {
                            aveFrameClock = vclk;
                            ++fpsStableCounter_;
                        }

                        accumVClock_ = 0;
                        fpsUpdateCounter_ = 0;
                    }
                }
                prevVClock_ = clk;
            }
        }

        int flipDelay = interval_ * appConfig_.synchroFetchPhase * 102 >> 10;
        if (curInterval_ == flipDelay)
        {
            ++counter_;
            // PadManager::instance().setVSyncCount(counter_);
        }

        prevLv_ = pl;
    }

    std::array<char, 6> getFPSString() const
    {
        int fps100 = getFPS100();
        std::array<char, 6> buf;
        buf[5] = 0;
        if (fps100 == 0)
        {
            buf = {'-', '-', '.', '-', '-', 0};
            return buf;
        }

        auto d0 = hw_divider_divmod_u32(fps100, 10);
        buf[4] = '0' + to_remainder_u32(d0);

        auto d1 = hw_divider_divmod_u32(to_quotient_u32(d0), 10);
        buf[3] = '0' + to_remainder_u32(d1);

        buf[2] = '.';

        auto d2 = hw_divider_divmod_u32(to_quotient_u32(d1), 10);
        buf[1] = '0' + to_remainder_u32(d2);

        auto d3 = hw_divider_divmod_u32(to_quotient_u32(d2), 10);
        buf[0] = '0' + to_remainder_u32(d3);
        return buf;
    }
};

namespace
{
    static constexpr int ADC_BUFFER_SIZE = 500;
    uint8_t adcBuffer_[2][ADC_BUFFER_SIZE];

    int adcDMACh_ = dma_claim_unused_channel(true);
    int adcDMADBID_ = 0;

    ButtonWatcher buttonWatcher_;
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

    hw_divider_state_t divState;
    hw_divider_save_state(&divState);

    vsyncDetector_.update(p, ADC_BUFFER_SIZE);

    hw_divider_restore_state(&divState);
}

void startADC()
{
    enableADCIRQ();

    irq_set_exclusive_handler(DMA_IRQ_0, irqHandler);
    irq_set_enabled(DMA_IRQ_0, true);

    startADCTransfer(0);

    adc_run(true);
}

void updateMIDIState();

// void setLCDContrast()
// {
//     if (HAS_LCD)
//     {
//         LCD::instance().setContrast(
//             std::clamp(appConfig_.LCDContrast, 0, 15));
//     }
// }

void setRapidPhaseMask()
{
    uint32_t m = 0;
    int b = 1 << static_cast<int>(PadStateButton::A);
    for (int v : appConfig_.rapidPhase)
    {
        if (v)
        {
            m |= b;
        }
        b <<= 1;
    }
    DPRINT(("setRapidPhaseMask %08x\n", m));
    PadManager::instance().setRapidFirePhaseMask(m);
}

void setRapidSettings()
{
    int i = 0;
    for (auto &rs : appConfig_.rapidSettings)
    {
        PadManager::instance().setNonMappedRapidFireMask(i, rs.mask);
        PadManager::instance().setRapidFireDiv(i, rs.div);
        ++i;
    }
}

void setRotEncSettings(int kind)
{
    const auto &re = appConfig_.rotEnc[kind];
    PadManager::instance().setRotEncSetting(kind, re.axis - 1, re.scale * (re.reverse ? -1 : 1));
}

void saveRapidSettings()
{
    int i = 0;
    for (auto &rs : appConfig_.rapidSettings)
    {
        rs.mask = PadManager::instance().getNonMappedRapidFireMask(i);
        rs.div = PadManager::instance().getRapidFireDiv(i);
        ++i;
    }
}

void resetRapidSettings()
{
    appConfig_.resetRapidPhase();

    int i = 0;
    for (auto &rs : appConfig_.rapidSettings)
    {
        rs.mask = 0;
        rs.div = 1;
        PadManager::instance().setNonMappedRapidFireMask(i, rs.mask);
        PadManager::instance().setRapidFireDiv(i, rs.div);
        ++i;
    }
}

void setTwinPortSetting()
{
    PadManager::instance().setTwinPortMode(appConfig_.twinPortMode);
}

void setAnalogMode()
{
    PadManager::instance().setAnalogMode(appConfig_.getAnalogMode());
}

void initButtonGPIO()
{
    int i = 0;
    for (auto b = BUTTON_GPIO_MASK; b; b >>= 1, ++i)
    {
        if (b & 1)
        {
            gpio_init(i);
            gpio_set_dir(i, GPIO_OUT);
            gpio_put(i, REVERSE_STATE ? 1 : 0);
        }
    }
}

namespace
{
    uint16_t dacTable_[1025];
    int16_t dacSensCurve_[AppConfig::ANALOG_MAX][1025];

    int analogTestValue_ = -1;
    int analogTestMode_ = 0;
}

void initDACTable()
{
    for (int i = 0; i < 1025; ++i)
    {
        float v = i * 5.0 / 1024;
        float y = (1.422f * v * v * v - 9.613f * v * v - 33.06f * v + 233.496f) / 256.0f;

        dacTable_[i] = std::clamp(int(y * 1023 + 0.5f), 0, 1023);
    }
}

void initDACSensCurve(int axis)
{
    float sensitivity = appConfig_.analogSettings[axis].sensitivity;
    if (sensitivity == 0)
    {
        for (int i = 0; i < 1025; ++i)
        {
            dacSensCurve_[axis][i] = i * 2 - 1024;
        }
    }
    else
    {
        float d = 1.0f / (1.0f - exp(-sensitivity));

        for (int i = 0; i < 1025; ++i)
        {
            float v = (i - 512) / 512.0f;
            float av = fabsf(v);
            float sign = v < 0 ? -1 : 1;
            float y = sign * (1.0f - expf(-sensitivity * av)) * d;
            dacSensCurve_[axis][i] = std::clamp(int(y * 1024), -1024, 1024);
        }
    }
}

int applyDACSensCurve(int axis, int v, int ofs, int scale_10, bool hasCenter)
{
    v = std::clamp(v + ofs, 0, 1024);

    int r = 0;
    if (hasCenter)
    {
        r = dacSensCurve_[axis][v] * scale_10 / 20 + 512;
    }
    else
    {
        r = dacSensCurve_[axis][(v >> 1) + 512] * scale_10 / 10;
    }
    return std::clamp(r, 0, 1024);
}

void initDACSensCurves()
{
    for (int i = 0; i < AppConfig::ANALOG_MAX; ++i)
    {
        initDACSensCurve(i);
    }
}

void initDACPWM()
{
    // E,F 出力を PWM による DAC 出力にするための基本設定
    // E2,F2 は E1, F1 に隣接しているものとする
    //    for (auto gpio : {ButtonGPIO::D1, ButtonGPIO::E1, ButtonGPIO::F1})
    for (auto gpio : {ButtonGPIO::E1, ButtonGPIO::F1})
    {
        auto pin = static_cast<int>(gpio);
        gpio_set_function(pin + 0, GPIO_FUNC_PWM);
        gpio_set_function(pin + 1, GPIO_FUNC_PWM);

        auto config = pwm_get_default_config();
        pwm_config_set_clkdiv(&config, 1.f);
        // 125M/256=488.28125kHz
        // 125M/1024= 122.070kHz
        pwm_config_set_wrap(&config, 1023);
        pwm_init(pwm_gpio_to_slice_num(pin), &config, true);

        pwm_set_gpio_level(pin + 0, 512);
        pwm_set_gpio_level(pin + 1, 512);
    }
}

void setDACValue(ButtonGPIO gpio, int v)
{
    auto pin = static_cast<int>(gpio);
    auto vv = dacTable_[v];
#ifndef NDEBUG
    if (analogTestValue_ >= 0)
    {
        if (analogTestMode_ == 0)
        {
            vv = dacTable_[analogTestValue_ * 4];
        }
        else
        {
            vv = std::clamp(analogTestValue_ * 4, 0, 1023);
        }
    }
#endif
    pwm_set_gpio_level(pin, vv);
    // printf("(%d:%d:%d)\n", pin, v, vv);
}

void setAnalogValue(const PadState::AnalogState &st, int port)
{
    auto getValue = [&](int i)
    {
        auto v = st.values[i];
        auto hasCenter = st.hasCenter[i];
        return applyDACSensCurve(i, v,
                                 appConfig_.analogSettings[i].offset * 4,
                                 appConfig_.analogSettings[i].scale,
                                 hasCenter);
    };

    switch (appConfig_.getAnalogMode())
    {
    default:
    case AppConfig::AnalogMode::NONE:
        break;

    case AppConfig::AnalogMode::_1P2P_EACH_2CH:
        if (port == 0)
        {
            setDACValue(ButtonGPIO::E1, getValue(0));
            setDACValue(ButtonGPIO::F1, getValue(1));
        }
        else
        {
            setDACValue(ButtonGPIO::E2, getValue(0));
            setDACValue(ButtonGPIO::F2, getValue(1));
        }
        break;

    case AppConfig::AnalogMode::_1P_ONLY_4CH:
        if (port == 0)
        {
            setDACValue(ButtonGPIO::E1, getValue(0));
            setDACValue(ButtonGPIO::E2, getValue(1));
            setDACValue(ButtonGPIO::F1, getValue(2));
            setDACValue(ButtonGPIO::F2, getValue(3));
        }
        break;
    }
}

void setupGPIO()
{
    if (appConfig_.getAnalogMode() != AppConfig::AnalogMode::NONE)
    {
        initDACPWM();
        initDACTable();
    }
    else
    {
        initButtonGPIO();
    }
}

void applySettings()
{
    setRapidPhaseMask();
    setRapidSettings();
    setRotEncSettings(0);
    setRotEncSettings(1);
    setTwinPortSetting();
    setAnalogMode();
    setupGPIO();
    initDACSensCurves();
}

void resetConfigs()
{
    appConfig_ = AppConfig();
    PadManager::instance().resetConfig();

    applySettings();
}

void load()
{
    Deserializer s;
    if (!s)
    {
        DPRINT(("no saved data\n"));
        return;
    }

    if (!appConfig_.deserialize(s))
    {
        DPRINT(("AppConfig load failed\n"));
        return;
    }

    PadManager::instance().deserialize(s);

    //
    applySettings();

    DPRINT(("Loaded.\n"));
}

void save()
{
    Serializer s(65536, 512);

    appConfig_.serialize(s);
    PadManager::instance().serialize(s);

    s.flash();
    DPRINT(("Saved.\n"));
}

char getButtonName(PadStateButton b)
{
    switch (b)
    {
    case PadStateButton::LEFT:
        return '\10';
    case PadStateButton::RIGHT:
        return '\7';
    case PadStateButton::UP:
        return '\5';
    case PadStateButton::DOWN:
        return '\6';
    case PadStateButton::A:
        return 'A';
    case PadStateButton::B:
        return 'B';
    case PadStateButton::C:
        return 'C';
    case PadStateButton::D:
        return 'D';
    case PadStateButton::E:
        return 'E';
    case PadStateButton::F:
        return 'F';
    case PadStateButton::COIN:
        return 'c';
    case PadStateButton::START:
        return 'S';
    case PadStateButton::CMD:
        return 'x';
    default:
        return '?';
    }
}

#define MAKE_BUTTON_STRING_CASE(x, str) \
    case PadStateButton::x:             \
        return str;

const char *getButtonConfigTextNormal(PadStateButton b)
{
    switch (b)
    {
        // clang-format off
        MAKE_BUTTON_STRING_CASE(LEFT,  "Push \10  ");
        MAKE_BUTTON_STRING_CASE(RIGHT, "Push \7  ");
        MAKE_BUTTON_STRING_CASE(UP,    "Push \5  ");
        MAKE_BUTTON_STRING_CASE(DOWN,  "Push \6  ");
        MAKE_BUTTON_STRING_CASE(A,     "Push A  ");
        MAKE_BUTTON_STRING_CASE(B,     "Push B  ");
        MAKE_BUTTON_STRING_CASE(C,     "Push C  ");
        MAKE_BUTTON_STRING_CASE(D,     "Push D  ");
        MAKE_BUTTON_STRING_CASE(E,     "Push E  ");
        MAKE_BUTTON_STRING_CASE(F,     "Push F  ");
        MAKE_BUTTON_STRING_CASE(COIN,  "PushCOIN");
        MAKE_BUTTON_STRING_CASE(START, "PshSTART");
        MAKE_BUTTON_STRING_CASE(CMD,   "Push CMD");
        // clang-format on
    default:
        return "";
    }
}

const char *getButtonConfigText2PortMode(PadStateButton b)
{
    switch (b)
    {
        // clang-format off
        MAKE_BUTTON_STRING_CASE(LEFT,  "1P \10    ");
        MAKE_BUTTON_STRING_CASE(RIGHT, "1P \7    ");
        MAKE_BUTTON_STRING_CASE(UP,    "1P \5    ");
        MAKE_BUTTON_STRING_CASE(DOWN,  "1P \6    ");
        MAKE_BUTTON_STRING_CASE(A,     "1P A    ");
        MAKE_BUTTON_STRING_CASE(B,     "1P B    ");
        MAKE_BUTTON_STRING_CASE(C,     "1P C    ");
        MAKE_BUTTON_STRING_CASE(D,     "1P D    ");
        MAKE_BUTTON_STRING_CASE(E,     "1P E    ");
        MAKE_BUTTON_STRING_CASE(F,     "1P F    ");
        MAKE_BUTTON_STRING_CASE(COIN,  "1P COIN ");
        MAKE_BUTTON_STRING_CASE(START, "1P START");
        MAKE_BUTTON_STRING_CASE(CMD,   "Push CMD");
        MAKE_BUTTON_STRING_CASE(LEFT_2P,  "2P \10    ");
        MAKE_BUTTON_STRING_CASE(RIGHT_2P, "2P \7    ");
        MAKE_BUTTON_STRING_CASE(UP_2P,    "2P \5    ");
        MAKE_BUTTON_STRING_CASE(DOWN_2P,  "2P \6    ");
        MAKE_BUTTON_STRING_CASE(A_2P,     "2P A    ");
        MAKE_BUTTON_STRING_CASE(B_2P,     "2P B    ");
        MAKE_BUTTON_STRING_CASE(C_2P,     "2P C    ");
        MAKE_BUTTON_STRING_CASE(D_2P,     "2P D    ");
        MAKE_BUTTON_STRING_CASE(E_2P,     "2P E    ");
        MAKE_BUTTON_STRING_CASE(F_2P,     "2P F    ");
        MAKE_BUTTON_STRING_CASE(COIN_2P,  "2P COIN ");
        MAKE_BUTTON_STRING_CASE(START_2P, "2P START");
        // clang-format on
    default:
        return "";
    }
}

const char *getAnalogConfigText(PadConfigAnalog b)
{
#define MAKE_CNFANALOG_STRING_CASE(x, str) \
    case PadConfigAnalog::x:               \
        return str;
    switch (b)
    {
        MAKE_CNFANALOG_STRING_CASE(H0, "EnterA1\5");
        MAKE_CNFANALOG_STRING_CASE(L0, "EnterA1\6");
        MAKE_CNFANALOG_STRING_CASE(H1, "EnterA2\5");
        MAKE_CNFANALOG_STRING_CASE(L1, "EnterA2\6");
        MAKE_CNFANALOG_STRING_CASE(H2, "EnterA3\5");
        MAKE_CNFANALOG_STRING_CASE(L2, "EnterA3\6");
        MAKE_CNFANALOG_STRING_CASE(H3, "EnterA4\5");
        MAKE_CNFANALOG_STRING_CASE(L3, "EnterA4\6");
    default:
        return "";
    }
}

const char *getButtonConfigText(PadStateButton b)
{
    return appConfig_.twinPortMode ? getButtonConfigText2PortMode(b) : getButtonConfigTextNormal(b);
}

void initMenu()
{
    static const char *buttonDispModeText[] = {"Input", "Rapid", "None"};
    static const char *onOffText[] = {"Off", "On"};
    static const char *inOutText[] = {"In", "Out"};
    static const char *reverseText[] = {"Normal", "Reverse"};
    static const char *initPowerModeText[] = {"InitOff", "InitOn"};
    static const char *rapidModeText[] = {"Softw", "Synchro"};
    static const char *analogModeText[] = {"Disable", "2Axis2P", "4Axis1P"};

    menu_.setBlinkInterval(CPU_CLOCK / 2);

    PadManager::instance().setOnExitConfigFunc(
        []
        {
            DPRINT(("exit button config\n"));
            textScreen_.clearAll();
            menu_.forceClose();
            menu_.refresh();
        });

    menu_.append(
        "BtnCfg", "LngPress", [](Menu &m)
        { 
            textScreen_.clearAll();
            textScreen_.printMain(0, 0, "BtConfig");
            PadManager::instance().enterConfigMode(); },
        true /* config button */);
    menu_.append(
             "AnlgCfg", "LngPress", [](Menu &m)
             { 
            textScreen_.clearAll();
            textScreen_.printMain(0, 0, "AnalgCfg");
            PadManager::instance().enterAnalogConfigMode(); },
             true /* config button */)
        .setConditionFunc(
            []()
            {
                return appConfig_.getAnalogMode() != AppConfig::AnalogMode::NONE;
            });
    menu_.append("AnalgMd", &appConfig_.analogMode, analogModeText, std::size(analogModeText),
                 [&](Menu &m)
                 {
                     setAnalogMode();
                     setupGPIO();
                 });
#ifndef NDEBUG
    menu_.append("AnlgTst", &analogTestValue_, {-1, 256},
                 [](char *buf, size_t bufSize, int v)
                 {
                     if (analogTestValue_ >= 0)
                     {
                         snprintf(buf, bufSize, "%3d", analogTestValue_);
                     }
                     else
                     {
                         snprintf(buf, bufSize, "Disable");
                     }
                 })
        .setConditionFunc(
            []()
            { return appConfig_.getAnalogMode() != AppConfig::AnalogMode::NONE; });

    static const char *analogTestModeText[] = {"Convert", "Direct"};
    menu_.append("AnlgTst", &analogTestMode_, analogTestModeText, std::size(analogTestModeText));
#endif
    for (int i = 0; i < AppConfig::ANALOG_MAX; ++i)
    {
        auto condFunc = [=]()
        {
            return i < appConfig_.getAnalogModeChannels();
        };

        menu_.append("AnlgSns", &appConfig_.analogSettings[i].sensitivity, {-16, 16}, [=](char *buf, size_t bufSize, int v)
                     { snprintf(buf, bufSize, "[%d]:%d", i + 1, v); }, [=](Menu &m)
                     { initDACSensCurve(i); })
            .setConditionFunc(condFunc);
        menu_.append("AnlgOfs", &appConfig_.analogSettings[i].offset, {-99, 99}, [=](char *buf, size_t bufSize, int v)
                     { snprintf(buf, bufSize, "[%d]:%d", i + 1, v); })
            .setConditionFunc(condFunc);
        menu_.append("AnlgMag", &appConfig_.analogSettings[i].scale, {1, 99}, [=](char *buf, size_t bufSize, int v)
                     { snprintf(buf, bufSize, "[%d]:%d.%d", i + 1, v / 10, v % 10); })
            .setConditionFunc(condFunc);
    }

    menu_.append("PowMode", &appConfig_.initPowerOn,
                 initPowerModeText, std::size(initPowerModeText));
    menu_.append("DispFPS", &appConfig_.dispFPS, onOffText, 2);
    menu_.append("BtnDisp", &appConfig_.buttonDispMode,
                 buttonDispModeText, std::size(buttonDispModeText));
    menu_.append("BackLit", &appConfig_.backLight, onOffText, 2);
    // menu_.append("LCD Ctr", &appConfig_.LCDContrast, {0, 15}, [](char *buf, size_t bufSize, int v)
    //              { snprintf(buf, bufSize, "%2d", v); }, [](Menu &)
    //              { setLCDContrast(); });
    menu_.append("RapidMd", &appConfig_.rapidModeSynchro, rapidModeText, std::size(rapidModeText));
    menu_.append("SwRapid", &appConfig_.softwareRapidSpeed, {1, 30},
                 // "%2dShot\3"
                 [](char *buf, size_t bufSize, int v)
                 { snprintf(buf, bufSize, "%2dShot\3", v); })
        .setConditionFunc(
            []()
            { return !appConfig_.rapidModeSynchro; });
    menu_.append("SyncUpT", &appConfig_.synchroFetchPhase, {0, 9},
                 [](char *buf, size_t bufSize, int v)
                 { snprintf(buf, bufSize, "%2d%%", v * 10); })
        .setConditionFunc([]()
                          { return appConfig_.rapidModeSynchro; });

    auto onRapidPhaseChanged = [](Menu &m)
    {
        setRapidPhaseMask();
    };

    menu_.append("Phase A", &appConfig_.rapidPhase[0], inOutText, 2, onRapidPhaseChanged);
    menu_.append("Phase B", &appConfig_.rapidPhase[1], inOutText, 2, onRapidPhaseChanged);
    menu_.append("Phase C", &appConfig_.rapidPhase[2], inOutText, 2, onRapidPhaseChanged);
    menu_.append("Phase D", &appConfig_.rapidPhase[3], inOutText, 2, onRapidPhaseChanged);
    menu_.append("Phase E", &appConfig_.rapidPhase[4], inOutText, 2, onRapidPhaseChanged);
    menu_.append("Phase F", &appConfig_.rapidPhase[5], inOutText, 2, onRapidPhaseChanged);

    menu_.append(
        "InitRpd", "Press A", [](Menu &m)
        {
            DPRINT(("reset rapid state\n"));
            textScreen_.printInfo(0, 1, " Init'd ");
            textScreen_.setInfoLayerClearTimer(CPU_CLOCK);
            resetRapidSettings(); });

    menu_.append(
        "SaveRpd", "Press A", [](Menu &m)
        {
            DPRINT(("save rapid state\n"));
            textScreen_.printInfo(0, 1, "  Saved ");
            textScreen_.setInfoLayerClearTimer(CPU_CLOCK);
            saveRapidSettings(); });

    static const char *rotEncAxisText[] = {"None", "Axis X", "Axis Y", "Axis Z", "Axis RX", "Axis RY", "Axis RZ", "SLIDER", "DIAL", "WHEEL"};
    menu_.append("RotEncX", &appConfig_.rotEnc[0].axis, rotEncAxisText, std::size(rotEncAxisText), [&](Menu &m)
                 { setRotEncSettings(0); });
    menu_.append("RotEncY", &appConfig_.rotEnc[1].axis, rotEncAxisText, std::size(rotEncAxisText), [&](Menu &m)
                 { setRotEncSettings(1); });

    auto rotEncXCond = []()
    {
        return appConfig_.rotEnc[0].axis != 0;
    };
    auto rotEncYCond = []()
    {
        return appConfig_.rotEnc[1].axis != 0;
    };
    menu_.append(
             "REncX S", &appConfig_.rotEnc[0].scale, {1, 256}, [](char *buf, size_t bufSize, int v)
             { snprintf(buf, bufSize, "%3d", v); },
             [&](Menu &m)
             { setRotEncSettings(0); })
        .setConditionFunc(rotEncXCond);
    menu_.append(
             "REncY S", &appConfig_.rotEnc[1].scale, {1, 256}, [](char *buf, size_t bufSize, int v)
             { snprintf(buf, bufSize, "%3d", v); },
             [&](Menu &m)
             { setRotEncSettings(1); })
        .setConditionFunc(rotEncYCond);
    menu_.append(
             "RotEncX", &appConfig_.rotEnc[0].reverse, reverseText, std::size(reverseText), [&](Menu &m)
             { setRotEncSettings(0); })
        .setConditionFunc(rotEncXCond);
    menu_.append(
             "RotEncY", &appConfig_.rotEnc[1].reverse, reverseText, std::size(reverseText), [&](Menu &m)
             { setRotEncSettings(1); })
        .setConditionFunc(rotEncYCond);

    menu_.append("2PortMd", &appConfig_.twinPortMode, onOffText, std::size(onOffText),
                 [&](Menu &m)
                 { setTwinPortSetting(); });

    menu_.append("InitAll", "PressA+S", [](Menu &m)
                 {
        auto pad = m.getPad();
        if (pad.first & (1 << static_cast<int>(PadStateButton::START)))
        {
            DPRINT(("init all\n"));
            textScreen_.printInfo(0, 1, " Init'd ");
            textScreen_.setInfoLayerClearTimer(CPU_CLOCK);
            resetConfigs();
        } });

    menu_.setOpenCloseFunc(
        [](bool open)
        {
            textScreen_.clearLayer(TextScreen::Layer::BASE);
            if (open)
            {
                textScreen_.setFont(3, get_sFont());
                textScreen_.setFont(5, getUpArrowFont());
            }
            else
            {
                textScreen_.setFont(0, getPlayerFont(0));
                textScreen_.setFont(1, getPlayerFont(1));
                textScreen_.setFont(2, get1_NFont(1));
                textScreen_.setFont(3, get1_NFont(1));
                textScreen_.setFont(5, getUpArrowFont());
                textScreen_.print(0, 0, TextScreen::Layer::BASE, "\240\2");
                textScreen_.print(0, 1, TextScreen::Layer::BASE, "\1\3");

                if (menu_.isChanged())
                {
                    save();
                    menu_.clearChanged();
                }
            }
        });

    auto &padManager = PadManager::instance();
    padManager.setEnableModelChangeByButton(false);
    padManager.setEnableLED(false);

    padManager.setPrintButtonFunc([](PadStateButton b)
                                  { textScreen_.printMain(0, 1,
                                                          getButtonConfigText(b)); });

    padManager.setPrintCnfAnalogFunc([](PadConfigAnalog b)
                                     { textScreen_.printMain(0, 1,
                                                             getAnalogConfigText(b)); });
}

////
class MultiPlayerAdapter
{
    inline static constexpr uint8_t MPEXT_SUBADDR = 7;

public:
    explicit operator bool() const { return !!pca95555MPExt_; }

    void init()
    {
        if (pca95555MPExt_.init(MPEXT_SUBADDR))
        {
            pca95555MPExt_.setPortDir(0xffff);
            pca95555MPExt_.output(0);
            dirty_ = true;
        }
    }

    void output(uint32_t st3, uint32_t st4)
    {
        if (!pca95555MPExt_)
        {
            return;
        }

        // Port3は逆順になっている
        uint16_t st = ~((((st3 >> 8) & 0b1) |
                         ((st3 >> 6) & 0b10) |
                         ((st3 >> 4) & 0b100) |
                         ((st3 >> 2) & 0b1000) |
                         ((st3 >> 0) & 0b10000) |
                         ((st3 << 2) & 0b100000) |
                         ((st3 << 4) & 0b1000000) |
                         ((st3 << 6) & 0b10000000)) |
                        ((st4 >> 1) << 8));

        if (st != prevSt_ || dirty_)
        {
            // printf("dir = %04x\n", st);
            pca95555MPExt_.setPortDirNonBlocking(st);
            prevSt_ = st;
            dirty_ = false;
        }

        auto outButtonCD = [](int i, uint32_t st)
        {
            auto &map = padPortMap34_[i];
            for (auto &m : map)
            {
                gpio_put(static_cast<int>(m.gpio),
                         st & (1u << static_cast<int>(m.padState)));
            }
        };
        outButtonCD(0, st3);
        outButtonCD(1, st4);
    }

private:
    device::PCA9555 pca95555MPExt_;

    uint16_t prevSt_ = 0;
    bool dirty_ = true;
};

namespace
{
    auto *i2cIF_ = i2c1;

    MultiPlayerAdapter multiPlayerAdapter_;
}

void updateDisplay(uint32_t dclk)
{
    if (menu_.isOpened())
    {
        return;
    }

    static uint32_t clkct = 0;
    clkct += dclk;
    constexpr uint32_t interval = CPU_CLOCK / 4;
    static bool blink = false;
    if (clkct > interval)
    {
        clkct -= interval;
        blink ^= true;
    }

    // 2P以降で最後に更新されたポートを探す
    static uint32_t prevButtons[PadManager::N_OUTPUT_PORTS]{};
    static int line2Port = 1;
    auto prevLine2Port = line2Port;
    if (multiPlayerAdapter_)
    {
        for (int i = PadManager::N_OUTPUT_PORTS - 1; i >= 1; --i)
        {
            auto buttons = PadManager::instance().getNonRapidButtons(i);
            if (buttons != prevButtons[i])
            {
                line2Port = i;
            }
            prevButtons[i] = buttons;
        }
    }
    else
    {
        line2Port = 1;
    }

    if (line2Port != prevLine2Port)
    {
        textScreen_.setFont(1, getPlayerFont(line2Port));
    }

    auto buttonDispMode = appConfig_.buttonDispMode;

    auto &padManager = PadManager::instance();
    for (int line = 0; line < 2; ++line)
    {
        int port = line == 0 ? 0 : line2Port;

        int rapidDiv = padManager.getRapidFireDiv(port);
        textScreen_.setFont(2 + line, get1_NFont(rapidDiv));

        switch (appConfig_.getButtonDispMode())
        {
        case AppConfig::ButtonDispMode::INPUT_BUTTONS:
        {
            auto rbt = padManager.getNonRapidButtonsEachRapidPhase(port);

            char buf[7];
            int ofs = 0;
            for (int i = 0; i < static_cast<int>(PadStateButton::MAX) && ofs < 6; ++i)
            {
                bool f0 = (rbt[0] & (1u << i));
                bool f1 = (rbt[1] & (1u << i));
                if (f0 || f1)
                {
                    bool f = blink ? f0 : f1;
                    buf[ofs++] = f ? getButtonName(static_cast<PadStateButton>(i)) : ' ';
                }
            }
            buf[ofs] = 0;
            textScreen_.printMain(2, line, buf);
            textScreen_.fill(2 + ofs, line, TextScreen::Layer::MAIN, 6 - ofs, 0);
        }
        break;

        case AppConfig::ButtonDispMode::RAPID_BUTTONS:
        {
            auto mask = padManager.getRapidFireMask(port);

            char buf[7];
            int ofs = 0;
            for (int i = 0; i < static_cast<int>(PadStateButton::MAX) && ofs < 6; ++i)
            {
                if (mask & (1u << i))
                {
                    buf[ofs++] = getButtonName(static_cast<PadStateButton>(i));
                }
            }
            buf[ofs] = 0;
            textScreen_.printMain(2, line, buf);
            textScreen_.fill(2 + ofs, line, TextScreen::Layer::MAIN, 6 - ofs, 0);
        }
        break;

        default:
            break;
        }
    }

    if (appConfig_.dispFPS)
    {
        auto fpsStr = vsyncDetector_.getFPSString();
#if 0
        int d0 = fpsStr[1] - '0';
        if (d0 >= 0 && d0 <= 9)
        {
            fpsStr[1] = '\4';
            textScreen_.setFont(4, getNumberWithDotFont(d0));
        }
#endif
        textScreen_.print(3, 1, TextScreen::Layer::BASE, fpsStr.data());
    }
}

class SwRapidFire
{
    uint32_t counter_ = 0;

    int clk_ = 0;

public:
    uint32_t getCounter() const { return counter_; }

    void update(int dclk, int ctPerSec)
    {
        clk_ += dclk;
        int interval = CPU_CLOCK / (ctPerSec << 1);
        if (clk_ > interval)
        {
            clk_ -= interval;
            ++counter_;
        }
    }
};

void initDevices()
{
    // Multiplayer ext
    multiPlayerAdapter_.init();

    //    LCD::instance().setDisplayOnOff(false);
    //    LCD::instance().setDisplayOnOff(true);
}

void updateJAMMAOutput(uint32_t st, int port, bool hasMultiPlayerAdapter)
{
    if (REVERSE_STATE)
    {
        st = ~st;
    }

    // MultiPlayerAdapter がある場合は、E,FボタンをPort3, Port4に使用する
    auto n = hasMultiPlayerAdapter ? N_PAD_PORT_MAP_WITH_MPA : N_PAD_PORT_MAP;

    auto &map = padPortMap_[port];
    for (auto i = 0; i < n; ++i)
    {
        auto &m = map[i];
        gpio_put(static_cast<int>(m.gpio),
                 st & (1u << static_cast<int>(m.padState)));
    }

    auto &ast = PadManager::instance().getAnalogState(port);
    setAnalogValue(ast, port);
}

void updateOutput()
{
    bool hasMPAdapter = !!multiPlayerAdapter_;

    auto &padManager = PadManager::instance();
    for (int port = 0; port < 2; ++port)
    {
        auto st = padManager.getButtons(port);
#if !defined(NDEBUG) && 0
        if (port == 0)
        {
            for (int i = 0; i < (int)PadStateButton::MAX; ++i)
            {
                printf("%c", st & (1u << i) ? '1' : '0');
            }
            printf(("\n"));
        }
#endif
        updateJAMMAOutput(st, port, hasMPAdapter);
    }
    if (multiPlayerAdapter_)
    {
        auto st3 = padManager.getButtons(2);
        auto st4 = padManager.getButtons(3);

        multiPlayerAdapter_.output(st3, st4);
    }
}

void setUSBIniitalized(bool f); // hid_app.cpp

bool powerOn()
{
    // check Power good
    gpio_init(PDPG_PIN);
    gpio_set_dir(PDPG_PIN, GPIO_IN);
    gpio_set_pulls(PDPG_PIN, true, false);
    sleep_ms(100);

    bool powerGood = !gpio_get(PDPG_PIN);

    gpio_set_pulls(PDPG_PIN, false, false);
    gpio_set_function(PDPG_PIN, GPIO_FUNC_UART);
    sleep_ms(100);

    if (powerGood)
    {
        gpio_put(POWER_EN_PIN, true);
        printf("power on\n");

        applySettings();

        // initDevice 時に5V電源が必要な可能性があるので、安定するまで待つ
        sleep_ms(100);
        initDevices();

        PadManager::instance().reset();
        PadManager::instance().enterNormalMode();
        vsyncDetector_.reset();
        vsyncDetector_.setEnableFPSCount(true);

        // textScreen_.printInfo(0, 0, "Power On");
        // textScreen_.setInfoLayerClearTimer(CPU_CLOCK);

        if (HAS_LCD)
        {
            menu_.refresh();
        }

        tusb_init();
        setUSBIniitalized(true);
    }
    else
    {
        printf("power NOT good\n");
        if (HAS_LCD)
        {
            textScreen_.printInfo(0, 0, "POWER NG");
            textScreen_.printInfo(0, 1, "Check PD");
            textScreen_.setInfoLayerClearTimer(CPU_CLOCK * 2);
        }
        return false;
    }

    return true;
}

void powerOff()
{
    setUSBIniitalized(false);
    tuh_deinit(0);

    initButtonGPIO();

    if (HAS_POWER_BUTTON)
    {
        gpio_put(POWER_EN_PIN, false);
    }
    vsyncDetector_.setEnableFPSCount(false);

    printf("power off\n");
    if (HAS_LCD)
    {
        menu_.forceClose();

        textScreen_.clearAll();
        textScreen_.printInfo(0, 0, "PowerOff");
        textScreen_.setInfoLayerClearTimer(CPU_CLOCK);
    }
}

void i2cTest()
{
    // check Power good
    gpio_init(PDPG_PIN);
    gpio_set_dir(PDPG_PIN, GPIO_IN);
    gpio_set_pulls(PDPG_PIN, true, false);
    sleep_ms(100);

    bool powerGood = !gpio_get(PDPG_PIN);

    gpio_set_pulls(PDPG_PIN, false, false);
    gpio_set_function(PDPG_PIN, GPIO_FUNC_UART);
    sleep_ms(100);

    assert(powerGood);
    if (!powerGood)
    {
        exit(-1);
    }

    gpio_put(POWER_EN_PIN, true);
    printf("power on\n");

    sleep_ms(200);

    while (1)
    {
        initDevices();
    }
}

////////////////////////
////////////////////////
int main()
{
    stdio_init_all();

#ifdef RASPBERRYPI_PICO_W
    if (cyw43_arch_init())
    {
        printf("Wi-Fi init failed");
        return -1;
    }
#endif

    util::initSysTick();

    initLED();
    setLED(false);

    if (HAS_POWER_BUTTON)
    {
        gpio_init(POWER_EN_PIN);
        gpio_set_dir(POWER_EN_PIN, GPIO_OUT);
        gpio_put(POWER_EN_PIN, 0);
    }

    load();

    DPRINT(("start.\n"));

    initButtonGPIO();

    constexpr bool enableI2C = HAS_LCD;

    // I2C
    if (enableI2C)
    {
        i2c_init(i2cIF_, 400000);
        // i2c_init(i2cIF_, 4000);
        gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
        gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
        getI2CManager().init(i2cIF_);

        // LCD
        auto &lcd = LCD::instance();
        DPRINT(("I2C init...\n"));
        lcd.init();
        DPRINT(("I2C init done.\n"));

        // i2cTest();

        textScreen_.clearAll();
        textScreen_.printInfo(0, 0, BOARD_NAME);
        textScreen_.printInfo(0, 1, BOARD_VERSION);
        textScreen_.setInfoLayerClearTimer(CPU_CLOCK);

        initMenu();

        textScreen_.clearLayer(TextScreen::Layer::BASE);
    }

    // ADC
    initADC();
    startADC();

    // DAC
    initDACTable();

#ifdef NDEBUG
    watchdog_enable(5000, true);
#endif
    auto &padManager = PadManager::instance();
    bool power = !HAS_POWER_BUTTON;
    padManager.setNormalModeLED(HAS_POWER_BUTTON);
    padManager.setOnSaveFunc(save);

    SwRapidFire swRapidFire;

    if (appConfig_.initPowerOn && !power)
    {
        sleep_ms(500); // PDが確実に安定するくらいまでまつ
        power = powerOn();
    }

    buttonWatcher_.init();

    while (1)
    {
        watchdog_update();

        auto cdct = buttonWatcher_.update();

        if (!power && HAS_POWER_BUTTON)
        {
            constexpr uint32_t CT_ALLINIT = CPU_CLOCK * 20 /*s*/;

            if (buttonWatcher_.isReleaseEdge() &&
                !buttonWatcher_.isPrevLongPushed())
            {
                power = powerOn();
            }
            else if (buttonWatcher_.isPushed() &&
                     buttonWatcher_.getPushCounter() > CT_ALLINIT)
            {
                DPRINT(("init all\n"));
                textScreen_.printInfo(0, 0, "ALL");
                textScreen_.printInfo(0, 1, " Init'd");
                textScreen_.setInfoLayerClearTimer(CPU_CLOCK);
                resetConfigs();
            }
            // sleep_ms(100); // 省電力?
        }
        else
        {
            auto nrb0 = padManager.getNonRapidButtons(0);
            auto nrb1 = padManager.getNonRapidButtons(1);
            auto nrb = nrb0 | nrb1;

            constexpr auto MASK_POWER_OFF = (1u << static_cast<int>(PadStateButton::START)) |
                                            (1u << static_cast<int>(PadStateButton::COIN)) |
                                            (1u << static_cast<int>(PadStateButton::CMD));
            auto btPowOff = (nrb & MASK_POWER_OFF) == MASK_POWER_OFF;

            if ((buttonWatcher_.isLongEdge() || btPowOff) && HAS_POWER_BUTTON)
            {
                powerOff();
                power = false;
            }
            else
            {
                if (HAS_LCD && padManager.isNormalMode())
                {
                    menu_.update(cdct,
                                 nrb,
                                 buttonWatcher_.isPushed(),
                                 buttonWatcher_.isReleaseEdge(),
                                 buttonWatcher_.isMiddleEdge());
                }

                if (appConfig_.rapidModeSynchro)
                {
                    padManager.setVSyncCount(vsyncDetector_.getCounter());
                }
                else
                {
                    swRapidFire.update(cdct, appConfig_.softwareRapidSpeed);
                    padManager.setVSyncCount(swRapidFire.getCounter());
                }

                padManager.update(cdct,
                                  buttonWatcher_.isPushed(),
                                  buttonWatcher_.isReleaseEdge(),
                                  buttonWatcher_.isMiddleEdge());

                updateOutput();

                if (HAS_LCD && padManager.isNormalMode())
                {
                    updateDisplay(cdct);
                    menu_.render();
                }
            }
        }

        if (HAS_LCD)
        {
            textScreen_.update(cdct);

            bool info = textScreen_.isInfoActive();
            bool led = (power || info) && appConfig_.backLight;
            setLED(led);

            LCD::instance().setDisplayOnOff(info || power);
        }

        tuh_task();
        updateMIDIState();
    }
    return 0;
}
