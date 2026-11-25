#pragma once

#include <source_location>

#include <fmt/format.h>

/**
 * @file
 * @brief Compile-time format string that also records the call site.
 *
 * Logging and error handling both take this as one parameter so a string
 * literal is checked against the argument types and the caller's source
 * location is captured without a macro. This header depends only on {fmt}
 * and the standard library. It does not include logging or error.
 */

namespace viscera
{
    /**
     * @brief A compile-time-checked format string and the location where it
     *        was written.
     *
     * Bundles a @c fmt::format_string with the @c std::source_location of the
     * call site. Callers get both:
     *  - compile-time validation of the format string against the argument
     *    types (a bad format string or the wrong argument count fails to
     *    compile),
     *  - automatic call-site capture (file, line, function) without macros.
     *
     * It is meant to be created implicitly from a string literal at the call:
     * @code
     * log(Level::Info, Category::Core, "loaded {} assets", count);
     * fail(IoError::NotFound, "open '{}' failed", path);
     * @endcode
     *
     * Declare the parameter as
     * @c FormatString<std::type_identity_t<Args>...> so @p Args is deduced
     * from the trailing arguments only, not from the format string.
     *
     * @tparam Args Argument types the format string is checked against.
     * @see viscera::logging::FormatString
     */
    template <typename ...Args>
    struct FormatString
    {
        /**
         * @brief The validated format string.
         */
        fmt::format_string<Args ...> format;

        /**
         * @brief Where the format string was written.
         *
         * For a string literal converted at the call, this is the call site,
         * not this header.
         */
        std::source_location location;

        /**
         * @brief Constructs from a format string and captures the call site.
         *
         * The constructor is @c consteval, so @p text must be a constant
         * expression (typically a string literal). That is what lets {fmt}
         * validate it during compilation. A runtime string will not compile;
         * pass @c "{}" and the runtime text as an argument.
         *
         * The constructor is intentionally not @c explicit so string literals
         * convert implicitly at call sites.
         *
         * @tparam S Any type @c fmt::format_string can be built from, such as
         *           a string literal.
         * @param text The format string.
         * @param loc  Defaults to @c std::source_location::current(), which
         *             evaluates at the caller's expression. Normally left as
         *             the default.
         * @pre @p text is a constant expression accepted by
         *      @c fmt::format_string<Args...>.
         * @post @c format holds that string and @c location is @p loc.
         */
        template <typename S>
        // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor, misc-explicit-constructor)
        consteval FormatString(const S &text, const std::source_location loc = std::source_location::current()) :
            format(text),
            location(loc) {}
    };
} // namespace viscera
