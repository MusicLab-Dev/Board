/**
 * @ Author: Paul Creze
 * @ Description: Program entry
 */

#include <iostream>

#include <Board/Scheduler.hpp>

int main(const int argc, const char * const * const argv)
{
    try {
        std::vector<std::string> arguments(argc - 1);
        for (auto i = 1; i < argc; ++i)
            arguments.emplace_back(argv[i]);
        Scheduler scheduler(std::move(arguments));
        scheduler.run();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "An error occured: " << e.what() << std::endl;
        return 1;
    }
}
