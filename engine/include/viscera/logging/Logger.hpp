#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "FormatString.hpp"
#include "Sink.hpp"
#include "Types.hpp"
#include "detail/Capture.hpp"
#include "detail/Constants.hpp"
#include "detail/Line.hpp"
#include "detail/Runtime.hpp"

namespace viscera::logging
{
    struct Config
    {
        std::size_t queueCapacity{detail::kDefaultQueueCapacity};
        std::chrono::milliseconds flushTimeout{std::chrono::seconds{1}};
        std::vector<std::unique_ptr<Sink>> sinks;
        std::optional<std::filesystem::path> filePath;
        std::string sourceRootPrefix;
    };

    struct Counters
    {
        std::uint64_t overflow{};
        std::uint64_t argumentBudget{};
        std::uint64_t notRunning{};
        std::uint64_t formattingFailures{};
        std::uint64_t flushTimeouts{};
        std::vector<std::uint64_t> sinkWriteFailures;
    };

    [[nodiscard]] bool initialize(Config config);
    void shutdown() noexcept;
    void flush() noexcept;

    void setMinLevel(Level level) noexcept;
    void setCategoryEnabled(Category category, bool enabled) noexcept;
    void setCategoryMinLevel(Category category, Level level) noexcept;
    void clearCategoryMinLevel(Category category) noexcept;
    void setFrame(std::uint64_t frame) noexcept;
    void addSink(std::unique_ptr<Sink> sink) noexcept;

    [[nodiscard]] Counters counters() noexcept;

    template <typename ...Args>
    void logAt(
        const Level level,
        const Category category,
        const std::source_location location,
        const fmt::format_string<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if (isBelowFloor(level))
        {
            return;
        }

        const bool isFatal = level == Level::Fatal;
        std::uint64_t timestampNs = 0;
        std::uint32_t threadId = 0;
        std::uint64_t frame = 0;
        bool consoleWritten = false;

        if (isFatal)
        {
            timestampNs = detail::sampleTimestampNs();
            threadId = detail::sampleThreadId();
            frame = detail::sampleFrame();
            std::array<char, detail::kFatalBufferBytes> buffer{};
            const std::size_t used = detail::formatFullLine(
                buffer.data(),
                buffer.size(),
                level,
                category,
                location,
                timestampNs,
                threadId,
                frame,
                detail::sourceRootPrefix(),
                format.get(),
                args ...
            );
            consoleWritten = detail::tryWriteFatalConsole(std::string_view{buffer.data(), used});
        }

        if (!detail::loggerRunning())
        {
            detail::countNotRunning();
            return;
        }

        if (!detail::shouldLog(level, category))
        {
            return;
        }

        if (!isFatal)
        {
            timestampNs = detail::sampleTimestampNs();
            threadId = detail::sampleThreadId();
            frame = detail::sampleFrame();
        }

        detail::enqueueLog(
            level,
            category,
            location,
            format,
            consoleWritten,
            timestampNs,
            threadId,
            frame,
            std::forward<Args>(args) ...
        );
    }

    template <typename ...Args>
    void
        log(
            const Level level,
            const Category category,
            const FormatString<std::type_identity_t<Args> ...> format,
            Args && ...args
        ) noexcept
    {
        logAt(level, category, format.location, format.format, std::forward<Args>(args) ...);
    }

    template <typename ...Args>
    void trace(
        const Category category,
        const FormatString<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Trace))
        {
            log(Level::Trace, category, format, std::forward<Args>(args) ...);
        }
    }

    template <typename ...Args>
    void debug(
        const Category category,
        const FormatString<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Debug))
        {
            log(Level::Debug, category, format, std::forward<Args>(args) ...);
        }
    }

    template <typename ...Args>
    void
        info(
            const Category category,
            const FormatString<std::type_identity_t<Args> ...> format,
            Args && ...args
        ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Info))
        {
            log(Level::Info, category, format, std::forward<Args>(args) ...);
        }
    }

    template <typename ...Args>
    void warning(
        const Category category,
        const FormatString<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Warning))
        {
            log(Level::Warning, category, format, std::forward<Args>(args) ...);
        }
    }

    template <typename ...Args>
    void error(
        const Category category,
        const FormatString<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Error))
        {
            log(Level::Error, category, format, std::forward<Args>(args) ...);
        }
    }

    template <typename ...Args>
    void fatal(
        const Category category,
        const FormatString<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Fatal))
        {
            log(Level::Fatal, category, format, std::forward<Args>(args) ...);
        }
    }
} // namespace viscera::logging
