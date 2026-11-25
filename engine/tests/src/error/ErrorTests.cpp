#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <viscera/error/error.hpp>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "DeathTest.hpp"
#include "SampleError.hpp"
#include "error_standalone.hpp"

using viscera::domainName;
using viscera::Error;
using viscera::ErrorCode;
using viscera::ErrorDomain;
using viscera::fail;
using viscera::kMaxErrorBytes;
using viscera::error_tests::SampleError;

namespace
{
    // NOLINTNEXTLINE(performance-enum-size)
    enum class SignedCode : int
    {
        Missing = 1,
    };

    // NOLINTNEXTLINE(performance-enum-size)
    enum class WideCode : std::uint32_t
    {
        Missing = 1,
    };

    // NOLINTNEXTLINE(performance-enum-size, cppcoreguidelines-use-enum-class)
    enum UnscopedCode : std::uint16_t
    {
        Missing = 1,
    };

    // NOLINTNEXTLINE(performance-enum-size)
    enum class NoHooks : std::uint16_t
    {
        Missing = 1,
    };

    enum class NarrowCode : std::uint8_t
    {
        Missing = 1,
    };

    [[nodiscard]] constexpr viscera::ErrorDomain errorDomain(const NarrowCode) noexcept
    {
        return viscera::ErrorDomain::Core;
    }

    [[nodiscard]] constexpr std::string_view errorMessage(const NarrowCode) noexcept
    {
        return "narrow";
    }

    struct BoomFormat {};

    struct BoomAlloc {};

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

    class ErrorDeath : public viscera::error_tests::DeathTest {};
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

template <>
struct fmt::formatter<BoomAlloc>
{
    static constexpr auto parse(const format_parse_context &context) { return context.begin(); }

    static auto format(const BoomAlloc &, format_context &context) -> format_context::iterator
    {
        if (formatShouldThrow())
        {
            throw std::bad_alloc();
        }

        return context.out();
    }
};

static_assert(!viscera::ErrorEnum<SignedCode>);
static_assert(!viscera::ErrorEnum<WideCode>);
static_assert(!viscera::ErrorEnum<UnscopedCode>);
static_assert(!viscera::ErrorEnum<NoHooks>);
static_assert(viscera::ErrorEnum<NarrowCode>);
static_assert(!std::is_convertible_v<SampleError, Error>);
static_assert(std::is_constructible_v<Error, SampleError>);

// NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
TEST(ErrorCodeTests, EqualityDistinguishesDomainAndValue)
{
    RecordProperty("sizeof_ErrorCode", std::to_string(sizeof(ErrorCode)));
    const ErrorCode missing{.domain = ErrorDomain::Core, .value = 1};
    const ErrorCode broken{.domain = ErrorDomain::Core, .value = 2};
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const ErrorCode otherDomain{.domain = static_cast<ErrorDomain>(255), .value = 1};

    EXPECT_EQ(missing, missing);
    EXPECT_NE(missing, broken);
    EXPECT_NE(missing, otherDomain);
    EXPECT_EQ(static_cast<std::uint16_t>(ErrorDomain::Core), 1U);
    EXPECT_EQ(static_cast<std::uint16_t>(SampleError::Missing), 1U);
    EXPECT_EQ(static_cast<std::uint16_t>(SampleError::Broken), 2U);
    EXPECT_EQ(domainName(ErrorDomain::Core), "core");
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_EQ(domainName(static_cast<ErrorDomain>(255)), "unknown");
    EXPECT_EQ(errorDomain(NarrowCode::Missing), ErrorDomain::Core);
    EXPECT_EQ(errorMessage(NarrowCode::Missing), "narrow");
}

TEST(ErrorTests, CodeOnlyStoresIdentityAndOrigin)
{
    RecordProperty("sizeof_Error", std::to_string(sizeof(Error)));
    EXPECT_LE(sizeof(Error), kMaxErrorBytes);

    const auto line = static_cast<std::uint32_t>(std::source_location::current().line() + 1);
    const Error error{SampleError::Missing};

    EXPECT_EQ(error.code().domain, ErrorDomain::Core);
    EXPECT_EQ(error.code().value, 1U);
    EXPECT_EQ(error.description(), "missing thing");
    EXPECT_EQ(error.description().data(), viscera::error_tests::errorMessage(SampleError::Missing).data());
    EXPECT_TRUE(error.message().empty());
    EXPECT_EQ(error.cause(), nullptr);
    EXPECT_TRUE(error.is(SampleError::Missing));
    EXPECT_FALSE(error.is(SampleError::Broken));
    EXPECT_TRUE(error.is(ErrorCode{.domain = ErrorDomain::Core, .value = 1}));
    EXPECT_FALSE(error.is(ErrorCode{.domain = ErrorDomain::Core, .value = 2}));
    EXPECT_EQ(error.location().line(), line);
    EXPECT_NE(std::string{error.location().file_name()}.find("ErrorTests.cpp"), std::string::npos);

    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const Error copy{error};
    EXPECT_EQ(copy.code(), error.code());
    EXPECT_EQ(copy.description().data(), error.description().data());
    EXPECT_EQ(copy.location().line(), error.location().line());
    EXPECT_TRUE(copy.message().empty());
    EXPECT_EQ(copy.cause(), nullptr);
}

TEST(ErrorTests, MoveLeavesDestinationHoldingTheFrame)
{
    Error original = fail(SampleError::Missing, "token {}", "same").error();
    const char *const data = original.message().data();
    Error moved{std::move(original)};
    EXPECT_EQ(moved.message(), "token same");
    EXPECT_EQ(moved.message().data(), data);
}

TEST(ErrorTests, ContextCopySharesTheFrame)
{
    const Error original = fail(SampleError::Missing, "token {}", "same").error();
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const Error copy{original};
    EXPECT_EQ(copy.message().data(), original.message().data());
    EXPECT_EQ(copy.message(), "token same");
    EXPECT_TRUE(copy.is(SampleError::Missing));
}

TEST(ErrorTests, WithContextKeepsCodeAndLinksCause)
{
    const Error root{SampleError::Missing};
    const Error outer = root.withContext("loading {}", "ship");

    EXPECT_TRUE(outer.is(SampleError::Missing));
    EXPECT_EQ(outer.description(), root.description());
    EXPECT_EQ(outer.message(), "loading ship");
    ASSERT_NE(outer.cause(), nullptr);
    EXPECT_TRUE(outer.cause()->is(SampleError::Missing));
    EXPECT_TRUE(outer.cause()->message().empty());
    EXPECT_EQ(outer.cause()->location().line(), root.location().line());
    EXPECT_NE(outer.location().line(), root.location().line());
    EXPECT_TRUE(rendered(outer).ends_with(locationSuffix(root)));
}

TEST(ErrorTests, WrapChangesTopLevelCodeOnly)
{
    const Error root = fail(SampleError::Missing, "open failed").error();
    const Error wrapped = root.wrap(SampleError::Broken, "texture {}", "a.png");

    EXPECT_TRUE(wrapped.is(SampleError::Broken));
    EXPECT_FALSE(wrapped.is(SampleError::Missing));
    EXPECT_EQ(wrapped.description(), "broken thing");
    EXPECT_EQ(wrapped.message(), "texture a.png");
    ASSERT_NE(wrapped.cause(), nullptr);
    EXPECT_TRUE(wrapped.cause()->is(SampleError::Missing));
    EXPECT_EQ(wrapped.cause()->message(), "open failed");
    EXPECT_FALSE(root.is(SampleError::Broken));
}

TEST(ErrorTests, FormatFailureOmitsTextWithoutDroppingIdentity)
{
    const auto failed = fail(SampleError::Missing, "{}", BoomFormat{});
    EXPECT_TRUE(failed.error().is(SampleError::Missing));
    EXPECT_TRUE(failed.error().message().empty());
    EXPECT_EQ(failed.error().cause(), nullptr);
    EXPECT_EQ(failed.error().description(), "missing thing");

    const Error root{SampleError::Missing};
    const Error same = root.withContext("{}", BoomFormat{});
    EXPECT_EQ(same.location().line(), root.location().line());
    EXPECT_TRUE(same.message().empty());
    EXPECT_EQ(same.cause(), nullptr);
    EXPECT_TRUE(same.is(SampleError::Missing));

    const Error wrapped = root.wrap(SampleError::Broken, "{}", BoomFormat{});
    EXPECT_TRUE(wrapped.is(SampleError::Broken));
    EXPECT_TRUE(wrapped.message().empty());
    ASSERT_NE(wrapped.cause(), nullptr);
    EXPECT_TRUE(wrapped.cause()->is(SampleError::Missing));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(ErrorDeath, AllocationFailurePanics)
{
    EXPECT_DEATH(static_cast<void>(fail(SampleError::Missing, "{}", BoomAlloc{})), "panic:");

    const Error root{SampleError::Missing};
    EXPECT_DEATH(static_cast<void>(root.withContext("{}", BoomAlloc{})), "panic:");
    EXPECT_DEATH(static_cast<void>(root.wrap(SampleError::Broken, "{}", BoomAlloc{})), "panic:");
}

TEST(ErrorStandaloneTests, UmbrellaBuildsWithoutLoggingHeaders)
{
    EXPECT_TRUE(viscera::error_standalone::exercise());
}
