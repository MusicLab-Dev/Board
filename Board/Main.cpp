/**
 * @ Author: Paul Creze
 * @ Description: Program entry
 */

#include <iostream>

#include <Board/Scheduler.hpp>
#include <Board/GPIO.hpp>

int main(const int argc, const char * const * const argv)
{
    try {
        GPIO gpioInstance;
        std::vector<std::string> arguments(argc - 1);
        for (auto i = 1; i < argc; ++i)
            arguments.emplace_back(argv[i]);
        Scheduler scheduler(std::move(arguments));
        arguments.clear();
        arguments.shrink_to_fit();
        scheduler.run();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "An error occured: " << e.what() << std::endl;
        return 1;
    }
}
