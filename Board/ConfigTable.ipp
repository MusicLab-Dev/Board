/**
 * @ Author: Paul Creze
 * @ Description: Configuration table
 */

#include <algorithm>

inline const std::string &ConfigTable::get(const Core::HashedName key, const std::string &defaultValue) const noexcept
{
    for (const auto &it : _table) {
        if (it.first != key)
            continue;
        return it.second;
    }
    return defaultValue;
}

template<typename As>
inline std::enable_if_t<std::is_integral_v<As> || std::is_floating_point_v<As>, As>
        ConfigTable::getAs(const Core::HashedName key, As defaultValue) const noexcept
{
    const std::string *str { nullptr };

    for (const auto &it : _table) {
        if (it.first != key)
            continue;
        str = &it.second;
    }
    if (!str)
        return defaultValue;
    if constexpr (std::is_integral_v<As>) {
        if constexpr (std::is_signed_v<As>)
            return static_cast<As>(std::stoll(*str));
        else
            return static_cast<As>(std::stoull(*str));
    } else if constexpr (std::is_floating_point_v<As>) {
        if constexpr (std::is_same_v<As, float>)
            return std::stof(*str);
        else if constexpr (std::is_same_v<As, double>)
            return std::stod(*str);
        else
            return std::stold(*str);
    }
}

inline void ConfigTable::parseLine(std::string &line)
{
    auto it = std::find(line.begin(), line.end(), '=');
    const auto keyLength = std::distance(line.begin(), it);

    if (it == line.end() || keyLength == 0)
        throw std::logic_error("[Board]\tConfigTable::parseLine: Invalid line\n\t'" + line + '\'');

    _table.push(
        Core::Hash(std::string_view(line.data(), keyLength)),
        std::string(it + 1, line.end())
    );
}