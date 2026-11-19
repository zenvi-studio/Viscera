#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <source_location>
#include <string_view>

#include <fmt/format.h>

#include "Constants.hpp"
#include "viscera/logging/Types.hpp"

namespace viscera::logging::detail
{
    [[nodiscard]] constexpr bool pathCharsEqual(const char left, const char right) noexcept
    {
        if (left == right)
        {
            return true;
        }

        const bool leftSlash = left == '/' || left == '\\';
        const bool rightSlash = right == '/' || right == '\\';
        return leftSlash && rightSlash;
    }

    [[nodiscard]] inline std::string_view shortFile(const char *const file, const std::string_view prefix) noexcept
    {
        if (file == nullptr)
        {
            return {};
        }

        const std::string_view path{file};
        if (prefix.empty() || path.size() < prefix.size())
        {
            return path;
        }

        for (std::size_t index = 0; index < prefix.size(); ++index)
        {
            if (!pathCharsEqual(path.at(index), prefix.at(index)))
            {
                return path;
            }
        }

        std::string_view rest = path.substr(prefix.size());
        while (!rest.empty() && (rest.front() == '/' || rest.front() == '\\'))
        {
            rest.remove_prefix(1);
        }

        return rest;
    }

    [[nodiscard]] inline double elapsedSeconds(const std::uint64_t timestampNs) noexcept
    {
        return static_cast<double>(timestampNs) / 1'000'000'000.0;
    }

    inline void applyTruncationMarker(char *const buffer, const std::size_t capacity, std::size_t &used) noexcept
    {
        if (capacity < kTruncationMarker.size())
        {
            used = capacity;
            return;
        }

        std::memcpy(buffer + capacity - kTruncationMarker.size(), kTruncationMarker.data(), kTruncationMarker.size());
        used = capacity;
    }

    template <typename ...Args>
    [[nodiscard]] std::size_t formatFullLine(
        char *const buffer,
        const std::size_t capacity,
        const Level level,
        const Category category,
        const std::source_location location,
        const std::uint64_t timestampNs,
        const std::uint32_t threadId,
        const std::uint64_t frame,
        const std::string_view sourceRoot,
        const fmt::string_view format,
        const Args & ...args
    ) noexcept
    {
        if (buffer == nullptr || capacity == 0)
        {
            return 0;
        }

        try
        {
            const auto file = shortFile(location.file_name(), sourceRoot);
            const auto prefix = fmt::format_to_n(
                buffer,
                capacity,
                "[{:.3f}][{}][{}][{}][{}][{}:{}] ",
                elapsedSeconds(timestampNs),
                frame,
                threadId,
                levelName(level),
                categoryName(category),
                file,
                location.line()
            );

            std::size_t used = std::min(prefix.size, capacity);
            bool truncated = prefix.size > capacity;

            if (used < capacity)
            {
                const auto message = fmt::format_to_n(buffer + used, capacity - used, fmt::runtime(format), args ...);
                truncated = truncated || message.size > (capacity - used);
                used += std::min(message.size, capacity - used);
            }

            if (truncated)
            {
                applyTruncationMarker(buffer, capacity, used);
            }

            return used;
        }
        catch (...)
        {
            const auto fallback =
                fmt::format_to_n(buffer, capacity, "[{}:{}] formatting failed", location.file_name(), location.line());
            return std::min(fallback.size, capacity);
        }
    }
} // namespace viscera::logging::detail
