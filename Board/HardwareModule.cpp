/**
 * @ Author: Paul Creze
 * @ Description: Hardware module
 */

#include "Scheduler.hpp"

HardwareModule::HardwareModule(void) noexcept
{

}

HardwareModule::~HardwareModule(void) noexcept
{

}

void HardwareModule::tick(Scheduler &scheduler) noexcept
{
    if (scheduler.state() != Scheduler::State::Connected)
        return;
}

void HardwareModule::discover(Scheduler &scheduler) noexcept
{
    if (scheduler.state() != Scheduler::State::Connected)
        return;
}