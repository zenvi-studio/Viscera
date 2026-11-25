#pragma once

#include <source_location>

/**
 * @file
 * @brief Abort path for broken invariants and allocation failure.
 *
 * panic writes a bounded diagnostic to the stderr descriptor and aborts.
 * It does not use the logger, iostreams, or a replaceable handler.
 */

namespace viscera
{
    /**
     * @brief Writes a diagnostic to stderr and aborts.
     *
     * The line is formatted into a 1024-byte stack buffer. If it does not
     * fit, the last bytes are @c "...\\n". The write uses the stderr
     * descriptor (@c write or @c _write) and does not allocate. A short
     * write is ignored. @c std::abort follows either way.
     *
     * @param message Text to include in the diagnostic.
     * @param where   Location printed after the message. The default is the
     *                caller of panic, not this header.
     * @pre @p message points at a null-terminated string.
     * @post This function does not return. The process is aborted.
     * @note A null @p message, or any exception while composing the line,
     *       still aborts. The fallback line includes the file and line and
     *       omits the caller text. That path is not a replaceable handler.
     * @warning Does not unwind, does not call the logger, and does not flush.
     *          Output is not a log record.
     */
    [[noreturn]] void panic(const char *message, std::source_location where = std::source_location::current()) noexcept;
} // namespace viscera
