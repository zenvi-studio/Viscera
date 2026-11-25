#pragma once

#include <viscera/core/FormatString.hpp>

/**
 * @file
 * @brief Logging name for the shared format-string wrapper.
 *
 * Logger call sites keep using @c viscera::logging::FormatString. The type
 * lives in @c viscera::core so error headers can use it without including
 * a logging header.
 *
 * @see viscera::FormatString
 */

namespace viscera::logging
{
    /**
     * @brief Alias of viscera::FormatString used by the logger.
     *
     * @tparam Args Argument types the format string is checked against.
     * @see viscera::FormatString
     */
    template <typename ...Args>
    using FormatString = ::viscera::FormatString<Args ...>;
} // namespace viscera::logging
