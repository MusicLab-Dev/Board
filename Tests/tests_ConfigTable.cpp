/**
 * @ Author: Paul Creze
 * @ Description: Configuration table unit tests
 */

#include <sstream>

#include <gtest/gtest.h>

#include <Board/ConfigTable.hpp>

TEST(ConfigTable, Basics)
{
    std::istringstream iss("Hello=World");
    ConfigTable table(iss);

    ASSERT_EQ(table.get("Hello"), "World");
    ASSERT_EQ(table.get(Core::Hash("Hello")), "World");
}

TEST(ConfigTable, Comments)
{
    std::istringstream iss(
        "#This=is a comment line\n"
        "Hello=World\n"
        "#TEST=VALUE\n"
    );
    ConfigTable table(iss);

    ASSERT_EQ(table.get("This", "NotFound"), "NotFound");
    ASSERT_EQ(table.get("Hello"), "World");
    ASSERT_EQ(table.get("TEST", "42"), "42");
}

TEST(ConfigTable, Convert)
{
    std::istringstream iss(
        "INT=42\n"
        "FLOAT=420.5\n"
    );
    ConfigTable table(iss);

    ASSERT_EQ(table.get("INT"), "42");
    ASSERT_EQ(table.getAs<std::int8_t>("INT"), std::int8_t { 42 });
    ASSERT_EQ(table.getAs<std::int16_t>("INT"), std::int16_t { 42 });
    ASSERT_EQ(table.getAs<std::int32_t>("INT"), std::int32_t { 42 });
    ASSERT_EQ(table.getAs<std::int64_t>("INT"), std::int64_t { 42 });
    ASSERT_EQ(table.getAs<std::uint8_t>("INT"), std::uint8_t { 42 });
    ASSERT_EQ(table.getAs<std::uint16_t>("INT"), std::uint16_t { 42 });
    ASSERT_EQ(table.getAs<std::uint32_t>("INT"), std::uint32_t { 42 });
    ASSERT_EQ(table.getAs<std::uint64_t>("INT"), std::uint64_t { 42 });

    ASSERT_EQ(table.get("FLOAT"), "420.5");
    ASSERT_EQ(table.getAs<float>("FLOAT"), 420.5f);
    ASSERT_EQ(table.getAs<double>("FLOAT"), 420.5);
    ASSERT_EQ(table.getAs<long double>("FLOAT"), 420.5l);
}

TEST(ConfigTable, Errors)
{
    constexpr auto TestCrash = [](const std::string &content) {
        std::istringstream iss(content);
        ASSERT_ANY_THROW(ConfigTable table(iss));
    };

    TestCrash("HelloWorld");
    TestCrash("=HelloWorld");
}

TEST(ConfigTable, Advanced)
{
    std::istringstream iss(
        "# This is a comment\n"
        "        VariableA=123\n"
        "      X=hello world \n"
        "    TrickyVar==\n"
        "            # # Another comment # #\n"
        "\n"
        "           \n"
        "Y=42.5\n"
        "W=\n"
    );
    ConfigTable table(iss);

    ASSERT_EQ(table.getAs<int>("VariableA"), int { 123 });
    ASSERT_EQ(table.getAs<unsigned>("VariableA"), unsigned { 123 });
    ASSERT_EQ(table.get("X"), "hello world ");
    ASSERT_EQ(table.get("TrickyVar"), "=");
    ASSERT_EQ(table.getAs<float>("Y"), 42.5f);
    ASSERT_EQ(table.getAs<float>("Z", 42.5f), 42.5f);
    ASSERT_EQ(table.get("W", "Error"), std::string());
}