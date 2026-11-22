#pragma once

#include <cstdint>
#include <string_view>
#include <viscera/logging/MinLevel.hpp>

/**
 * @file
 * @brief Severity, category, and the compile-time floor helpers.
 *
 * Numeric Level values match VISCERA_LOG_MIN_LEVEL (see MinLevel.hpp).
 * The floor compiles convenience functions below it to empty; log() and
 * logAt() still compare at runtime via isBelowFloor(). The runtime min
 * level cannot go below that floor.
 */

namespace viscera::logging
{
    /**
     * @brief Ordered severity of a log record.
     *
     * A record is kept when its level is at least the effective minimum
     * (category override if set, otherwise the global min level). Fatal is
     * a severity: it does not abort or throw. Values are the integers used
     * by VISCERA_LOG_MIN_LEVEL.
     */
    enum class Level : std::uint8_t
    {
        Trace   = 0,
        Debug   = 1,
        Info    = 2,
        Warning = 3,
        Error   = 4,
        Fatal   = 5,
    };

    /**
     * @brief Subsystem a log record belongs to.
     *
     * Each record carries exactly one named bit. The remaining bits of the
     * uint64_t are reserved and stay disabled. Adding a subsystem is a new
     * enumerator here, not a string.
     */
    enum class Category : std::uint64_t
    {
        Core   = 1ULL << 0U,
        Render = 1ULL << 1U,
        Audio  = 1ULL << 2U,
        Asset  = 1ULL << 3U,
        Input  = 1ULL << 4U,
        Game   = 1ULL << 5U,
    };

    /**
     * @brief Bits of the six named categories (Core through Game).
     *
     * initialize() sets the enabled-category mask to this value, so every
     * named category starts on. Higher bits are not included and remain off.
     */
    inline constexpr std::uint64_t kNamedCategoryMask = 0x3F;

    /**
     * @brief True if @p level is strictly below the compile-time floor.
     *
     * Used by logAt() to return before capture, and by clampToFloor() so the
     * runtime min level cannot go below VISCERA_LOG_MIN_LEVEL. When the floor
     * is Trace (0), no level is below it and this always returns false.
     *
     * @param level Severity to compare.
     * @return @c true if logAt() should return without capturing.
     */
    [[nodiscard]] constexpr bool isBelowFloor(const Level level) noexcept
    {
        constexpr int kFloor = VISCERA_LOG_MIN_LEVEL;
        if constexpr (kFloor <= 0)
        {
            (void)level;
            return false;
        }
        else
        {
            return static_cast<int>(level) < kFloor;
        }
    }

    /**
     * @brief True if @p level is at or above the compile-time floor.
     *
     * Used as @c if constexpr in trace(), debug(), and the other helpers so
     * their bodies are empty when that severity is below the floor. log()
     * is not compiled out; it still calls isBelowFloor() at runtime.
     *
     * @param level Severity to compare.
     * @return @c true if that helper should call log().
     */
    [[nodiscard]] constexpr bool isAtLeastFloor(const Level level) noexcept
    {
        constexpr int kFloor = VISCERA_LOG_MIN_LEVEL;
        if constexpr (kFloor <= 0)
        {
            (void)level;
            return true;
        }
        else
        {
            return static_cast<int>(level) >= kFloor;
        }
    }

    /**
     * @brief Lowercase name printed in the @c [level] field of a log line.
     *
     * @param level Severity of the record.
     * @return A static name such as @c "info", or @c "unknown".
     */
    [[nodiscard]] constexpr std::string_view levelName(const Level level) noexcept
    {
        switch (level)
        {
            case Level::Trace:
                return "trace";
            case Level::Debug:
                return "debug";
            case Level::Info:
                return "info";
            case Level::Warning:
                return "warning";
            case Level::Error:
                return "error";
            case Level::Fatal:
                return "fatal";
        }

        return "unknown";
    }

    /**
     * @brief Lowercase name printed in the @c [category] field of a log line.
     *
     * @param category Subsystem of the record.
     * @return A static name such as @c "core", or @c "unknown".
     */
    [[nodiscard]] constexpr std::string_view categoryName(const Category category) noexcept
    {
        switch (category)
        {
            case Category::Core:
                return "core";
            case Category::Render:
                return "render";
            case Category::Audio:
                return "audio";
            case Category::Asset:
                return "asset";
            case Category::Input:
                return "input";
            case Category::Game:
                return "game";
        }

        return "unknown";
    }
} // namespace viscera::logging
