#include <memory>
#include <string>
#include <utility>
#include <viscera/error/error.hpp>

#include <gtest/gtest.h>

#include "SampleError.hpp"

using viscera::fail;
using viscera::Result;
using viscera::error_tests::SampleError;

namespace
{
    constexpr int kParsedValue = 7;

    struct MoveOnly
    {
        std::unique_ptr<int> value;

        MoveOnly() = default;

        explicit MoveOnly(const int number) :
            value(std::make_unique<int>(number)) {}
    };

    [[nodiscard]] Result<int> parseOk()
    {
        return kParsedValue;
    }

    [[nodiscard]] Result<void> done()
    {
        return {};
    }

    [[nodiscard]] Result<int> parseFail()
    {
        return fail(SampleError::Missing);
    }

    [[nodiscard]] Result<int> propagate()
    {
        auto value = parseFail();
        if (!value)
        {
            return std::unexpected{std::move(value).error().withContext("propagating")};
        }

        return *value;
    }

    [[nodiscard]] Result<MoveOnly> makeMoveOnly()
    {
        return MoveOnly{3};
    }
} // namespace

TEST(ResultTests, SuccessAndVoidAndMoveOnly)
{
    const auto number = parseOk();
    ASSERT_TRUE(number);
    EXPECT_EQ(*number, kParsedValue);

    const auto empty = done();
    EXPECT_TRUE(empty);

    auto moved = makeMoveOnly();
    ASSERT_TRUE(moved);
    ASSERT_NE(moved->value, nullptr);
    EXPECT_EQ(*moved->value, 3);
}

TEST(ResultTests, PropagationAddsContext)
{
    const auto result = propagate();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().message(), "propagating");
    ASSERT_NE(result.error().cause(), nullptr);
    EXPECT_TRUE(result.error().cause()->is(SampleError::Missing));
    EXPECT_FALSE(result.error().is(SampleError::Broken));
}

TEST(ResultTests, FailRecordsTheCallSite)
{
    const auto codeLine = static_cast<std::uint32_t>(std::source_location::current().line() + 1);
    const auto codeOnly = fail(SampleError::Missing);
    const auto formatLine = static_cast<std::uint32_t>(std::source_location::current().line() + 1);
    const auto formatted = fail(SampleError::Broken, "token {}", "abc");

    EXPECT_EQ(codeOnly.error().location().line(), codeLine);
    EXPECT_EQ(formatted.error().location().line(), formatLine);
    EXPECT_EQ(formatted.error().message(), "token abc");
    EXPECT_NE(std::string{codeOnly.error().location().file_name()}.find("ResultTests.cpp"), std::string::npos);
    EXPECT_EQ(std::string{codeOnly.error().location().file_name()}.find("Result.hpp"), std::string::npos);
}
