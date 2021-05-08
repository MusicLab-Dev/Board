/**
 * @ Author: Paul Creze
 * @ Description: Hardware module
 */

#include "Scheduler.hpp"
#include "GPIO.hpp"
#include "PinoutConfig.hpp"

HardwareModule::HardwareModule(void) noexcept
{
    _controls.resize(Pin::Count);
    for (auto &ctrl : _controls)
        ctrl.type = Control::Type::Button;
}

HardwareModule::~HardwareModule(void) noexcept
{

}

void HardwareModule::tick(Scheduler &scheduler) noexcept
{
    if (scheduler.state() != Scheduler::State::Connected)
        return;
    _events.clear();
    for (auto i = 0; i < Pin::Count; ++i) {
        const auto value = static_cast<std::uint8_t>(GPIO::DigitalRead(Pin::Array[i]));
        auto &ctrl = _controls.at(static_cast<std::size_t>(i));
        if (value == ctrl.value1)
            continue;
        ctrl.value1 = value;
        _events.push(InputEvent {
            static_cast<std::uint8_t>(i),
            ctrl.value1
        });
    }
}

void HardwareModule::discover(Scheduler &scheduler) noexcept
{
    if (scheduler.state() != Scheduler::State::Connected)
        return;
}