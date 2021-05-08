/**
 * @ Author: Paul Creze
 * @ Description: Hardware module
 */

#pragma once

class GPIO
{
public:
    /** @brief Pin modes */
    enum class PinMode {
        Input,
        Output,
        PwmOutput,
        GpioClock
    };

    /** @brief Pin pull modes */
    enum class PullMode {
        Up,
        Down
    };

    /** @brief Initialize the GPIO instance */
    GPIO(void) noexcept;

    /** @brief Destroy the GPIO instance */
    ~GPIO(void) noexcept;

    /** @brief Set the pin mode */
    static void SetPinMode(int pin, PinMode mode) noexcept;

    /** @brief Set the pin pull mode */
    static void SetPullMode(int pin, PullMode mode) noexcept;


    /** @brief Read / Write a digital pin */
    [[nodiscard]] static int DigitalRead(int pin) noexcept;
    static void DigitalWrite(int pin, int value) noexcept;


    /** @brief Read / Write an analog pin */
    [[nodiscard]] static int AnalogRead(int pin) noexcept;
    static void AnalogWrite(int pin, int value) noexcept;


    /** @brief Write a PWM pin */
    static void PwmWrite(int pin, int value) noexcept;

private:
};
