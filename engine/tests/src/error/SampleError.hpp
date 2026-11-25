#pragma once

#include <cstdint>
#include <string_view>
#include <viscera/error/ErrorCode.hpp>
#include <viscera/error/ErrorDomain.hpp>

/**
 * @file
 * @brief Test-only error enum. Not part of the public umbrella.
 */

namespace viscera::error_tests
{
    enum class SampleError : std::uint16_t
    {
        Missing = 1,
        Broken  = 2,
    };

    [[nodiscard]] constexpr viscera::ErrorDomain errorDomain(const SampleError) noexcept
    {
        return viscera::ErrorDomain::Core;
    }

    [[nodiscard]] constexpr std::string_view errorMessage(const SampleError code) noexcept
    {
        switch (code)
        {
            case SampleError::Missing:
                return "missing thing";
            case SampleError::Broken:
                return "broken thing";
        }

        #ifdef __clang__
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wunreachable-code"
        #endif
        #if defined(_MSC_VER) && !defined(__clang__)
        #pragma warning(push)
        #pragma warning(disable : 4702)
        #endif
        return "unknown sample error";
        #if defined(_MSC_VER) && !defined(__clang__)
        #pragma warning(pop)
        #endif
        #ifdef __clang__
        #pragma clang diagnostic pop
        #endif
    }

    static_assert(viscera::ErrorEnum<SampleError>);
    static_assert(errorDomain(SampleError::Missing) == viscera::ErrorDomain::Core);
    static_assert(errorMessage(SampleError::Missing) == "missing thing");
    static_assert(errorMessage(SampleError::Broken) == "broken thing");
} // namespace viscera::error_tests
