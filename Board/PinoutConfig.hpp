
#pragma once

#ifndef HARDWARE_PIN_COUNT
# define HARDWARE_PIN_COUNT 0
#endif

namespace Pin
{
    static constexpr int Array[] = {
        40, 38, 36
    };
    static constexpr int Count = HARDWARE_PIN_COUNT;

    static_assert(sizeof(Array) / sizeof(*Array) > Count, "Pin count too high for given pin array");
}