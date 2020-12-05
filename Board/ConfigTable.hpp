/**
 * @ Author: Paul Creze
 * @ Description: Configuration table
 */

#pragma once

#include <string>
#include <istream>

#include <Core/Hash.hpp>
#include <Core/Vector.hpp>

class ConfigTable
{
public:
    using Row = std::pair<Core::HashedName, std::string>;
    using Table = Core::Vector<Row, std::uint16_t>;

    ConfigTable(std::istream &is)
        { loadFile(is); }

    ~ConfigTable(void) noexcept = default;

    /** @brief Get a value from the table using its key */
    template<typename Key>
    [[nodiscard]] const std::string &get(const Key &key, const std::string &defaultValue = std::string()) const noexcept
        { return get(Core::Hash(key), defaultValue); }


    /** @brief Get a value from the table using its hashed key */
    [[nodiscard]] const std::string &get(const Core::HashedName key, const std::string &defaultValue = std::string()) const noexcept;

    /** @brief Get a value converted to 'As' from the table using its key */
    template<typename As, typename Key>
    [[nodiscard]] std::enable_if_t<std::is_integral_v<As> || std::is_floating_point_v<As>, As>
            getAs(const Key &key, As defaultValue = As()) const noexcept
        { return getAs<As>(Core::Hash(key), defaultValue); }

    /** @brief Get a value converted to 'As' from the table using its hashed key */
    template<typename As>
    [[nodiscard]] std::enable_if_t<std::is_integral_v<As> || std::is_floating_point_v<As>, As>
            getAs(const Core::HashedName key, As defaultValue = As()) const noexcept;

private:
    Table _table {};

    /** @brief Load a file at given path */
    void loadFile(std::istream &ifs);

    /** @brief Parse a single line */
    void parseLine(std::string &line);
};

#include "ConfigTable.ipp"