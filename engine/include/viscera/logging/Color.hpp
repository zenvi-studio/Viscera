#pragma once

#include <array>
#include <string_view>

#include <fmt/color.h>

#include "Types.hpp"

namespace viscera::logging
{
    [[nodiscard]] constexpr fmt::terminal_color getLevelColor(const Level level) noexcept
    {
        switch (level)
        {
            case Level::Trace:
                return fmt::terminal_color::bright_black;
            case Level::Debug:
                return fmt::terminal_color::green;
            case Level::Info:
                return fmt::terminal_color::blue;
            case Level::Warning:
                return fmt::terminal_color::yellow;
            case Level::Error:
                return fmt::terminal_color::red;
            case Level::Fatal:
                return fmt::terminal_color::bright_red;
        }

        return fmt::terminal_color::white;
    }

    namespace detail
    {
        template <int Code>
        struct SgrCode
        {
            static constexpr std::array<char, 6> kValue{
                '\033',
                '[',
                static_cast<char>('0' + (Code / 10)),
                static_cast<char>('0' + (Code % 10)),
                'm',
                '\0',
            };
        };
    } // namespace detail

    [[nodiscard]] constexpr std::string_view sgrPrefix(const Level level) noexcept
    {
        switch (level)
        {
            case Level::Trace:
                return detail::SgrCode<static_cast<int>(fmt::terminal_color::bright_black)>::kValue.data();
            case Level::Debug:
                return detail::SgrCode<static_cast<int>(fmt::terminal_color::green)>::kValue.data();
            case Level::Info:
                return detail::SgrCode<static_cast<int>(fmt::terminal_color::blue)>::kValue.data();
            case Level::Warning:
                return detail::SgrCode<static_cast<int>(fmt::terminal_color::yellow)>::kValue.data();
            case Level::Error:
                return detail::SgrCode<static_cast<int>(fmt::terminal_color::red)>::kValue.data();
            case Level::Fatal:
                return detail::SgrCode<static_cast<int>(fmt::terminal_color::bright_red)>::kValue.data();
        }

        return detail::SgrCode<static_cast<int>(fmt::terminal_color::white)>::kValue.data();
    }

    [[nodiscard]] constexpr std::string_view sgrReset() noexcept
    {
        return "\033[0m";
    }

    template <typename Write>
    void writeMaybeColored(const Level level, const std::string_view line, const bool isTerminal, Write write)
    {
        if (isTerminal)
        {
            write(sgrPrefix(level));
            write(line);
            write(sgrReset());
            return;
        }

        write(line);
    }
} // namespace viscera::logging
