/**
 * @ Author: Paul Creze
 * @ Description: Hardware module
 */

#pragma once

#include <Core/FlatVector.hpp>
#include <Protocol/Protocol.hpp>

#include "Module.hpp"

class Scheduler;

/** @brief Board module responsible of hardware communication */
class alignas_cacheline HardwareModule : public Module
{
public:
    /** @brief Construct the hardware module */
    HardwareModule(void) noexcept;

    /** @brief Destruct the hardware module */
    ~HardwareModule(void) noexcept;


    /** @brief Tick called at tick rate */
    void tick(Scheduler &scheduler) noexcept;

    /** @brief Discover called at discover rate */
    void discover(Scheduler &scheduler) noexcept;


    /** @brief Get the input event stack */
    [[nodiscard]] const auto &inputEvents(void) const noexcept { return _inputEvents; }

private:
    Core::FlatVector<Protocol::Control> _controls {};
    Core::FlatVector<Protocol::InputEvent> _inputEvents {};
    // std::uint32_t _multiplexers { 0 };
};

static_assert_fit_cacheline(HardwareModule);
