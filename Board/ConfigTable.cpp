/**
 * @ Author: Paul Creze
 * @ Description: Configuration table
 */

#include "ConfigTable.hpp"

void ConfigTable::loadFile(std::istream &is)
{
    constexpr auto RemoveWhiteSpaces = [](std::string &line) {
        std::size_t index = 0ul;
        for (auto c : line) {
            if (!std::isspace(c))
                break;
            ++index;
        }
        line.erase(0, index);
    };

    std::string line;

    while (std::getline(is, line)) {
        RemoveWhiteSpaces(line);
        if (line.empty() || line.at(0) == '#')
            continue;
        parseLine(line);
    }
}