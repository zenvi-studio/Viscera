#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace viscera::logging::detail
{
    inline constexpr std::size_t kPayloadBytes = 256;
    inline constexpr std::size_t kFatalBufferBytes = 1024;
    inline constexpr std::size_t kFormatBufferBytes = 4096;
    inline constexpr std::size_t kDefaultQueueCapacity = 8192;
    inline constexpr std::uint32_t kConsecutiveFailuresToDisable = 3;
    inline constexpr std::string_view kTruncationMarker = "...";
    inline constexpr std::string_view kNullToken = "(null)";
    inline constexpr std::uint8_t kFlagConsoleWritten = 1U;
} // namespace viscera::logging::detail
