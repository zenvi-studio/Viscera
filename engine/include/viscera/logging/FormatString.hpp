#pragma once

#include <source_location>

#include <fmt/format.h>

namespace viscera::logging
{
    template <typename ...Args>
    struct FormatString
    {
        fmt::format_string<Args ...> format;
        std::source_location location;

        template <typename S>
        // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor, misc-explicit-constructor)
        consteval FormatString(const S &s, const std::source_location loc = std::source_location::current()) :
            format(s),
            location(loc) {}
    };
} // namespace viscera::logging
