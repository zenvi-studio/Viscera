#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "Record.hpp"

/**
 * @file
 * @brief Bounded MPSC ring of Record slots.
 *
 * One producer-shared ring, one consumer (the backend). The ring owns the
 * mutex and condition variable but does not lock internally: every method
 * except notifyOne()/notifyAll() assumes the caller already holds mutex(),
 * or is running with exclusive ownership during initialize()/shutdown().
 *
 * Publish is two-phase so capture can fill a slot and then either commit
 * or abort: writeSlot() returns the next cell without making it visible;
 * publish() advances the write index. The matching consume path is
 * readSlot() then consume() after the destroy thunk has run.
 */

namespace viscera::logging::detail
{
    /**
     * @brief Fixed-capacity ring of Record slots between producers and the backend.
     *
     * Capacity is a power of two so indices wrap with a mask. Storage is
     * allocated in reset() (initialize) and released in clear() (after the
     * backend has joined). The hot path does not grow the ring.
     */
    class Ring
    {
        public:
            /**
             * @brief Allocates @p capacity default-constructed slots and resets indices.
             *
             * @p capacity must be a power of two. Called from initialize()
             * after rounding Config::queueCapacity. Not noexcept: resize may
             * allocate.
             *
             * @param capacity Number of slots; 0 leaves the ring unusable.
             */
            void reset(std::size_t capacity);

            /**
             * @brief Releases slot storage and resets indices to empty.
             *
             * Called from shutdown() after the backend has joined, and from
             * initialize() if the backend thread fails to start. Occupied
             * records must already have been destroyed.
             */
            void clear() noexcept;

            /**
             * @brief Mutex that guards occupancy and slot hand-off.
             *
             * Producers lock around writeSlot()/publish() (or abort). The
             * backend locks around wait, readSlot(), destroy, and consume().
             * It is never held across Sink::write or Sink::flush, and it is
             * released while the backend formats from an occupied slot.
             */
            [[nodiscard]] std::mutex &mutex() noexcept { return m_mutex; }

            /**
             * @brief Condition variable the backend waits on.
             *
             * Predicate is owned by the logger (not empty, stop, abandon, or
             * flush). Producers wake one waiter after a successful publish;
             * shutdown, flush, and the backend stop_callback wake all.
             */
            [[nodiscard]] std::condition_variable &cv() noexcept { return m_cv; }

            /**
             * @brief Wakes one backend waiter. Used after a successful publish.
             */
            void notifyOne() noexcept;

            /**
             * @brief Wakes every backend waiter. Used for stop, flush, and abandon.
             */
            void notifyAll() noexcept;

            /**
             * @brief Returns whether no published slot is waiting to be consumed.
             */
            [[nodiscard]] bool empty() const noexcept;

            /**
             * @brief Returns whether occupied() equals capacity.
             *
             * A full ring causes the producer to drop and count overflow
             * instead of waiting for space.
             */
            [[nodiscard]] bool full() const noexcept;

            /**
             * @brief Number of published slots not yet consumed.
             *
             * @return @c m_write - @c m_read. Index wrap uses the mask;
             *         these counters themselves are monotonic.
             */
            [[nodiscard]] std::uint64_t occupied() const noexcept;

            /**
             * @brief Returns the unpublished slot at the current write index.
             *
             * Does not advance the index. acquireLogSlot() and commitLogSlot()
             * both call this and see the same cell until publish() or abort.
             * The caller must have already checked !full().
             *
             * @return Pointer into m_slots; never null while capacity > 0.
             */
            [[nodiscard]] Record *writeSlot() noexcept;

            /**
             * @brief Makes the current write slot visible to the consumer.
             *
             * Increments the write index. Call only after the slot is fully
             * filled (header, payload objects, and thunks).
             */
            void publish() noexcept;

            /**
             * @brief Returns the occupied slot at the current read index.
             *
             * Does not advance the index. The slot stays occupied so producers
             * cannot overwrite it while the backend formats with the mutex
             * released. The caller must have already checked !empty().
             *
             * @return Pointer into m_slots; never null while capacity > 0.
             */
            [[nodiscard]] Record *readSlot() noexcept;

            /**
             * @brief Frees the current read slot for reuse.
             *
             * Increments the read index. Call only after the destroy thunk
             * has run (or was null) and, for AddSink, after controlSink
             * ownership has been taken or discarded.
             */
            void consume() noexcept;

        private:
            std::mutex m_mutex;
            std::condition_variable m_cv;
            std::vector<Record> m_slots;

            /** Slot count; power of two after reset(). */
            std::uint64_t m_capacity{0};

            /** @c m_capacity - 1, used as @c index & m_mask to wrap. */
            std::uint64_t m_mask{0};

            /** Next publish index; monotonic. */
            std::uint64_t m_write{0};

            /** Next consume index; monotonic. */
            std::uint64_t m_read{0};
    };
} // namespace viscera::logging::detail
