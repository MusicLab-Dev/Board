/**
 * @ Author: Paul Creze
 * @ Description: Scheduler
 */

#include <iostream>

#include "Scheduler.hpp"

static bool Running = false;

/**
 * Arguments:
 * --config-path [String]
 *   -> Path to the config table file
 **/
Scheduler::Scheduler(std::vector<std::string> &&arguments)
{
    (void)arguments; // cast to remove error
    std::cout << "[Board]\tScheduler consctructor" << std::endl;
}

Scheduler::~Scheduler(void)
{
    std::cout << "[Board]\tScheduler destructor" << std::endl;
}

void Scheduler::run(void)
{
    std::cout << "[Board]\tBoard running..." << std::endl;

    using namespace std::chrono;

    constexpr auto ProcessDiscovery = [](Scheduler &scheduler, auto &mod, auto &previousTime, const auto &currentTime) {
        const auto elapsedTime = static_cast<std::size_t>(duration_cast<nanoseconds>(currentTime - previousTime).count());

        if (elapsedTime >= mod.discoveryRate()) {
            previousTime = currentTime;
            mod.discover(scheduler);
        }
    };

    steady_clock::time_point previousTick {};
    steady_clock::time_point previousHardwareDiscovery {};
    steady_clock::time_point previousNetworkDiscovery {};
    steady_clock::time_point currentTime;

    Running = true;

    while (Running) {
        currentTime = steady_clock::now();

        // Process discovery of each module if needed
        ProcessDiscovery(*this, _hardwareModule, previousHardwareDiscovery, currentTime);
        ProcessDiscovery(*this, _networkModule, previousNetworkDiscovery, currentTime);

        // Process tick of each module if needed
        const auto elapsedTime = static_cast<std::size_t>(duration_cast<nanoseconds>(currentTime - previousTick).count());
        if (elapsedTime > _cache.tickRate) {
            _hardwareModule.tick(*this);
            _networkModule.tick(*this);
            previousTick = currentTime;
        }
    }
}

void Scheduler::stop(void) noexcept
{
    Running = false;
}
