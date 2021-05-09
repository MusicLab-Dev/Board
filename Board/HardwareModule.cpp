/**
 * @ Author: Paul Creze
 * @ Description: Hardware module
 */

#include <Protocol/NetworkLog.hpp>
#include "Scheduler.hpp"
#include "GPIO.hpp"
#include "PinoutConfig.hpp"

HardwareModule::HardwareModule(void) noexcept
{
    _controls.resize(Pin::Count);
    int i = 0;
    for (auto &ctrl : _controls) {
        ctrl.type = Control::Type::Button;
        GPIO::SetPinMode(i, GPIO::PinMode::Input);
        GPIO::SetPullMode(i, GPIO::PullMode::Up);
    }
}

HardwareModule::~HardwareModule(void) noexcept
{

}

void HardwareModule::tick(Scheduler &scheduler) noexcept
{
    if (scheduler.state() != Scheduler::State::Connected)
        return;
    _inputEvents.clear();
    for (auto i = 0; i < Pin::Count; ++i) {
        const auto value = static_cast<std::uint8_t>(!GPIO::DigitalRead(Pin::Array[i]));
        auto &ctrl = _controls.at(static_cast<std::size_t>(i));
        if (value == ctrl.value1)
            continue;
        ctrl.value1 = value;
        _inputEvents.push(InputEvent {
            static_cast<std::uint8_t>(i),
            ctrl.value1
        });
        NETWORK_LOG("Input event ", i, static_cast<int>(ctrl.value1));
    }
}

void HardwareModule::discover(Scheduler &scheduler) noexcept
{
    if (scheduler.state() != Scheduler::State::Connected)
        return;
}
