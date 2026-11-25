#pragma once

#include <cstdint>
#include <string_view>

/**
 * @file
 * @brief Closed list of subsystems that own error codes.
 *
 * v1 has one enumerator, Core. Platform, Io, Asset, Render, Audio, Input,
 * and Game are reserved names. They are not enumerators until the change
 * that adds that owner's first error enum.
 */

namespace viscera
{
    /**
     * @brief Which subsystem owns an error code.
     *
     * Values are explicit and append-only. A removed enumerator is not
     * reused. @c Core is 1 so a zero-initialized code is not a real domain.
     */
    enum class ErrorDomain : std::uint16_t
    {
        Core = 1,
    };

    /**
     * @brief Lowercase name of @p domain, for the formatter's brackets.
     *
     * The switch has no default arm, so a new enumerator fails the build
     * until this function grows an arm. A value that is not an enumerator
     * returns @c "unknown".
     *
     * @param domain Domain to name.
     * @return A @c string_view with static lifetime. Never empty.
     * @post The returned view remains valid for the rest of the program.
     */
    [[nodiscard]] constexpr std::string_view domainName(const ErrorDomain domain) noexcept
    {
        // NOLINTNEXTLINE(readability-trivial-switch)
        switch (domain)
        {
            case ErrorDomain::Core:
                return "core";
        }

        #ifdef __clang__
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wunreachable-code"
        #endif
        #if defined(_MSC_VER) && !defined(__clang__)
        #pragma warning(push)
        #pragma warning(disable : 4702)
        #endif
        return "unknown";
        #if defined(_MSC_VER) && !defined(__clang__)
        #pragma warning(pop)
        #endif
        #ifdef __clang__
        #pragma clang diagnostic pop
        #endif
    }
} // namespace viscera
