#pragma once

#include <string_view>

#include "Sink.hpp"

/**
 * @file
 * @brief Debug-only sink that forwards lines to the platform debugger.
 *
 * Included from logging.hpp only when VISCERA_LOG_DEBUG_SINKS is on
 * (Debug and RelWithDebInfo by default). Not registered automatically;
 * the engine adds it if it wants debugger output.
 */

namespace viscera::logging
{
    /**
     * @brief Sink that writes the plain line to OutputDebugStringA on Windows.
     *
     * Copies the line plus a newline into a temporary std::string so the
     * debugger API receives a NUL-terminated buffer. Does not color.
     * Level is ignored. On non-Windows platforms write() is a no-op
     * that still returns @c true. flush() does nothing.
     */
    class DebuggerSink : public Sink
    {
        public:
            /**
             * @brief Forwards @p line to the debugger on Windows.
             *
             * @param level Unused; the line already contains the level name.
             * @param line  Already formatted text, without a newline.
             * @return Always @c true.
             */
            bool write(Level level, std::string_view line) override;

            /** No-op; OutputDebugString is not buffered by this sink. */
            void flush() override;

            /**
             * @brief Returns @c "debugger".
             */
            [[nodiscard]] std::string_view name() const override;
    };
} // namespace viscera::logging
