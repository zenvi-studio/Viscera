#include <stdexcept>
#include <string>
#include <string_view>
#include <viscera/error/error.hpp>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "SampleError.hpp"

using viscera::Error;
using viscera::fail;
using viscera::kMaxErrorFormatFrames;
using viscera::error_tests::SampleError;

namespace
{
    struct BoomFormat {};

    // A volatile read keeps the throw from looking unconditional. An
    // unconditional throw makes MSVC call the formatter [[noreturn]] and
    // then reject fmt's format_custom as unreachable (C4702).
    [[nodiscard]] bool formatShouldThrow() noexcept
    {
        const volatile int gate = 1;
        return gate != 0;
    }

    [[nodiscard]] std::string rendered(const Error &error)
    {
        return fmt::format("{}", error);
    }

    [[nodiscard]] std::string locationSuffix(const Error &error)
    {
        const char *const file = error.location().file_name();
        return fmt::format(" ({}:{})", file != nullptr ? file : "unknown", error.location().line());
    }

    [[nodiscard]] int countToken(const std::string &text, const std::string_view token)
    {
        int count = 0;
        for (std::size_t position = 0; (position = text.find(token, position)) != std::string::npos;
             position += token.size())
        {
            ++count;
        }

        return count;
    }
} // namespace

template <>
struct fmt::formatter<BoomFormat>
{
    static constexpr auto parse(const format_parse_context &context) { return context.begin(); }

    static auto format(const BoomFormat &, format_context &context) -> format_context::iterator
    {
        if (formatShouldThrow())
        {
            throw std::runtime_error("formatter boom");
        }

        return context.out();
    }
};

TEST(ErrorFormatTests, FrameCapIsEight)
{
    EXPECT_EQ(kMaxErrorFormatFrames, 8);
}

TEST(ErrorFormatTests, CodeOnlyLineHasNoColon)
{
    const Error error{SampleError::Missing};
    EXPECT_EQ(rendered(error), "[core] missing thing" + locationSuffix(error));
}

TEST(ErrorFormatTests, MessageAndWrapUseTheGrammar)
{
    const Error root = fail(SampleError::Missing, "open failed").error();
    const Error noted = root.withContext("loading {}", "ship");
    EXPECT_EQ(rendered(noted), "[core] missing thing: loading ship: open failed" + locationSuffix(root));
    EXPECT_EQ(countToken(rendered(noted), "[core]"), 1);

    const Error wrapped = root.wrap(SampleError::Broken, "texture {}", "a.png");
    EXPECT_EQ(
        rendered(wrapped),
        "[core] broken thing: texture a.png: [core] missing thing: open failed" + locationSuffix(root)
    );
    EXPECT_EQ(countToken(rendered(wrapped), "[core]"), 2);
}

TEST(ErrorFormatTests, EmptyWrapMessageDoesNotAddAColon)
{
    const Error root{SampleError::Missing};
    const Error wrapped = root.wrap(SampleError::Broken, "{}", BoomFormat{});
    EXPECT_EQ(rendered(wrapped), "[core] broken thing: [core] missing thing" + locationSuffix(root));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(ErrorFormatTests, EightFramesKeepTheRootAndNineTruncate)
{
    Error within = fail(SampleError::Missing, "root-token").error();
    const Error withinRoot = within;
    for (int index = 1; index <= kMaxErrorFormatFrames - 1; ++index)
    {
        within = within.withContext("frame-{}", index);
    }

    const std::string withinText = rendered(within);
    EXPECT_TRUE(withinText.contains("root-token"));
    EXPECT_TRUE(withinText.contains(fmt::format("frame-{}", kMaxErrorFormatFrames - 1)));
    EXPECT_FALSE(withinText.contains(" ..."));
    EXPECT_TRUE(withinText.ends_with(locationSuffix(withinRoot)));
    EXPECT_EQ(countToken(withinText, "[core]"), 1);

    Error beyond = fail(SampleError::Missing, "root-token").error();
    const Error beyondRoot = beyond;
    for (int index = 1; index <= kMaxErrorFormatFrames; ++index)
    {
        beyond = beyond.withContext("frame-{}", index);
    }

    const std::string beyondText = rendered(beyond);
    EXPECT_TRUE(beyondText.contains(fmt::format("frame-{}", kMaxErrorFormatFrames)));
    EXPECT_TRUE(beyondText.contains("frame-1"));
    EXPECT_FALSE(beyondText.contains("root-token"));
    EXPECT_TRUE(beyondText.contains(" ..."));
    EXPECT_TRUE(beyondText.ends_with(locationSuffix(beyond)));
    EXPECT_FALSE(beyondText.contains(locationSuffix(beyondRoot)));
}

TEST(ErrorFormatTests, NestedWrapThenContextPrintsBothCodes)
{
    Error error = fail(SampleError::Missing, "errno {}", 2).error();
    const Error origin = error;
    error = error.wrap(SampleError::Broken, "texture {}", "a.png");
    error = error.withContext("loading level {}", "ship");

    const std::string text = rendered(error);
    EXPECT_EQ(
        text,
        "[core] broken thing: loading level ship: texture a.png: [core] missing thing: errno 2" +
        locationSuffix(origin)
    );
    EXPECT_EQ(countToken(text, "[core]"), 2);
}
