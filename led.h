/*
 * author : Shuichi TAKANO
 * since  : Sun Jan 14 2024 05:37:16
 */

#include <hardware/gpio.h>
#include "board.h"

#ifdef RASPBERRYPI_PICO_W
#include "pico/cyw43_arch.h"
#endif

#ifdef RASPBERRYPI_PICO_W
inline void initLED()
{
}

inline void setLED(bool f)
{
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, f);
}
#else
inline void initLED()
{
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
}

inline void setLED(bool f)
{
    gpio_put(PICO_DEFAULT_LED_PIN, f ^ LED_ACTIVE_LOW);
}
#endif