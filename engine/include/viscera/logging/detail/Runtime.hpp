#pragma once

#include <cstdint>
#include <source_location>
#include <string_view>

#include "Record.hpp"
#include "viscera/logging/Types.hpp"

/**
 * @file
 * @brief Producer-side facade over the process-wide logger.
 *
 * logAt() and enqueueLog() live in headers and cannot see the Logger class
 * in Logger.cpp. These functions forward to that instance: filter, sample
 * call-site context, take a ring slot, and count drops. None of them throw.
 */

namespace viscera::logging::detail
{
    /**
     * @brief Enables or disables debug-only contract asserts.
     *
     * When enabled (the default), a broken contract such as logging while
     * not running fires @c assert in non-NDEBUG builds. Tests turn this off
     * around initialize/shutdown so expected not-running calls are counted
     * instead of aborting. Compiles to a no-op in NDEBUG.
     *
     * @param enabled @c false suppresses contract asserts.
     */
    void setContractAssertsEnabled(bool enabled) noexcept;

    /**
     * @brief Returns whether initialize() has succeeded and shutdown() has not begun.
     *
     * logAt() checks this after the Fatal console write and before shouldLog()
     * or enqueue. Maps to RunState::Running.
     */
    [[nodiscard]] bool loggerRunning() noexcept;

    /**
     * @brief Increments Counters::notRunning.
     *
     * Called from logAt() when the logger is not running, and from
     * acquireLogSlot() if the state changed after the ring mutex was taken.
     * Also fires a contract assert in debug when asserts are enabled.
     */
    void countNotRunning() noexcept;

    /**
     * @brief Increments Counters::argumentBudget.
     *
     * Called from enqueueLog() when the argument pack does not fit in
     * kPayloadBytes, or when storeArgs() fails after a slot was acquired.
     */
    void countArgumentBudget() noexcept;

    /**
     * @brief Runtime level and category filter, after the compile-time floor.
     *
     * Returns @c false if @p level is below the effective minimum for
     * @p category (per-category override if set, otherwise the global min
     * level), or if @p category's bit is cleared in the enabled mask.
     * Does not sample or capture. The floor is checked separately by logAt()
     * via isBelowFloor().
     *
     * @param level    Severity of the call.
     * @param category Subsystem of the call.
     * @return @c true if the record should be enqueued.
     */
    [[nodiscard]] bool shouldLog(Level level, Category category) noexcept;

    /**
     * @brief Nanoseconds of steady_clock elapsed since initialize().
     *
     * Returns 0 if no session base is set (before initialize or after
     * shutdown). Fatal samples this before the running check, so a Fatal
     * line taken while stopped can print 0. Not a wall-clock time.
     */
    [[nodiscard]] std::uint64_t sampleTimestampNs() noexcept;

    /**
     * @brief Cached OS thread id of the calling thread.
     *
     * Forwards to currentThreadId(). Sampled on the producer into the
     * record; the backend does not recapture it for log lines.
     */
    [[nodiscard]] std::uint32_t sampleThreadId() noexcept;

    /**
     * @brief Latest frame number published by setFrame().
     *
     * Relaxed atomic load. 0 until the game loop calls setFrame(). A
     * setFrame that happens-before a log on the same thread is visible to
     * that log; another thread may still observe the previous frame briefly.
     */
    [[nodiscard]] std::uint64_t sampleFrame() noexcept;

    /**
     * @brief Config::sourceRootPrefix for the current session.
     *
     * Used by formatFullLine() on the Fatal stack path. The backend reads
     * the same prefix from the logger when it calls shortFile(). May be
     * empty, in which case paths are printed unshortened.
     */
    [[nodiscard]] std::string_view sourceRootPrefix() noexcept;

    /**
     * @brief Best-effort direct write of a Fatal line to the console sink.
     *
     * Returns @c false if there is no console sink, it is disabled, or its
     * mutex is already held (try_lock). On success the line is already on
     * stderr and logAt() passes consoleWritten so the backend skips that sink.
     * The enqueue still happens if this write is skipped.
     *
     * @param line Already formatted Fatal text, typically from formatFullLine().
     * @return @c true if the console received the line.
     */
    [[nodiscard]] bool tryWriteFatalConsole(std::string_view line) noexcept;

    /**
     * @brief Locks the ring and fills the next unpublished Log slot header.
     *
     * Does not publish. On success the caller still holds the ring mutex and
     * must commitLogSlot() or abortLogSlot(). Returns null and unlocks if
     * the mutex cannot be taken (counted as overflow), the logger is not
     * running, or the ring is full (overflow). Thunks are left null until
     * commit. Sets kFlagConsoleWritten from @p consoleWritten.
     *
     * @param level          Severity stored on the record.
     * @param category       Subsystem stored on the record.
     * @param timestampNs    Value from sampleTimestampNs().
     * @param threadId       Value from sampleThreadId().
     * @param frame          Value from sampleFrame().
     * @param file           Static source-location path; not copied.
     * @param function       Static source-location function name; not copied.
     * @param line           Source line.
     * @param format         Static format-string view; not copied.
     * @param consoleWritten Whether Fatal already wrote this line to stderr.
     * @return The unpublished slot, or null if the record was dropped.
     */
    [[nodiscard]] Record *acquireLogSlot(
        Level level,
        Category category,
        std::uint64_t timestampNs,
        std::uint32_t threadId,
        std::uint64_t frame,
        const char *file,
        const char *function,
        std::uint32_t line,
        std::string_view format,
        bool consoleWritten
    ) noexcept;

    /**
     * @brief Installs thunks on the acquired slot, publishes it, and unlocks.
     *
     * writeSlot() returns the same cell acquireLogSlot() filled. Wakes one
     * backend waiter. @p destroyFn may be null when the payload is trivially
     * destructible.
     *
     * @param formatFn  Format thunk for this argument pack; not null for Log.
     * @param destroyFn Destroy thunk, or null.
     */
    void commitLogSlot(FormatThunk formatFn, DestroyThunk destroyFn) noexcept;

    /**
     * @brief Unlocks the ring without publishing the acquired slot.
     *
     * The slot is not visible to the consumer and will be overwritten on
     * the next acquire. Called from enqueueLog() after a failed storeArgs().
     */
    void abortLogSlot() noexcept;
} // namespace viscera::logging::detail
