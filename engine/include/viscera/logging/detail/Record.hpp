#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <fmt/format.h>

#include "Constants.hpp"
#include "viscera/logging/Sink.hpp"
#include "viscera/logging/Types.hpp"

namespace viscera::logging::detail
{
    struct Record;

    using FormatThunk = void (*)(fmt::memory_buffer &out, const Record &record);
    using DestroyThunk = void (*)(Record &record);

    struct CapturedString
    {
        const char *data{};
        std::uint32_t size{};
        bool inlineStorage{false};
    };

    struct Record
    {
        enum class Kind : std::uint8_t
        {
            Empty,
            Log,
            AddSink,
        };

        Kind kind{Kind::Empty};
        Level level{};
        Category category{Category::Core};
        std::uint64_t timestampNs{};
        std::uint32_t threadId{};
        std::uint64_t frame{};
        const char *file{};
        const char *function{};
        std::uint32_t line{};
        std::string_view format;
        FormatThunk formatThunk{};
        DestroyThunk destroyThunk{};
        std::uint8_t flags{};
        Sink *controlSink{};
        alignas(std::max_align_t) std::array<std::byte, kPayloadBytes> payload{};
    };
} // namespace viscera::logging::detail
