#pragma once

#include <atomic>
#include <string_view>

#include "Types.hpp"

/**
 * @file
 * @brief Abstract destination for an already formatted log line.
 */

namespace viscera::logging
{
    /**
     * @brief Destination that receives one formatted line from the backend.
     *
     * Sinks do not format. The line is the shared plain-text pattern; level
     * is for presentation (for example ConsoleSink color) and is not a second
     * filter. The backend thread is the only caller of write() and flush()
     * during normal logging. ConsoleSink::tryWriteFatal() is the Fatal
     * exception and uses the console mutex, not this interface.
     *
     * Returning false or throwing is a failure of this sink only: the backend
     * counts it and disables the sink after three consecutive failures. A
     * sink must not call back into the logger. setEnabled() is safe from any
     * thread; a disabled sink stays registered and is skipped until
     * setEnabled(true).
     */
    class Sink
    {
        public:
            /**
             * Sinks may be destroyed on the backend thread (remove/abandon)
             * or on the thread that called shutdown(). Must not log.
             */
            virtual ~Sink() = default;

            /**
             * @brief Writes one already formatted line.
             *
             * @param level Severity of the record; for presentation, not filtering.
             * @param line  Plain log text, without a trailing newline. The view
             *              is valid only for this call; copy if it must outlive it.
             * @return @c true on success; @c false (or a throw) is a sink failure.
             */
            virtual bool write(Level level, std::string_view line) = 0;

            /**
             * @brief Pushes buffered bytes to the underlying destination.
             *
             * The backend catches throws. Does not format or write a new line.
             */
            virtual void flush() = 0;

            /**
             * @brief Stable name used in logger diagnostics (sink disabled, counters).
             *
             * @return A view with static lifetime, such as @c "console".
             */
            [[nodiscard]] virtual std::string_view name() const = 0;

            /**
             * @brief Enables or disables this sink.
             *
             * Safe from any thread. The backend checks enabled() before write
             * and flush. A disabled sink is not retried until this is true.
             *
             * @param isEnabled @c false to skip this sink.
             */
            void setEnabled(const bool isEnabled) noexcept { m_enabled.store(isEnabled, std::memory_order_relaxed); }

            /**
             * @brief Returns whether the backend should offer lines to this sink.
             *
             * Defaults to true. initialize() starts a FileSink disabled when
             * the file could not be opened.
             */
            [[nodiscard]] bool enabled() const noexcept { return m_enabled.load(std::memory_order_relaxed); }

        private:
            std::atomic<bool> m_enabled{true};
    };
} // namespace viscera::logging
