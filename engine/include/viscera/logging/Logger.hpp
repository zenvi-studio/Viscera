#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "FormatString.hpp"
#include "Sink.hpp"
#include "Types.hpp"
#include "detail/Capture.hpp"
#include "detail/Constants.hpp"
#include "detail/Line.hpp"
#include "detail/Runtime.hpp"

/**
 * @file
 * @brief Public API of the viscera logging subsystem.
 *
 * Call sites include logging.hpp. There is one process-wide logger:
 * initialize() from the main thread before other threads log, and join
 * those threads before shutdown(). Log calls are cheap on the calling
 * thread: they filter, capture a timestamp, thread id and frame number,
 * and enqueue the formatted-later record. Fatal messages also format into
 * a stack buffer and try_lock the console so a line can appear even if the
 * queue is lost. Argument expressions are always evaluated, including on
 * disabled or compiled-out helpers.
 *
 * Typical usage:
 * @code
 * viscera::logging::Config config;
 * config.sinks.push_back(std::make_unique<MySink>());
 * if (!viscera::logging::initialize(std::move(config))) { return 1; }
 *
 * viscera::logging::info(Category::Core, "started in {} ms", elapsedMs);
 *
 * viscera::logging::shutdown();
 * @endcode
 */

namespace viscera::logging
{
    /**
     * @brief Settings passed to initialize().
     *
     * All members have sensible defaults, so a default-constructed Config is
     * valid. The Config owns its sinks; they are moved into the logger by
     * initialize().
     */
    struct Config
    {
        /**
         * Ring capacity in slots. initialize() rounds this up to a power of
         * two. Default is kDefaultQueueCapacity (8192). Tests pass a small
         * ring.
         */
        std::size_t queueCapacity{detail::kDefaultQueueCapacity};

        /**
         * How long flush() and shutdown() wait for queued records to be
         * written before abandoning unstarted ones (counted in
         * Counters::flushTimeouts).
         */
        std::chrono::milliseconds flushTimeout{std::chrono::seconds{1}};

        /** Initial destinations; ownership is transferred to the logger. */
        std::vector<std::unique_ptr<Sink>> sinks;

        /**
         * If set, initialize() opens a FileSink at this path (append) in
         * addition to sinks. A failed open disables that sink and does not
         * fail initialize().
         */
        std::optional<std::filesystem::path> filePath;

        /**
         * Prefix stripped from source file paths in log output so lines show
         * a path relative to the source root. Empty leaves paths unchanged.
         */
        std::string sourceRootPrefix;
    };

    /**
     * @brief Snapshot of the logger's diagnostic counters.
     *
     * Each counter records a way in which a log call did not result in a
     * delivered record. Useful for tests and for surfacing logging health in
     * debug overlays.
     */
    struct Counters
    {
        /** Records dropped because the queue was full. */
        std::uint64_t overflow{};

        /**
         * Records dropped because their arguments exceeded the per-record
         * argument budget.
         */
        std::uint64_t argumentBudget{};

        /**
         * Log calls made while the logger was not running (before
         * initialize() or after shutdown()).
         */
        std::uint64_t notRunning{};

        /** Backend format thunk threw, or growing the format buffer failed. */
        std::uint64_t formattingFailures{};

        /** flush() or shutdown() timed out before the ring drained. */
        std::uint64_t flushTimeouts{};

        /**
         * Write failures per sink, in registration order: filePath first (if
         * set), then Config::sinks, then addSink(). A failed file open counts
         * as one.
         */
        std::vector<std::uint64_t> sinkWriteFailures;
    };

    /**
     * @brief Starts the process-wide logger.
     *
     * Call once from the main thread before other threads log. Consumes
     * @p config: builds the ring, registers filePath (if any) then sinks,
     * and starts the backend thread. A second call without shutdown() is a
     * programmer error: debug assert, return false, leave the running
     * logger alone.
     *
     * Log calls before a successful initialize() are counted in
     * Counters::notRunning. Returns false if the backend thread fails to
     * start. A FileSink that cannot be opened does not fail this call.
     *
     * @param config Logger settings; consumed by the call.
     * @return @c true if the backend started.
     */
    [[nodiscard]] bool initialize(Config config);

    /**
     * @brief Stops the logger: drain (or abandon on timeout), join, destroy.
     *
     * Other threads must have stopped logging first. Idempotent. Log calls
     * afterward are counted in Counters::notRunning. A timeout increments
     * Counters::flushTimeouts, abandons unstarted records, and still waits
     * for an in-flight sink write.
     */
    void shutdown() noexcept;

    /**
     * @brief Blocks until the ring is empty and sinks are flushed, or
     *        Config::flushTimeout elapses.
     *
     * A timeout increments Counters::flushTimeouts and abandons unstarted
     * records. An in-flight write is not interrupted. No-op if the logger
     * is not running.
     */
    void flush() noexcept;

    /**
     * @brief Sets the global minimum level.
     *
     * Records below @p level are discarded, regardless of category, unless
     * a category override is set. Clamped up to the compile-time floor.
     *
     * @param level Requested minimum; cannot go below the floor.
     */
    void setMinLevel(Level level) noexcept;

    /**
     * @brief Enables or disables a category entirely.
     *
     * @param category Category to change.
     * @param enabled  @c false suppresses all records in the category.
     */
    void setCategoryEnabled(Category category, bool enabled) noexcept;

    /**
     * @brief Sets a minimum level that applies only to one category,
     *        overriding the global minimum for it.
     *
     * Clamped up to the compile-time floor. The category bit must still be
     * enabled.
     *
     * @param category Category to override.
     * @param level    Requested minimum for that category.
     */
    void setCategoryMinLevel(Category category, Level level) noexcept;

    /**
     * @brief Removes a category's minimum-level override so the category
     *        follows the global minimum again.
     */
    void clearCategoryMinLevel(Category category) noexcept;

    /**
     * @brief Sets the current frame number.
     *
     * The value is sampled into each record at log time so lines can be
     * correlated with the game loop.
     */
    void setFrame(std::uint64_t frame) noexcept;

    /**
     * @brief Adds a sink after initialization via a control record on the ring.
     *
     * The backend takes ownership between log records. No-op if @p sink is
     * null or the logger is not running. A full ring drops the control
     * record, counts overflow, and destroys @p sink on this thread.
     *
     * @param sink Sink to add; ownership is transferred on success.
     */
    void addSink(std::unique_ptr<Sink> sink) noexcept;

    /**
     * @brief Returns a snapshot of the diagnostic counters.
     *
     * sinkWriteFailures is indexed in the order sinks were registered.
     */
    [[nodiscard]] Counters counters() noexcept;

    /**
     * @brief Core logging entry point with an explicit source location.
     *
     * Prefer log() or the per-level helpers (trace(), debug(), info(),
     * warning(), error(), fatal()), which capture the call site automatically.
     * Use this overload when forwarding a location captured elsewhere.
     *
     * Processing order:
     *  1. Returns immediately if @p level is below the build-time floor.
     *  2. **Fatal only:** the full line is formatted into a fixed-size stack
     *     buffer and written to the console synchronously, before any of the
     *     checks below. Fatal output therefore appears even if the logger is
     *     not running or the category is filtered out.
     *  3. If the logger is not running, counts the call in
     *     Counters::notRunning and returns.
     *  4. Returns if the runtime level/category filters reject the record.
     *  5. Samples timestamp, thread id and frame (already done in step 2 for
     *     fatal records) and enqueues the record. Whether the console already
     *     received a fatal line is passed along with it.
     *
     * Never throws.
     *
     * @tparam Args Types of the format arguments.
     * @param level    Severity of the record.
     * @param category Subsystem the record belongs to.
     * @param location Source location to attribute the record to.
     * @param format   Compile-time-checked format string.
     * @param args     Values to substitute into @p format.
     */
    template <typename ...Args>
    void logAt(
        const Level level,
        const Category category,
        const std::source_location location,
        const fmt::format_string<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if (isBelowFloor(level))
        {
            return;
        }

        const bool isFatal = level == Level::Fatal;
        std::uint64_t timestampNs = 0;
        std::uint32_t threadId = 0;
        std::uint64_t frame = 0;
        bool consoleWritten = false;

        if (isFatal)
        {
            timestampNs = detail::sampleTimestampNs();
            threadId = detail::sampleThreadId();
            frame = detail::sampleFrame();
            std::array<char, detail::kFatalBufferBytes> buffer{};
            const std::size_t used = detail::formatFullLine(
                buffer.data(),
                buffer.size(),
                level,
                category,
                location,
                timestampNs,
                threadId,
                frame,
                detail::sourceRootPrefix(),
                format.get(),
                args ...
            );
            consoleWritten = detail::tryWriteFatalConsole(std::string_view{buffer.data(), used});
        }

        if (!detail::loggerRunning())
        {
            detail::countNotRunning();
            return;
        }

        if (!detail::shouldLog(level, category))
        {
            return;
        }

        if (!isFatal)
        {
            timestampNs = detail::sampleTimestampNs();
            threadId = detail::sampleThreadId();
            frame = detail::sampleFrame();
        }

        detail::enqueueLog(
            level,
            category,
            location,
            format,
            consoleWritten,
            timestampNs,
            threadId,
            frame,
            std::forward<Args>(args) ...
        );
    }

    /**
     * @brief Logs a message at an arbitrary level, capturing the call site.
     *
     * @c FormatString records the caller's source location when constructed,
     * so no location needs to be passed explicitly. Argument expressions are
     * always evaluated; a disabled call returns before capture.
     *
     * @tparam Args Types of the format arguments.
     * @param level    Severity of the record.
     * @param category Subsystem the record belongs to.
     * @param format   Compile-time-checked format string plus call-site location.
     * @param args     Values to substitute into @p format.
     * @see logAt()
     */
    template <typename ...Args>
    void
        log(
            const Level level,
            const Category category,
            const FormatString<std::type_identity_t<Args> ...> format,
            Args && ...args
        ) noexcept
    {
        logAt(level, category, format.location, format.format, std::forward<Args>(args) ...);
    }

    /**
     * @brief Logs at Level::Trace. Compiled out entirely when Trace is below
     *        the build-time floor.
     * @see log()
     */
    template <typename ...Args>
    void trace(
        const Category category,
        const FormatString<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Trace))
        {
            log(Level::Trace, category, format, std::forward<Args>(args) ...);
        }
    }

    /**
     * @brief Logs at Level::Debug. Compiled out entirely when Debug is below
     *        the build-time floor.
     * @see log()
     */
    template <typename ...Args>
    void debug(
        const Category category,
        const FormatString<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Debug))
        {
            log(Level::Debug, category, format, std::forward<Args>(args) ...);
        }
    }

    /**
     * @brief Logs at Level::Info. Compiled out entirely when Info is below
     *        the build-time floor.
     * @see log()
     */
    template <typename ...Args>
    void
        info(
            const Category category,
            const FormatString<std::type_identity_t<Args> ...> format,
            Args && ...args
        ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Info))
        {
            log(Level::Info, category, format, std::forward<Args>(args) ...);
        }
    }

    /**
     * @brief Logs at Level::Warning. Compiled out entirely when Warning is
     *        below the build-time floor.
     * @see log()
     */
    template <typename ...Args>
    void warning(
        const Category category,
        const FormatString<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Warning))
        {
            log(Level::Warning, category, format, std::forward<Args>(args) ...);
        }
    }

    /**
     * @brief Logs at Level::Error. Compiled out entirely when Error is below
     *        the build-time floor.
     * @see log()
     */
    template <typename ...Args>
    void error(
        const Category category,
        const FormatString<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Error))
        {
            log(Level::Error, category, format, std::forward<Args>(args) ...);
        }
    }

    /**
     * @brief Logs at Level::Fatal.
     *
     * The line is written synchronously to the console before it is queued,
     * and bypasses the "logger running" and runtime filter checks for that
     * console write. See logAt() for details. This function logs only; it does
     * not terminate the program.
     *
     * @see log()
     */
    template <typename ...Args>
    void fatal(
        const Category category,
        const FormatString<std::type_identity_t<Args> ...> format,
        Args && ...args
    ) noexcept
    {
        if constexpr (isAtLeastFloor(Level::Fatal))
        {
            log(Level::Fatal, category, format, std::forward<Args>(args) ...);
        }
    }
} // namespace viscera::logging
