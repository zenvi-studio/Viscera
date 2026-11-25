#include <cstddef>
#include <string>
#include <viscera/error/error.hpp>

#include <gtest/gtest.h>

#include "DeathTest.hpp"

namespace
{
    constexpr std::size_t kOverlongPanicMessageBytes = 2048;

    class PanicDeath : public viscera::error_tests::DeathTest {};
} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PanicDeath, AbortsWithDiagnosticWhenLoggerWasNeverInitialized)
{
    EXPECT_DEATH(viscera::panic("boom"), "panic: boom");
    EXPECT_DEATH(viscera::panic("boom"), "PanicTests.cpp");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PanicDeath, FallbackOmitsCallerText)
{
    EXPECT_DEATH(viscera::panic(nullptr), "diagnostic formatting failed");
    EXPECT_DEATH(viscera::panic(nullptr), "PanicTests.cpp");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PanicDeath, TruncatesToTheEndMarker)
{
    const std::string message(kOverlongPanicMessageBytes, 'A');
    EXPECT_DEATH(viscera::panic(message.c_str()), "panic: AAA");
    EXPECT_DEATH(viscera::panic(message.c_str()), "\\.\\.\\.");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PanicDeath, PanicMacroAborts)
{
    EXPECT_DEATH(VISCERA_PANIC("always on"), "always on");
}
