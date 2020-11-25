/**
 * @ Author: Paul Creze
 * @ Description: Common types
 */

#pragma once

#include <cstdint>
#include <chrono>

namespace Net
{
    /** @brief Network IP address */
    using IP = std::uint32_t;

    /** @brief Network port */
    using Port = std::uint16_t;

    /** @brief System socket */
    using Socket = std::int32_t;
}

namespace I2C
{
    /** @brief Hardware I2C socket */
    using Socket = std::int32_t;
}

namespace Chrono
{
    /** @brief Timestamp in nanoseconds */
    using Timestamp = std::chrono::steady_clock::time_point;

    /** @brief Time duration in nanoseconds */
    using Duration = std::size_t;
}