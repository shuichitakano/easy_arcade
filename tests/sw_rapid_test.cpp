#include <cassert>
#include <cstdint>
#include <climits>
#include <iostream>
constexpr int CPU_CLOCK = 125000000;
#include "sw_rapid.inc"

int main()
{
    for (int speed = 1; speed <= 30; ++speed)
    {
        const int interval = CPU_CLOCK / (speed * 2);
        SwRapidFire timer;
        timer.update(interval - 1, speed);
        assert(timer.getCounter() == 0);
        timer.update(1, speed);
        assert(timer.getCounter() == 1);
        timer.update(0, speed);
        assert(timer.getCounter() == 1);
        timer.update(3 * interval + 7, speed);
        assert(timer.getCounter() == 4);
        timer.update(interval - 7, speed);
        assert(timer.getCounter() == 5);

        // Chunking elapsed time must not change the resulting phase/count.
        SwRapidFire chunked, batched;
        for (int i = 0; i < 100; ++i)
            chunked.update(100000, speed);
        batched.update(10000000, speed);
        assert(chunked.getCounter() == batched.getCounter());

        // The remainder plus a large delta must not overflow signed int.
        SwRapidFire large;
        large.update(interval - 1, speed);
        large.update(INT_MAX, speed);
        assert(large.getCounter() ==
               (static_cast<uint64_t>(INT_MAX) + interval - 1) / interval);
    }
    // Changing speed uses the accumulated time at the new interval.
    SwRapidFire changed;
    changed.update(CPU_CLOCK / 4, 1);
    assert(changed.getCounter() == 0);
    changed.update(0, 30);
    assert(changed.getCounter() == (CPU_CLOCK / 4) / (CPU_CLOCK / 60));
    std::cout << "software rapid timer tests passed\n";
}
