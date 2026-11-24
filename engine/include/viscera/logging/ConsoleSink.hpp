#pragma once

#include <cstdio>
#include <mutex>
#include <string_view>

#include "Sink.hpp"

/**
 * @file
 * @brief Sink that writes a formatted line to a console FILE, usually stderr.
 */

namespace viscera::logging
{
    /**
     * @brief Sink that writes a formatted line to a console stream.
     *
     * Colors the whole line by Level when the stream is a terminal and
     * virtual terminal processing is available (see Color.hpp). A pipe or
     * redirect gets the plain line. Appends a newline. Does not format the
     * record; the backend or the Fatal stack path already did that.
     *
     * write() and flush() take m_mutex. tryWriteFatal() try_locks the same
     * mutex so a Fatal producer can share the stream with the backend
     * without waiting, and without holding this mutex while taking the
     * ring mutex. The logger remembers the ConsoleSink via dynamic_cast
     * so Fatal can find it.
     *
     * Does not own @c m_file and does not close it.
     */
    class ConsoleSink : public Sink
    {
        public:
            /**
             * @brief Optional isatty override, used by tests.
             *
             * @c nullptr (the default) uses the platform isatty on @c m_file.
             */
            using IsTerminal = bool (*)(std::FILE *);

            /** Writes to stderr, detecting a terminal with isatty. */
            ConsoleSink();

            /**
             * @brief Writes to @p file (stderr if @p file is null).
             *
             * Enables virtual terminal processing on Windows for stdout and
             * stderr so SGR colors are honored. If that fails, lines stay
             * plain even when the stream is a terminal.
             *
             * @param file       Stream to write; not owned.
             * @param isTerminal Test hook; @c nullptr uses isatty.
             */
            explicit ConsoleSink(std::FILE *file, IsTerminal isTerminal = nullptr);

            ConsoleSink(const ConsoleSink &) = delete;
            ConsoleSink &operator=(const ConsoleSink &) = delete;
            ConsoleSink(ConsoleSink &&) = delete;
            ConsoleSink &operator=(ConsoleSink &&) = delete;

            ~ConsoleSink() override = default;

            /**
             * @brief Locks the console mutex and writes @p line plus a newline.
             *
             * @p level selects the color only; the text of @p line is not parsed.
             *
             * @param level Severity used for SGR coloring.
             * @param line  Already formatted text, without a newline.
             * @return @c true if every fwrite (and the newline) succeeded.
             */
            bool write(Level level, std::string_view line) override;

            /**
             * @brief fflush the stream while holding the console mutex.
             */
            void flush() override;

            /**
             * @brief Returns @c "console".
             */
            [[nodiscard]] std::string_view name() const override;

            /**
             * @brief Best-effort Fatal write used by tryWriteFatalConsole().
             *
             * try_locks m_mutex so a Fatal that races the backend skips
             * rather than waiting. Writes at Level::Fatal, then unlocks.
             *
             * @param line Already formatted Fatal text, without a newline.
             * @return @c true if the lock was taken and the write succeeded.
             */
            [[nodiscard]] bool tryWriteFatal(std::string_view line) noexcept;

        private:
            /**
             * @brief True when the stream is a terminal and VT is enabled.
             */
            [[nodiscard]] bool shouldColor() const noexcept;

            /**
             * @brief Writes color (if any), @p line, and a newline. Caller holds m_mutex.
             */
            [[nodiscard]] bool writeLocked(Level level, std::string_view line) noexcept;

            /**
             * @brief On Windows, sets ENABLE_VIRTUAL_TERMINAL_PROCESSING for stdout/stderr.
             *
             * Non-Windows, and Windows FILE* values other than stdout/stderr,
             * leave m_vtEnabled true. Failure leaves it false so writeLocked
             * emits a plain line.
             */
            void enableVirtualTerminal() noexcept;

            std::FILE *m_file{stderr};
            IsTerminal m_isTerminal{nullptr};
            bool m_vtEnabled{true};
            std::mutex m_mutex;
    };
} // namespace viscera::logging
