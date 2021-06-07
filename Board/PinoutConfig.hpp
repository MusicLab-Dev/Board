
#pragma once

#ifndef HARDWARE_PIN_COUNT
# define HARDWARE_PIN_COUNT 3
#endif

namespace Pin
{
    static constexpr int Array[] = {
        29, 28, 27
    };
    static constexpr int Count = HARDWARE_PIN_COUNT;

    static_assert(sizeof(Array) / sizeof(*Array) >= Count, "Pin count too high for given pin array");
}
