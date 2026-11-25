#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "ErrorDomain.hpp"

/**
 * @file
 * @brief Stable identity of a failure after the subsystem enum is erased.
 */

namespace viscera
{
    /**
     * @brief Domain plus numeric code.
     *
     * Callers compare this pair. There is no Ok value: success is the
     * expected side of Result. The size is not a contract; trivial
     * copyability and standard layout are.
     *
     * @invariant @c domain and @c value are the whole identity.
     */
    struct ErrorCode
    {
        /**
             * @brief Subsystem that owns @c value.
             */
        // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
        ErrorDomain domain{};

        /**
             * @brief Subsystem-local code. Real enumerators start at 1.
             */
        std::uint16_t value{};

        /**
             * @brief True when both fields are equal.
             *
             * @param other Code to compare.
             * @return @c true when domain and value both match.
             */
        [[nodiscard]] constexpr bool operator==(const ErrorCode &other) const noexcept = default;
    };

    static_assert(std::is_trivially_copyable_v<ErrorCode>);
    static_assert(std::is_standard_layout_v<ErrorCode>);
    static_assert(std::is_nothrow_copy_constructible_v<ErrorCode>);

    /**
     * @brief A subsystem enum that can be stored in an Error.
     *
     * @c errorDomain and @c errorMessage live in @p E's namespace so ADL
     * finds them. Both are @c constexpr and @c noexcept. @c errorMessage
     * returns a string literal or other static storage. The concept checks
     * the signature and @c noexcept; tests check that the messages are
     * @c constexpr.
     *
     * @tparam E Scoped enum, unsigned, no wider than @c uint16_t.
     */
    template <typename E>
    concept ErrorEnum = std::is_scoped_enum_v<E> && std::unsigned_integral<std::underlying_type_t<E>> &&
        sizeof(E) <= sizeof(std::uint16_t) && requires(const E code)
        {
            { errorDomain(code) } noexcept -> std::same_as<ErrorDomain>;
            { errorMessage(code) } noexcept -> std::same_as<std::string_view>;
        };
} // namespace viscera
