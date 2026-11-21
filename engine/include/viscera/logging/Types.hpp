#pragma once

#include <cstdint>
#include <string_view>
#include <viscera/logging/MinLevel.hpp>

namespace viscera::logging
{
    enum class Level : std::uint8_t
    {
        Trace   = 0,
        Debug   = 1,
        Info    = 2,
        Warning = 3,
        Error   = 4,
        Fatal   = 5,
    };

    // Bitmask width is required by ADR-007 even though only six named bits exist today.
    enum class Category : std::uint64_t
    {
        Core   = 1ULL << 0U,
        Render = 1ULL << 1U,
        Audio  = 1ULL << 2U,
        Asset  = 1ULL << 3U,
        Input  = 1ULL << 4U,
        Game   = 1ULL << 5U,
    };

    inline constexpr std::uint64_t kNamedCategoryMask = 0x3F;

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
