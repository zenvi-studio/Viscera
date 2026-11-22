#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 * @file
 * @brief Shared size and token constants for the logging internals.
 *
 * These values are the budgets and markers the capture path, the Fatal
 * stack format, the backend format buffer, and the ring all agree on.
 * Changing one without updating the code that consumes it will desync
 * those paths.
 */

namespace viscera::logging::detail
{
    /**
     * @brief Bytes of inline argument storage in each Record.
     *
     * Capture measures and constructs arguments into Record::payload. A pack
     * that does not fit is dropped and counted as an argument-budget failure.
     * Independent of kFatalBufferBytes.
     */
    inline constexpr std::size_t kPayloadBytes = 256;

    /**
     * @brief Size of the stack buffer used to format a Fatal line on the caller.
     *
     * logAt() formats into this buffer and writes it to the console before
     * enqueueing. Overflow is truncated with kTruncationMarker. Independent
     * of kPayloadBytes: a Fatal line can print truncated and still be dropped
     * from the ring for not fitting in the slot.
     */
    inline constexpr std::size_t kFatalBufferBytes = 1024;

    /**
     * @brief Initial size of the backend thread's reusable format buffer.
     *
     * The backend reserves this many bytes once, then grows the buffer if a
     * formatted line does not fit. Growth is not a producer allocation.
     */
    inline constexpr std::size_t kFormatBufferBytes = 4096;

    /**
     * @brief Default Config::queueCapacity, in ring slots.
     *
     * initialize() rounds the configured value up to a power of two. Tests
     * pass a smaller capacity so they can fill the ring.
     */
    inline constexpr std::size_t kDefaultQueueCapacity = 8192;

    /**
     * @brief Consecutive sink write failures after which that sink is disabled.
     *
     * A false return or a thrown write increments the sink's streak; a success
     * resets it. On disable, the backend emits one diagnostic to the sinks
     * that are still enabled. The sink is not retried until setEnabled(true).
     */
    inline constexpr std::uint32_t kConsecutiveFailuresToDisable = 3;

    /**
     * @brief Marker overwritten onto the last bytes of a truncated Fatal line.
     *
     * formatFullLine() applies this when the header or message does not fit
     * in kFatalBufferBytes. The backend format buffer grows instead, so it
     * does not use this marker.
     */
    inline constexpr std::string_view kTruncationMarker = "...";

    /**
     * @brief Text stored for a null C-string log argument.
     *
     * storeCString() writes a CapturedString pointing at this static token
     * with inlineStorage false, so the payload holds only the header.
     */
    inline constexpr std::string_view kNullToken = "(null)";

    /**
     * @brief Record::flags bit set when Fatal already wrote the line to the console.
     *
     * acquireLogSlot() sets it from logAt()'s consoleWritten result. The
     * backend skips the console sink for that record so stderr is not
     * doubled. If the direct write was skipped, the bit stays clear and the
     * backend writes the console as usual.
     */
    inline constexpr std::uint8_t kFlagConsoleWritten = 1U;
} // namespace viscera::logging::detail
