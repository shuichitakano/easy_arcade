#include "switch_pro.h"
#include <cassert>
#include <cstdio>

int main()
{
    using namespace SwitchPro;
    for (bool ackFirst : {false, true})
    {
        Initializer init;
        Output output;
        const uint8_t ack[] = {0x81, 2};
        assert(init.nextOutput(0, output) && output.bytes[1] == 2);
        init.submitted(0);
        if (!ackFirst) init.completed(64);
        init.received(ack, sizeof(ack));
        assert(init.state() == Initializer::State::HandshakeAgain);
        if (ackFirst)
        {
            assert(!init.nextOutput(1, output));
            init.completed(64);
        }
        assert(init.nextOutput(2, output));
        assert(output.length == 64 && output.bytes[0] == 0x80 && output.bytes[1] == 2);
        init.submitted(2);
        init.completed(64);
        init.received(ack, sizeof(ack));
        assert(init.state() == Initializer::State::EnableUsb);
        assert(init.nextOutput(3, output) && output.bytes[1] == 4);
        init.submitted(3);
        init.completed(64);
        assert(init.state() == Initializer::State::FactoryCalibration);
        // Remount also follows the same no-baud-change sequence.
        init = {};
        assert(init.nextOutput(0, output));
        init.submitted(0);
        init.completed(64);
        init.received(ack, sizeof(ack));
        assert(init.state() == Initializer::State::HandshakeAgain);
    }
    std::puts("Switch no-baud-change sequence and buffer ownership tests passed");
}
