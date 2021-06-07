/**
 * @ Author: Paul Creze
 * @ Description: Module
 */

#pragma once

#include "Types.hpp"

/** @brief Abstraction of a board module */
class alignas_eighth_cacheline Module
{
public:
    /** @brief Get the discovery rate duration */
    [[nodiscard]] Chrono::Duration discoveryRate(void) const noexcept { return _discoveryRate; }

    /** @brief Set the discovery rate duration */
    void setDiscoveryRate(const Chrono::Duration discoveryRate) noexcept { _discoveryRate = discoveryRate; }

private:
    Chrono::Duration _discoveryRate { 1000000000 };
};

static_assert_fit_eighth_cacheline(Module);
