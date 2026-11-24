#pragma once

#include <source_location>

#include <fmt/format.h>

/**
 * @file
 * @brief Format-string wrapper that also captures the log call site.
 *
 * log(), trace(), and the other helpers take this as one parameter so a
 * string literal converts at the caller's expression. logAt() takes an
 * explicit location instead, for wrappers that log on someone else's behalf.
 */

namespace viscera::logging
{
    /**
     * @brief A compile-time-checked format string that also records where it
     *        was written.
     *
     * Bundles a @c fmt::format_string with the @c std::source_location of the
     * call site. It exists so that log functions can take a single parameter
     * and get both:
     *  - **compile-time validation** of the format string against the argument
     *    types (a bad format string or wrong argument count fails to compile),
     *  - **automatic call-site capture** (file, line, function) without macros.
     *
     * It is meant to be created implicitly from a string literal at the call:
     * @code
     * log(Level::Info, Category::Core, "loaded {} assets", count);
     * //                               ^ converts to FormatString<int&>
     * @endcode
     *
     * In the log functions the parameter is declared as
     * @c FormatString<std::type_identity_t<Args>...>, which stops @p Args from
     * being deduced from the format string, so it is deduced from the
     * trailing arguments only.
     *
     * @tparam Args Argument types the format string is checked against.
     */
    template <typename ...Args>
    struct FormatString
    {
        /**
         * The validated format string.
         */
        fmt::format_string<Args ...> format;

        /**
         * Where the format string was written (the log call site).
         */
        std::source_location location;

        /**
         * @brief Constructs from a format string and captures the call site.
         *
         * The constructor is @c consteval, so @p s must be a constant
         * expression (typically a string literal); this is what lets fmt
         * validate it during compilation. A runtime string will not compile;
         * wrap it explicitly, e.g. @c "{}" with the string as an argument.
         *
         * The constructor is intentionally not @c explicit so string literals
         * convert implicitly at log call sites (hence the NOLINT).
         *
         * @tparam S Any type @c fmt::format_string can be built from, such as
         *           a string literal.
         * @param s   The format string.
         * @param loc Defaults to @c std::source_location::current(), which
         *            evaluates at the caller's expression and so records the
         *            log call site rather than this header. Normally left as
         *            the default.
         */
        template <typename S>
        // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor, misc-explicit-constructor)
        consteval FormatString(const S &s, const std::source_location loc = std::source_location::current()) :
            format(s),
            location(loc) {}
    };
} // namespace viscera::logging
