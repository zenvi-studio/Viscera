#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <fmt/format.h>

#include "Constants.hpp"
#include "viscera/logging/Sink.hpp"
#include "viscera/logging/Types.hpp"

/**
 * @file
 * @brief Ring-slot layout: a log record or a sink-control record.
 *
 * The queue is homogeneous. The backend never sees the C++ argument types;
 * it calls formatThunk, then destroyThunk, then reuses the slot. Header
 * scalars are trivially copyable. Argument objects in payload are
 * constructed in place and must not be memcpy'd as a blob.
 */

namespace viscera::logging::detail
{
    struct Record;

    /**
     * @brief Backend entry that formats this record's message into @p out.
     *
     * Capture installs a specialization that knows the original Args... pack.
     * Null on AddSink records. The backend calls this with the ring mutex
     * released while the slot is still occupied.
     */
    using FormatThunk = void (*)(fmt::memory_buffer &out, const Record &record);

    /**
     * @brief Backend entry that ends argument lifetime in payload.
     *
     * Null when every stored argument is trivially destructible, and on
     * AddSink records. The backend calls this (if non-null) while holding
     * the ring mutex, immediately before consume().
     */
    using DestroyThunk = void (*)(Record &record);

    /**
     * @brief Length-prefixed string stored in a Record payload.
     *
     * Characters are not a std::string. When inlineStorage is true, data
     * points at bytes immediately after this header in payload and size is
     * the character count with no terminator. When inlineStorage is false,
     * data points at static storage (the kNullToken for a null C string)
     * and payload holds only this header.
     */
    struct CapturedString
    {
        const char *data{};
        std::uint32_t size{};
        bool inlineStorage{false};
    };

    /**
     * @brief One cell of the logging ring.
     *
     * Occupancy is tracked by Ring indices, not by Kind: a consumed slot is
     * not reset to Empty, it is overwritten on the next publish. Published
     * records are Kind::Log or Kind::AddSink.
     */
    struct Record
    {
        /**
         * @brief What this slot currently holds once published.
         */
        enum class Kind : std::uint8_t
        {
            Empty,   /**< Default-constructed slot; not published. */
            Log,     /**< Captured log record with format/destroy thunks. */
            AddSink, /**< Ownership of controlSink transferred to the backend. */
        };

        Kind kind{Kind::Empty};

        /** Severity of a Log record. Unused for AddSink. */
        Level level{};

        /** Subsystem of a Log record. Unused for AddSink. */
        Category category{Category::Core};

        /** Producer timestamp from sampleTimestampNs(), in nanoseconds. */
        std::uint64_t timestampNs{};

        /** Producer OS thread id from sampleThreadId(). */
        std::uint32_t threadId{};

        /** Frame number sampled at log time via sampleFrame(). */
        std::uint64_t frame{};

        /**
         * Path from std::source_location; pointer into static storage.
         * Not copied. The backend shortens it with shortFile().
         */
        const char *file{};

        /**
         * Function name from std::source_location; pointer into static
         * storage. Stored on the record and not printed in the line.
         */
        const char *function{};

        /** Source line from std::source_location. Column is not stored. */
        std::uint32_t line{};

        /**
         * Format string from the call site. A view into static storage,
         * not a copy, and not counted against kPayloadBytes.
         */
        std::string_view format;

        /** Installed by capture; null for AddSink. */
        FormatThunk formatThunk{};

        /** Installed by capture when payload is non-trivial; null otherwise. */
        DestroyThunk destroyThunk{};

        /**
         * Bit set. kFlagConsoleWritten means Fatal already wrote this line
         * to the console, so the backend skips the console sink.
         */
        std::uint8_t flags{};

        /**
         * Owning raw pointer used only for Kind::AddSink. The producer
         * releases a unique_ptr into this field; the backend takes it back
         * with unique_ptr, or discards it on abandon. Null for Log records.
         */
        Sink *controlSink{};

        /**
         * Inline argument storage. Capture constructs objects here with
         * std::construct_at; the destroy thunk runs std::destroy_at before
         * the slot is reused. Aligned for any max_align_t object.
         */
        alignas(std::max_align_t) std::array<std::byte, kPayloadBytes> payload{};
    };
} // namespace viscera::logging::detail
