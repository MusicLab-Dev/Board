/**
 * @ Author: Paul Creze
 * @ Description: Hardware module
 */

#if __has_include("wiringPi.h")
# define HAS_WIRING_PI true
#else
# define HAS_WIRING_PI false
#endif

#if HAS_WIRING_PI
# include "wiringPi.h"
#endif

#include "GPIO.hpp"

GPIO::GPIO(void) noexcept
{
#if HAS_WIRING_PI
    static bool setup = false;

    if (!setup)
        ::wiringPiSetup();
    setup = true;
#endif
}

GPIO::~GPIO(void) noexcept
{
}

void GPIO::SetPinMode(int pin, PinMode mode) noexcept
{
#if HAS_WIRING_PI
    int targetMode = 0;
    switch (mode) {
    case PinMode::Input:
        targetMode = INPUT;
        break;
    case PinMode::Output:
        targetMode = OUTPUT;
        break;
    case PinMode::PwmOutput:
        targetMode = PWM_OUTPUT;
        break;
    case PinMode::GpioClock:
        targetMode = GPIO_CLOCK;
        break;
    default:
        return;
    }
    ::pinMode(pin, targetMode);
#else
    static_cast<void>(pin);
    static_cast<void>(mode);
#endif
}

void GPIO::SetPullMode(int pin, PullMode mode) noexcept
{
#if HAS_WIRING_PI
    int targetMode = 0;
    switch (mode) {
    case PullMode::Up:
        targetMode = PUD_UP;
        break;
    case PullMode::Down:
        targetMode = PUD_DOWN;
        break;
    default:
        return;
    }
    pullUpDnControl(pin, targetMode);
#else
    static_cast<void>(pin);
    static_cast<void>(mode);
#endif
}

int GPIO::DigitalRead(int pin) noexcept
{
#if HAS_WIRING_PI
    return ::digitalRead(pin);
#else
    static_cast<void>(pin);
    return 0;
#endif
}

void GPIO::DigitalWrite(int pin, int value) noexcept
{
#if HAS_WIRING_PI
    ::digitalWrite(pin, value);
#else
    static_cast<void>(pin);
    static_cast<void>(value);
#endif
}

int GPIO::AnalogRead(int pin) noexcept
{
#if HAS_WIRING_PI
    return ::analogRead(pin);
#else
    static_cast<void>(pin);
    return 0;
#endif
}

void GPIO::AnalogWrite(int pin, int value) noexcept
{
#if HAS_WIRING_PI
    ::analogWrite(pin, value);
#else
    static_cast<void>(pin);
    static_cast<void>(value);
#endif
}

void GPIO::PwmWrite(int pin, int value) noexcept
{
#if HAS_WIRING_PI
    ::pwmWrite(pin, value);
#else
    static_cast<void>(pin);
    static_cast<void>(value);
#endif
}
