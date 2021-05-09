/**
 * @ Author: Paul Creze
 * @ Description: Scheduler
 */

#pragma once

#include <vector>
#include <string>
#include <fstream>

#include "Types.hpp"

#include "HardwareModule.hpp"
#include "NetworkModule.hpp"
#include "ConfigTable.hpp"
#include <Protocol/NetworkLog.hpp>

static std::ifstream stream("./Board/Board.conf");
static ConfigTable confTable(stream);

/** @brief The scheduler is responsible to coordinate each module in time */
class alignas(Core::CacheLineSize * 4) Scheduler
{
public:
    /** @brief Path of the configuration file */
    static constexpr auto DefaultConfigFilePath = "Config.conf";

    /** @brief Global connection state */
    enum class State : bool {
        Disconnected,
        Connected
    };

    /** @brief Construct the scheduler */
    Scheduler(std::vector<std::string> &&arguments);

    /** @brief Destruct the scheduler */
    ~Scheduler(void);


    /** @brief Run scheduler in blocking mode */
    void run(void);

    /** @brief Stop the scheduler ! Not thread safe ! */
    void stop(void) noexcept;


    /** @brief Get the connection state */
    [[nodiscard]] State state(void) noexcept { return _cache.state; }

    /** @brief Change the connection state of the scheduler */
    void setState(const State state) noexcept { _cache.state = state; }


    /** @brief Get the internal hardware module */
    [[nodiscard]] HardwareModule &hardwareModule(void) noexcept { return _hardwareModule; }

    /** @brief Get the internal network module */
    [[nodiscard]] NetworkModule &networkModule(void) noexcept { return _networkModule; }

private:
    struct alignas_cacheline Cache
    {
        State state { State::Disconnected };
        Chrono::Duration tickRate { 10000 };
    };

    Cache _cache;
    HardwareModule _hardwareModule;
    NetworkModule _networkModule;
};

static_assert_sizeof(Scheduler, Core::CacheLineSize * 4);
static_assert_alignof(Scheduler, Core::CacheLineSize * 4);

#include "HardwareModule.ipp"
#include "NetworkModule.ipp"
#include "Scheduler.ipp"
