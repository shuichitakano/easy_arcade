# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

#set(PICO_DEOPTIMIZED_DEBUG on)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
#set(PICO_SDK_PATH "/Users/shuichi/pico/pico-sdk")

#set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(arcade_play C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(arcade_play 
        main.cpp
        pad_state.cpp
        pad_translator.cpp
        pad_manager.cpp
        hid_app.cpp
        hid_info.cpp
        serializer.cpp
        third_party/tusb_xinput/xinput_host.c
        usb_app_driver.cpp
        usb_midi.cpp
        lcd.cpp
        text_screen.cpp
        font.cpp
        menu.cpp
        rot_encoder.cpp
        app_config.cpp
        pca9555.cpp
        i2c_manager.cpp
        )

pico_set_program_name(arcade_play "arcade_play")
pico_set_program_version(arcade_play "0.1")

pico_enable_stdio_uart(arcade_play 1)
pico_enable_stdio_usb(arcade_play 0)

# tinyusb
set(FAMILY rp2040)
set(BOARD pico_sdk)
set(TINYUSB_DEBUG_LEVEL 0)

# Add the standard library to the build
target_link_libraries(arcade_play
PRIVATE
        pico_stdlib
        
        tinyusb_host
        )

add_subdirectory(third_party/usb_midi_host)

target_include_directories(arcade_play
PRIVATE
    .
    third_party
)

# Add any user requested libraries
target_link_libraries(arcade_play
PRIVATE
        hardware_watchdog
        hardware_adc
	hardware_dma
        hardware_flash
        hardware_i2c
        hardware_pwm

        usb_midi_host
        )

if (PICO_BOARD STREQUAL "pico_w")
target_link_libraries(arcade_play
PRIVATE
        pico_cyw43_arch_none
#        pico_cyw43_arch_lwip_threadsafe_background
#        pico_btstack_cyw43
)
endif()        

pico_add_extra_outputs(arcade_play)

