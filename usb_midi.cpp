/*
 * author : Shuichi TAKANO
 * since  : Mon Feb 12 2024 20:11:46
 */

#include <cstdint>

#include <tusb.h>
#include <usb_midi_host/usb_midi_host.h>
#include "pad_manager.h"
#include "debug.h"

namespace
{
    struct MIDIState
    {
        uint32_t keys_[128 / 32]{};

        void clear()
        {
            for (auto &k : keys_)
            {
                k = 0;
            }
        }

        void keyon(int note)
        {
            assert(note < 128);
            keys_[note >> 5] |= 1u << (note & 31);
        }

        void keyoff(int note)
        {
            assert(note < 128);
            keys_[note >> 5] &= ~(1u << (note & 31));
        }
    };

    MIDIState midiState_;
}

extern "C"
{
    void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx)
    {
        DPRINT(("MIDI device address = %u, IN endpoint %u has %u cables, OUT endpoint %u has %u cables\r\n",
                dev_addr, in_ep & 0xf, num_cables_rx, out_ep & 0xf, num_cables_tx));

        midiState_.clear();
    }

    void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
    {
        // MIDI じゃないやつからも来ちゃう
        DPRINT(("MIDI device address = %d, instance = %d is unmounted\r\n", dev_addr, instance));
    }

    void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
    {
        while (num_packets--)
        {
            uint8_t packet[4]{};
            if (!tuh_midi_packet_read(dev_addr, packet))
            {
                break;
            }

            int kind = packet[0] & 0xf;
            int note = packet[2] & 127;
            int vel = packet[3];
            if (kind == 8 || (kind == 9 && vel == 0))
            {
                midiState_.keyoff(note);
            }
            else if (kind == 9)
            {
                midiState_.keyon(note);
            }
#if 0
            printf("#%d:%d: ", packet[0] >> 4, packet[0] & 0xf);
            for (uint32_t i = 1; i < 4; ++i)
            {
                printf("%02x ", packet[i]);
            }
            printf("\n");
#endif
        }

        PadManager::PadInput pi;
        // vid/pidはダミー。どのIFでも同じにする
        pi.vid = PadManager::VID_MIDI;
        pi.pid = PadManager::PID_MIDI;
        pi.buttons[0] = midiState_.keys_[0];
        pi.buttons[1] = midiState_.keys_[1];
        pi.buttons[2] = midiState_.keys_[2];
        pi.buttons[3] = midiState_.keys_[3];
        constexpr int port = static_cast<int>(PadManager::StateKind::MIDI);
        PadManager::instance().setData(port, pi);
    }

    void tuh_midi_tx_cb(uint8_t dev_addr)
    {
    }
}