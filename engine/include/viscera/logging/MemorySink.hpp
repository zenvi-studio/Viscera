#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "Sink.hpp"

/**
 * @file
 * @brief In-memory sink for tests and benchmarks.
 */

namespace viscera::logging
{
    /**
     * @brief Sink that stores a copy of each line for later inspection.
     *
     * Used by tests and LoggingBench. write() copies @p line as-is (no
     * newline, no color) and always returns true. lines() and flushCount()
     * are safe to call while the backend may still be writing; they take
     * the same mutex. flush() only increments flushCount() — it does not
     * gate visibility of lines().
     */
    class MemorySink : public Sink
    {
        public:
            /**
             * @brief Appends a copy of @p line to the stored list.
             *
             * @param level Unused; the line already contains the level name.
             * @param line  Already formatted text, without a newline.
             * @return Always @c true.
             */
            bool write(Level level, std::string_view line) override;

            /**
             * @brief Increments flushCount(). Does not clear or publish lines.
             */
            void flush() override;

            /**
             * @brief Returns @c "memory".
             */
            [[nodiscard]] std::string_view name() const override;

            /**
             * @brief Returns a snapshot of lines received so far.
             *
             * Tests typically call flush() on the logger first so the backend
             * has drained into this sink.
             */
            [[nodiscard]] std::vector<std::string> lines() const;

            /**
             * @brief Number of times flush() has been called on this sink.
             */
            [[nodiscard]] std::uint64_t flushCount() const;

        private:
            mutable std::mutex m_mutex;
            std::vector<std::string> m_lines;
            std::uint64_t m_flushCount{0};
    };
} // namespace viscera::logging
