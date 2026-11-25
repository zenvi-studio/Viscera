#include <cstdint>
#include <string_view>
#include <viscera/error/error.hpp>

namespace
{
    // NOLINTNEXTLINE(performance-enum-size)
    enum class SampleError : std::uint16_t
    {
        Missing = 1,
    };

    [[nodiscard]] constexpr viscera::ErrorDomain errorDomain(const SampleError) noexcept
    {
        return viscera::ErrorDomain::Core;
    }

    [[nodiscard]] constexpr std::string_view errorMessage(const SampleError) noexcept
    {
        return "missing thing";
    }
} // namespace

void visceraErrorFormatMismatch()
{
    viscera::fail(SampleError::Missing, "{} {}", 1);
    const viscera::Error error{SampleError::Missing};
    static_cast<void>(error.withContext("{} {}", 1));
    static_cast<void>(error.wrap(SampleError::Missing, "{} {}", 1));
    static_cast<void>(viscera::Error{SampleError::Missing, "{} {}", 1});
}
