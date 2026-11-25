#pragma once

#include <source_location>

#include "Panic.hpp"

/**
 * @file
 * @brief Assertion and panic macros.
 *
 * VISCERA_ENABLE_ASSERTS is set for Debug and RelWithDebInfo. Release and
 * MinSizeRel leave it undefined, and a disabled assert does not evaluate
 * its condition. VISCERA_PANIC is compiled in every configuration.
 */

namespace viscera::detail
{
    /// @cond
    [[noreturn]] void assertFailed(const char *condition, const char *message, std::source_location where) noexcept;
    /// @endcond
} // namespace viscera::detail

/**
 * @brief Aborts when @p condition is false, in Debug and RelWithDebInfo.
 *
 * When asserts are compiled out, @p condition is an unevaluated operand.
 * Do not rely on it for side effects. Do not assert on files, network data,
 * config, or user text; those are Result failures.
 *
 * @param condition Expression that is true when the invariant holds.
 * @param message   Null-terminated explanation passed to panic.
 * @pre @p message points at a null-terminated string when asserts are on.
 * @post If asserts are on and @p condition is false, the process is aborted.
 * @post If asserts are off, @p condition is not evaluated and execution
 *       continues.
 * @warning A disabled assert does not check the invariant. Shipping builds
 *          leave VISCERA_ENABLE_ASSERTS undefined.
 * @see viscera::panic
 */
#ifdef VISCERA_ENABLE_ASSERTS
#define VISCERA_ASSERT(condition, message)                                                                         \
        (static_cast<bool>(condition)                                                                                  \
             ? static_cast<void>(0)                                                                                    \
             : ::viscera::detail::assertFailed(#condition, (message), ::std::source_location::current()))
#else
#define VISCERA_ASSERT(condition, message) static_cast<void>(sizeof(static_cast<bool>(condition)))
#endif

/**
 * @brief Aborts in every build configuration.
 *
 * @param message Null-terminated explanation passed to panic.
 * @pre @p message points at a null-terminated string.
 * @post The process is aborted.
 * @warning Does not unwind and does not call the logger.
 * @see viscera::panic
 */
#define VISCERA_PANIC(message) ::viscera::panic((message), ::std::source_location::current())
