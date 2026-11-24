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

/**
 * @file
 * @brief Formatting helpers for a finished log line.
 *
 * shortFile() and elapsedSeconds() are shared by the Fatal stack path
 * (formatFullLine) and the backend. formatFullLine() itself is the bounded,
 * no-allocation formatter used only for the direct Fatal console write;
 * the backend formats the same pattern into a growable buffer.
 */

namespace viscera::logging::detail
{
    /**
     * @brief Returns whether two path characters match, treating @c '/' and
     *        @c '\\' as the same separator.
     *
     * Used by shortFile() so a source-root prefix configured with one slash
     * style still matches paths captured with the other.
     *
     * @param left  Character from the captured file path.
     * @param right Character from the configured prefix.
     * @return @c true if the characters are equal, or both are slashes.
     */
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

    /**
     * @brief Strips a configured source-root prefix from a captured file path.
     *
     * Comparison is slash-insensitive via pathCharsEqual(). If @p file is
     * null, returns an empty view. If @p prefix is empty, longer than the
     * path, or does not match, returns the original path. After a match,
     * leading slashes on the remainder are removed so the line shows a
     * relative path. The result is a view into @p file; nothing is copied.
     *
     * Used by formatFullLine() and by the backend when it prints
     * @c [file:line].
     *
     * @param file   NUL-terminated path from std::source_location, or null.
     * @param prefix Config::sourceRootPrefix, may be empty.
     * @return The path to print, possibly shortened.
     */
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

    /**
     * @brief Converts a producer timestamp in nanoseconds to seconds.
     *
     * The line pattern prints this with millisecond precision (@c {:.3f}).
     * Used by formatFullLine() and by the backend for the @c [elapsed] field.
     *
     * @param timestampNs Nanoseconds from sampleTimestampNs().
     * @return Elapsed seconds as a @c double.
     */
    [[nodiscard]] inline double elapsedSeconds(const std::uint64_t timestampNs) noexcept
    {
        return static_cast<double>(timestampNs) / 1'000'000'000.0;
    }

    /**
     * @brief Overwrites the last bytes of @p buffer with kTruncationMarker.
     *
     * Writes at the end of the capacity, then sets @p used to @p capacity so
     * the caller treats the buffer as full. If @p capacity is smaller than
     * the marker, @p used is set to @p capacity and the marker is not written.
     *
     * Called by formatFullLine() when the Fatal stack line does not fit.
     *
     * @param buffer   Destination of the truncated line.
     * @param capacity Size of @p buffer in bytes.
     * @param used     Byte count to report; always set to @p capacity.
     */
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

    /**
     * @brief Formats a complete log line into a caller-provided buffer.
     *
     * Used by logAt() for the synchronous Fatal console write. The pattern
     * matches the backend:
     * @c [elapsed][frame][thread][level][category][file:line] message
     *
     * Formats the header, then the message into whatever space remains.
     * If either part exceeds @p capacity, the last bytes are replaced with
     * kTruncationMarker. On a formatting exception, writes
     * @c [file:line] formatting failed using the raw file name (not
     * shortFile) and no user arguments.
     *
     * Never throws. Returns 0 if @p buffer is null or @p capacity is 0.
     *
     * @tparam Args Types of the format arguments (the original call-site
     *              values, not the captured payload types).
     * @param buffer      Destination; typically kFatalBufferBytes on the stack.
     * @param capacity    Size of @p buffer in bytes.
     * @param level       Severity printed in the header.
     * @param category    Subsystem printed in the header.
     * @param location    Call site; file is shortened with @p sourceRoot.
     * @param timestampNs Producer timestamp already sampled by the caller.
     * @param threadId    Producer thread id already sampled by the caller.
     * @param frame       Frame number already sampled by the caller.
     * @param sourceRoot  Prefix stripped by shortFile(); may be empty.
     * @param format      Runtime format string for the message body.
     * @param args        Values substituted into @p format.
     * @return Number of bytes written to @p buffer, not including a terminator.
     */
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
            const auto [out, size] = fmt::format_to_n(
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

            std::size_t used = std::min(size, capacity);
            bool truncated = size > capacity;

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
