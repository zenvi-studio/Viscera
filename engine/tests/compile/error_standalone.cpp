#include "error_standalone.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <viscera/error/error.hpp>

#include <fmt/format.h>

namespace viscera::error_standalone
{
    namespace
    {
        constexpr int kMoveOnlyValue = 7;
        constexpr int kIntValue = 42;

        // NOLINTNEXTLINE(performance-enum-size)
        enum class SampleError : std::uint16_t
        {
            Missing = 1,
        };

        struct MoveOnly
        {
            int value{kMoveOnlyValue};

            MoveOnly() = default;
            ~MoveOnly() = default;
            MoveOnly(const MoveOnly &) = delete;
            MoveOnly &operator=(const MoveOnly &) = delete;
            MoveOnly(MoveOnly &&) noexcept = default;
            MoveOnly &operator=(MoveOnly &&) noexcept = default;
        };

        [[nodiscard]] constexpr ErrorDomain errorDomain(const SampleError) noexcept
        {
            return ErrorDomain::Core;
        }

        [[nodiscard]] constexpr std::string_view errorMessage(const SampleError) noexcept
        {
            return "missing thing";
        }

        [[nodiscard]] Result<int> makeInt()
        {
            return kIntValue;
        }

        [[nodiscard]] Result<void> makeVoid()
        {
            return {};
        }

        [[nodiscard]] Result<MoveOnly> makeMoveOnly()
        {
            return MoveOnly{};
        }
    } // namespace

    bool exercise()
    {
        const Error error{SampleError::Missing};
        const std::string text = fmt::format("{}", error);
        if (!text.contains("[core] missing thing"))
        {
            return false;
        }

        const auto unexpected = fail(SampleError::Missing);
        if (unexpected.error().cause() != nullptr || !unexpected.error().is(SampleError::Missing))
        {
            return false;
        }

        const auto formatted = fail(SampleError::Missing, "id {}", kMoveOnlyValue);
        if (formatted.error().message() != fmt::format("id {}", kMoveOnlyValue))
        {
            return false;
        }

        const auto number = makeInt();
        const auto empty = makeVoid();
        const auto moved = makeMoveOnly();
        return *number == kIntValue && empty.has_value() && moved->value == kMoveOnlyValue;
    }
} // namespace viscera::error_standalone
