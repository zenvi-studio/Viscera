#pragma once

#include <cstdint>

/**
 * @file
 * @brief Cached operating-system thread id for log lines.
 *
 * The producer stores a 32-bit OS tid on the record, not std::thread::id.
 * The first call on a thread queries the platform and caches the result in
 * thread-local storage; later logs on that thread reuse it.
 */

namespace viscera::logging::detail
{
    /**
     * @brief Returns the calling thread's OS thread id.
     *
     * Windows uses GetCurrentThreadId, Linux uses gettid. Other platforms
     * hash std::this_thread::get_id() into a uint32_t once; a collision
     * would mislabel a thread in the text, not corrupt the queue.
     *
     * Used by sampleThreadId() on the producer path, and directly by the
     * backend when it formats overflow and sink-disabled diagnostics (those
     * lines show the backend thread, not the original producer).
     */
    [[nodiscard]] std::uint32_t currentThreadId() noexcept;
} // namespace viscera::logging::detail
