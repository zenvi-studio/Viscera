#pragma once

#include <array>
#include <string_view>

#include <fmt/color.h>

#include "Types.hpp"

/**
 * @file
 * @brief Level-to-SGR color used only by ConsoleSink.
 *
 * The backend formats a plain line. ConsoleSink wraps that line in a prefix
 * and sgrReset() when the stream is a terminal. File, debugger, and memory
 * sinks write the line unchanged. The map is fixed:
 * Trace bright black, Debug green, Info blue, Warning yellow, Error red,
 * Fatal bright red. Unknown levels use white.
 */

namespace viscera::logging
{
    /**
     * @brief Terminal color for @p level.
     *
     * Same mapping sgrPrefix() encodes as an SGR sequence. Unknown levels
     * return white.
     *
     * @param level Severity of the line being presented.
     * @return The corresponding @c fmt::terminal_color.
     */
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
        /**
         * @brief Compile-time ESC[NNm sequence for a two-digit SGR parameter.
         *
         * @p Code is the numeric value of a @c fmt::terminal_color (30–37 or
         * 90–97). kValue is a NUL-terminated array; sgrPrefix() returns a
         * view of the five visible characters.
         *
         * @tparam Code Two-digit SGR color code.
         */
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

    /**
     * @brief CSI sequence that selects the color for @p level.
     *
     * Encodes getLevelColor(@p level) as ESC[NNm. ConsoleSink writes
     * this, then the plain line, then sgrReset(), as three separate writes
     * so it does not allocate a colored copy of the line.
     *
     * @param level Severity of the line being presented.
     * @return A view of a static SGR prefix; never empty.
     */
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

    /**
     * @brief CSI sequence that restores default terminal attributes.
     *
     * @return ESC[0m.
     */
    [[nodiscard]] constexpr std::string_view sgrReset() noexcept
    {
        return "\033[0m";
    }

    /**
     * @brief Writes @p line, wrapping it in SGR codes when @p isTerminal is true.
     *
     * On a terminal: @p write is called three times (prefix, line, reset).
     * Otherwise it is called once with the plain line. Does not append a
     * newline; ConsoleSink does that after this returns.
     *
     * @tparam Write Callable taking std::string_view.
     * @param level      Selects the prefix color.
     * @param line       Already formatted log text, without a newline.
     * @param isTerminal @c true to emit SGR; @c false for pipes and redirects.
     * @param write      Destination; typically fwrite of one span.
     */
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
