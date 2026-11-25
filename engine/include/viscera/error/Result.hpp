#pragma once

#include <expected>
#include <source_location>
#include <type_traits>
#include <utility>
#include <viscera/core/FormatString.hpp>

#include "ErrorType.hpp"

/**
 * @file
 * @brief std::expected<T, Error> and the fail() helpers that build one.
 *
 * Result is an alias, not a subclass. Put [[nodiscard]] on every function
 * that returns a Result. Engine code checks the result and then uses * or
 * ->. It does not call .value(), which throws.
 */

namespace viscera
{
    /**
     * @brief Success value or a viscera::Error.
     *
     * @tparam T Success type. @c Result<void> carries no value.
     * @note [[nodiscard]] goes on the function that returns a Result. The
     *       alias itself cannot carry the attribute portably.
     * @note Success for @c Result<void> is @c return {}.
     * @pre A caller that writes @c *result or @c result-> has already checked
     *      that the result holds a value. Dereferencing the error state is a
     *      precondition violation.
     * @warning Do not call @c .value(). It throws @c std::bad_expected_access,
     *          and engine control flow does not use that exception.
     * @see viscera::fail
     * @see viscera::Error
     */
    template <typename T>
    using Result = std::expected<T, Error>;

    /**
     * @brief Builds a code-only unexpected error.
     *
     * The defaulted source location is last, so it is evaluated at the
     * caller. This function forwards that location and does not capture
     * another one.
     *
     * @tparam E Subsystem enum.
     * @param code  Failure identity.
     * @param where Location stored on the error. Defaults to the call site.
     * @return An unexpected error the caller can return from a Result function.
     * @pre errorMessage(code) points at static storage.
     * @post The error is code-only, and its location is @p where.
     * @note Allocation failure does not apply: this overload does not allocate.
     *       The function is noexcept because a context allocation failure
     *       panics inside the formatted overload rather than escaping.
     */
    template <ErrorEnum E>
    [[nodiscard]] std::unexpected<Error>
        fail(E code, std::source_location where = std::source_location::current()) noexcept;

    /**
     * @brief Builds an unexpected error with a context message.
     *
     * @p format carries the call-site location. There is no separate
     * source_location parameter: a defaulted location placed before the
     * parameter pack would bind inside fail.
     *
     * @tparam E Subsystem enum.
     * @tparam Args Types checked against @p format.
     * @param code   Failure identity.
     * @param format Compile-time format string and call-site location.
     * @param args   Values substituted into @p format.
     * @return An unexpected error the caller can return from a Result function.
     * @pre errorMessage(code) points at static storage.
     * @post On success the error's message is the formatted text and its
     *       location is @p format's location.
     * @post If formatting fails for a reason other than allocation, the
     *       error is code-only.
     * @note std::bad_alloc while building the message calls panic.
     * @see viscera::Error::withContext
     */
    template <ErrorEnum E, typename ...Args>
    [[nodiscard]] std::unexpected<Error>
        fail(E code, FormatString<std::type_identity_t<Args> ...> format, Args && ...args) noexcept;

    template <ErrorEnum E>
    std::unexpected<Error> fail(const E code, const std::source_location where) noexcept
    {
        return std::unexpected<Error>{Error{code, where}};
    }

    template <ErrorEnum E, typename ...Args>
    std::unexpected<Error>
        fail(const E code, FormatString<std::type_identity_t<Args> ...> format, Args && ...args) noexcept
    {
        return std::unexpected<Error>{Error{code, std::move(format), std::forward<Args>(args) ...}};
    }
} // namespace viscera
