/**
 * @ Author: Matthieu Moinvaziri
 * @ Description: Scheduler unit tests
 */

#include <thread>
#include <atomic>

#include <gtest/gtest.h>

#include <Board/Scheduler.hpp>

TEST(Scheduler, ExternalTestTemplate)
{
    std::atomic<bool> started = false;
    Scheduler scheduler({});
    std::thread thd([&scheduler, &started] {
        started = true;
        scheduler.run();
    });

    // Wait until the thread is started
    while (!started);

    // Execute external tests
    // ...

    // Stop and join scheduler
    scheduler.stop();
    if (thd.joinable())
        thd.join();
}

TEST(Scheduler, InternalTestTemplate)
{
    Scheduler scheduler({});

    // Execute internal tests
    scheduler.networkModule().tick(scheduler);
    // ...
}