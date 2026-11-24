#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include "Constants.hpp"
#include "Record.hpp"
#include "Runtime.hpp"

/**
 * @file
 * @brief Producer-side argument capture for deferred logging.
 *
 * Packs log arguments into a Record's kPayloadBytes payload so the backend
 * can format them later. Strings are copied as characters, enumerations are
 * stored as their underlying integer, and other pointers are stored as
 * addresses. Public log functions call enqueueLog() after filtering.
 */

namespace viscera::logging::detail
{
    /**
     * @brief Rounds @p value up to the next multiple of @p alignment.
     *
     * Used when laying arguments into a Record payload so each stored object
     * sits at a correctly aligned offset. @p alignment must be a power of two.
     *
     * @param value     Current byte offset.
     * @param alignment Required alignment of the next stored object.
     * @return The smallest multiple of @p alignment that is >= @p value.
     */
    [[nodiscard]] constexpr std::size_t alignUp(const std::size_t value, const std::size_t alignment) noexcept
    {
        return (value + alignment - 1U) & ~(alignment - 1U);
    }

    /**
     * @brief Maps a non-enumeration argument type to the type stored in the payload.
     *
     * Identity: @c Type is @p T. ArgTraits uses this for every non-string,
     * non-pointer argument; the enum specialization below replaces @c Type
     * with the underlying integer so enumerations are not stored as enum
     * objects.
     *
     * @tparam T      Argument type after decay.
     * @tparam IsEnum Discriminator; defaults to @c std::is_enum_v<T>.
     */
    template <typename T, bool IsEnum = std::is_enum_v<T>>
    struct StoredEnum
    {
        using Type = T;
    };

    /**
     * @brief Maps an enumeration to the integer type stored in the payload.
     *
     * @c Type is @c std::underlying_type_t<T>. The payload therefore holds a
     * formattable integer, and the backend formats that integer rather than
     * the original enum type.
     *
     * @tparam T Enumeration type after decay.
     */
    template <typename T>
    struct StoredEnum<T, true>
    {
        using Type = std::underlying_type_t<T>;
    };

    /**
     * @brief Compile-time classification of a log argument.
     *
     * Decides how @p T is stored in the Record payload and how it is
     * presented to {fmt} on the backend. Measure, store, load, and destroy
     * all consult these aliases and flags.
     *
     * @tparam T Argument type as passed at the call site (references are
     *           allowed; they are decayed before classification).
     */
    template <typename T>
    struct ArgTraits
    {
        /**
         * @brief The decayed form of the argument type.
         *
         * Removes references and top-level cv-qualifiers and applies array-to-pointer
         * and function-to-pointer conversions, providing a normalized type for
         * subsequent type comparisons and trait evaluations.
         */
        using Decayed = std::decay_t<T>;

        /**
         * @brief Indicates whether the argument is a supported string type.
         *
         * Recognizes C-style character pointers, std::string, and std::string_view.
         * Those types are stored as CapturedString and formatted as std::string_view.
         */
        static constexpr bool kIsString =
            std::is_same_v<Decayed, char*> ||
            std::is_same_v<Decayed, const char*> ||
            std::is_same_v<Decayed, std::string> ||
            std::is_same_v<Decayed, std::string_view>;

        /**
         * @brief Indicates whether the argument is an enumeration type.
         *
         * Evaluated against the decayed argument type.
         */
        static constexpr bool kIsEnum = std::is_enum_v<Decayed>;

        /**
         * @brief Indicates whether the argument is a non-string pointer type.
         *
         * Excludes recognized string types so that character pointers are handled
         * as strings rather than generic pointers.
         */
        static constexpr bool kIsPointer = std::is_pointer_v<Decayed> && !kIsString;

        /**
         * @brief Type constructed into the Record payload for this argument.
         *
         * - String types: CapturedString (header plus copied characters).
         * - Other pointers: @c const void* (the address; never dereferenced).
         * - Enumerations: underlying integer, via StoredEnum.
         * - Remaining types: themselves, via StoredEnum's identity mapping.
         */
        using Stored = std::conditional_t<
            kIsString,
            CapturedString,
            std::conditional_t < kIsPointer, const void*, typename StoredEnum<Decayed>::Type>
        >;

        /**
         * @brief Type passed to {fmt} when the backend formats the record.
         *
         * Strings become std::string_view over the captured characters. Every
         * other argument is formatted as Stored, so enumerations print as
         * their underlying integer.
         */
        using Formatted = std::conditional_t<kIsString, std::string_view, Stored>;
    };

    /**
     * @brief Advances @p offset past an aligned region of @p size bytes.
     *
     * Aligns @p offset to @p alignment, then checks that @p size further
     * bytes still fit in kPayloadBytes. On success, @p offset becomes the
     * end of that region. On failure, @p offset is left unchanged.
     *
     * @param offset    Current write offset; updated on success.
     * @param size      Number of bytes to reserve after alignment.
     * @param alignment Alignment required for the reserved region.
     * @return @c true if the region fits in the payload.
     */
    [[nodiscard]] inline bool
        accountBytes(std::size_t &offset, const std::size_t size, const std::size_t alignment) noexcept
    {
        const std::size_t aligned = alignUp(offset, alignment);
        if (aligned + size > kPayloadBytes)
        {
            return false;
        }

        offset = aligned + size;
        return true;
    }

    /**
     * @brief Measures the payload cost of a C-style string without copying it.
     *
     * Reserves a CapturedString header, then the characters up to the first
     * NUL, searching only within the remaining payload budget (no unbounded
     * strlen). A null @p value reserves the header only, matching
     * storeCString()'s static kNullToken. If no NUL appears in the remaining
     * bytes, the call fails and the whole record will be dropped.
     *
     * @tparam T Pointer element type (typically char).
     * @param offset Current write offset; updated on success.
     * @param value  Pointer to a NUL-terminated string, or null.
     * @return @c true if the string fits in the remaining payload.
     */
    template <typename T>
    [[nodiscard]] bool measureCString(std::size_t &offset, const T *const value) noexcept
    {
        const std::size_t aligned = alignUp(offset, alignof(CapturedString));
        if (aligned + sizeof(CapturedString) > kPayloadBytes)
        {
            return false;
        }

        if (value == nullptr)
        {
            offset = aligned + sizeof(CapturedString);
            return true;
        }

        const std::size_t remaining = kPayloadBytes - aligned - sizeof(CapturedString);
        const void *const nul = std::memchr(value, 0, remaining);
        if (nul == nullptr)
        {
            return false;
        }

        const auto length = static_cast<std::size_t>(static_cast<const char*>(nul) - value);
        offset = aligned + sizeof(CapturedString) + length;
        return true;
    }

    /**
     * @brief Measures the payload cost of one argument without copying it.
     *
     * std::string and std::string_view reserve a CapturedString header plus
     * their character count. C strings go through measureCString(). Every
     * other type reserves sizeof/alignof of ArgTraits<T>::Stored.
     *
     * @tparam T Type of @p value.
     * @param offset Current write offset; updated on success.
     * @param value  Argument to measure.
     * @return @c true if the argument fits in the remaining payload.
     */
    template <typename T>
    [[nodiscard]] bool measureArg(std::size_t &offset, const T &value) noexcept
    {
        using Traits = ArgTraits<T>;
        using Decayed = Traits::Decayed;

        if constexpr (Traits::kIsString)
        {
            if constexpr (std::is_same_v<Decayed, std::string> || std::is_same_v<Decayed, std::string_view>)
            {
                const std::string_view view{value};
                return accountBytes(offset, sizeof(CapturedString) + view.size(), alignof(CapturedString));
            }
            else
            {
                return measureCString(offset, static_cast<const char*>(value));
            }
        }
        else
        {
            using Stored = Traits::Stored;
            return accountBytes(offset, sizeof(Stored), alignof(Stored));
        }
    }

    /**
     * @brief Copies a C-style string into the payload as a CapturedString.
     *
     * A null @p value stores a CapturedString that points at the static
     * kNullToken ("(null)") with inlineStorage false, so no extra payload
     * bytes are used. A non-null @p value copies characters after the header
     * (no terminator) and sets inlineStorage true, but only if a NUL is found
     * in the remaining budget.
     *
     * @tparam T Pointer element type (typically char).
     * @param payload Record payload to write into.
     * @param offset  Current write offset; updated on success.
     * @param value   Pointer to a NUL-terminated string, or null.
     * @return @c true if the string was stored; @c false if it does not fit
     *         or has no NUL in the remaining budget.
     */
    template <typename T>
    [[nodiscard]] bool storeCString(
        std::byte * const payload,
        std::size_t & offset,


    const T *const value
    )
    noexcept
    {
        offset = alignUp(offset, alignof(CapturedString));
        if (offset + sizeof(CapturedString) > kPayloadBytes)
        {
            return false;
        }

        auto *const slot = reinterpret_cast<CapturedString *>(payload + offset);
        if (value == nullptr)
        {
            std::construct_at(
                slot,
                CapturedString{
                    .data = "(null)",
                    .size = static_cast<std::uint32_t>(kNullToken.size()),
                    .inlineStorage = false,
                }
                );
            offset += sizeof(CapturedString);
            return true;
        }

        const std::size_t remaining = kPayloadBytes - offset - sizeof(CapturedString);
        const void *const nul = std::memchr(value, 0, remaining);
        if (nul == nullptr)
        {
            return false;
        }

        const auto length = static_cast<std::uint32_t>(static_cast<const char *>(nul) - value);
        auto *const chars = reinterpret_cast<char *>(slot + 1);
        if (length != 0)
        {
            std::memcpy(chars, value, length);
        }

        std::construct_at(
            slot,
            CapturedString{
                .data = chars,
                .size = length,
                .inlineStorage = true,
            }
            );
        offset += sizeof(CapturedString) + length;
        return true;
    }

    /**
     * @brief Constructs one argument into the payload.
     *
     * Dispatches on ArgTraits<T>:
     *  - std::string / std::string_view: characters copied after a
     *    CapturedString header (inlineStorage true).
     *  - C strings: storeCString().
     *  - Enumerations: stored as the underlying integer.
     *  - Other pointers: stored as @c const void* (the address, never
     *    dereferenced).
     *  - Remaining types: placement-constructed via std::forward. The stored
     *    type must be nothrow constructible from @p value.
     *
     * @tparam T Type of @p value.
     * @param payload Record payload to write into.
     * @param offset  Current write offset; updated on success.
     * @param value   Argument to capture.
     * @return @c true if the argument was stored in the remaining payload.
     */
    template <typename T>
    [[nodiscard]] bool storeArg(
        std::byte * const payload,
        std::size_t & offset,
        T && value
    )
    noexcept
    {
        using Traits = ArgTraits<T>;
        using Decayed = Traits::Decayed;
        using Stored = Traits::Stored;

        if constexpr (Traits::kIsString)
        {
            if constexpr (std::is_same_v<Decayed, std::string> || std::is_same_v<Decayed, std::string_view>)
            {
                const std::string_view view{value};
                offset = alignUp(offset, alignof(CapturedString));
                if (offset + sizeof(CapturedString) + view.size() > kPayloadBytes)
                {
                    return false;
                }

                auto *const slot = reinterpret_cast<CapturedString *>(payload + offset);
                auto *const chars = reinterpret_cast<char *>(slot + 1);
                if (!view.empty())
                {
                    std::memcpy(chars, view.data(), view.size());
                }

                std::construct_at(
                    slot,
                    CapturedString{
                        .data = chars,
                        .size = static_cast<std::uint32_t>(view.size()),
                        .inlineStorage = true,
                    }
                    );
                offset += sizeof(CapturedString) + view.size();
                return true;
            }
            else
            {
                return storeCString(payload, offset, static_cast<const char *>(value));
            }
        }
        else if constexpr (Traits::kIsEnum)
        {
            static_assert(std::is_nothrow_constructible_v<Stored, Stored>);
            offset = alignUp(offset, alignof(Stored));
            if (offset + sizeof(Stored) > kPayloadBytes)
            {
                return false;
            }

            std::construct_at(reinterpret_cast<Stored *>(payload + offset), static_cast<Stored>(value));
            offset += sizeof(Stored);
            return true;
        }
        else if constexpr (Traits::kIsPointer)
        {
            offset = alignUp(offset, alignof(Stored));
            if (offset + sizeof(Stored) > kPayloadBytes)
            {
                return false;
            }

            std::construct_at(reinterpret_cast<Stored *>(payload + offset), static_cast<const void *>(value));
            offset += sizeof(Stored);
            return true;
        }
        else
        {
            static_assert(std::is_nothrow_constructible_v<Stored, T>);
            offset = alignUp(offset, alignof(Stored));
            if (offset + sizeof(Stored) > kPayloadBytes)
            {
                return false;
            }

            std::construct_at(reinterpret_cast<Stored *>(payload + offset), std::forward<T>(value));
            offset += sizeof(Stored);
            return true;
        }
    }

    /**
     * @brief Base case of measureArgsFrom(): an empty pack always fits.
     */
    [[nodiscard]] constexpr bool measureArgsFrom(std::size_t) noexcept
    {
        return true;
    }

    /**
     * @brief Measures @p value then the rest of the pack, left to right.
     *
     * @param offset Current write offset (passed by value; the recursive call
     *               receives the updated offset).
     * @param value  Next argument to measure.
     * @param rest   Remaining arguments.
     * @return @c true if every argument fits in kPayloadBytes.
     */
    template <typename T, typename ...Rest>
    [[nodiscard]] bool measureArgsFrom(std::size_t offset, const T &value, const Rest & ...rest) noexcept
    {
        if (!measureArg(offset, value))
        {
            return false;
        }

        return measureArgsFrom(offset, rest ...);
    }

    /**
     * @brief Returns whether the full argument pack fits in a Record payload.
     *
     * Starts measuring at offset 0. Called by enqueueLog() before a slot is
     * taken, so an oversized pack never occupies the ring.
     *
     * @tparam Args Types of the arguments to measure.
     * @param args Arguments to measure.
     * @return @c true if every argument fits.
     */
    template <typename ...Args>
    [[nodiscard]] bool measureArgs(const Args & ...args) noexcept
    {
        return measureArgsFrom(0, args ...);
    }

    /**
     * @brief Base case of storeArgsFrom(): an empty pack is already stored.
     */
    [[nodiscard]] constexpr bool storeArgsFrom(std::byte *, std::size_t) noexcept
    {
        return true;
    }

    /**
     * @brief Stores @p value then the rest of the pack, left to right.
     *
     * @tparam T    Type of @p value.
     * @tparam Rest Types of the remaining arguments.
     * @param payload Record payload to write into.
     * @param offset  Current write offset (passed by value; the recursive call
     *                receives the updated offset).
     * @param value   Next argument to store.
     * @param rest    Remaining arguments.
     * @return @c true if every argument was stored.
     */
    template <typename T, typename ...Rest>
    [[nodiscard]] bool storeArgsFrom(
        std::byte * const payload,
        std::size_t offset,
        T && value,
        Rest && ...rest
    )
    noexcept
    {
        if (!storeArg(payload, offset, std::forward<T>(value)))
        {
            return false;
        }

        return storeArgsFrom(payload, offset, std::forward<Rest>(rest) ...);
    }

    /**
     * @brief Stores the full argument pack into @p payload starting at offset 0.
     *
     * @tparam Args Types of the arguments to store.
     * @param payload Record payload to write into.
     * @param args    Arguments to capture.
     * @return @c true if every argument was stored.
     */
    template <typename ...Args>
    [[nodiscard]] bool storeArgs(
        std::byte * const payload,
        Args && ...args
    )
    noexcept
    {
        return storeArgsFrom(payload, 0, std::forward<Args>(args) ...);
    }

    /**
     * @brief Reads one stored argument from the payload and advances @p offset.
     *
     * For CapturedString, also skips the inline character bytes when
     * inlineStorage is true, and returns a std::string_view over those
     * characters (or over the static null token). For every other Stored
     * type, returns a copy of the object.
     *
     * Used by formatThunk() to rebuild the argument pack for {fmt}.
     *
     * @tparam Stored    Type constructed in the payload.
     * @tparam Formatted Type returned to the formatter (string_view for strings).
     * @param payload Record payload to read from.
     * @param offset  Current read offset; updated past this argument.
     * @return The value presented to {fmt}.
     */
    template <typename Stored, typename Formatted>
    [[nodiscard]] Formatted loadArg(const std::byte *const payload, std::size_t &offset) noexcept
    {
        offset = alignUp(offset, alignof(Stored));
        const auto *const value = reinterpret_cast<const Stored*>(payload + offset);
        offset += sizeof(Stored);

        if constexpr (std::is_same_v<Stored, CapturedString>)
        {
            if (value->inlineStorage)
            {
                offset += value->size;
            }

            return Formatted{value->data, value->size};
        }
        else
        {
            return *value;
        }
    }

    /**
     * @brief Destroys stored arguments in reverse order of construction.
     *
     * Recurses through Rest... first, then std::destroy_at on First, matching
     * the construction order of storeArgsFrom(). For an inline CapturedString,
     * the following character bytes are skipped so the next offset is correct;
     * they are not objects and are not destroyed.
     *
     * @tparam First First argument type in the original pack.
     * @tparam Rest  Remaining argument types.
     * @param payload Record payload containing the constructed objects.
     * @param offset  Current offset; updated past this argument.
     */
    template <typename First, typename ...Rest>
    void destroyPack(
        std::byte * const payload,
        std::size_t & offset
    )
    noexcept
    {
        using Stored = ArgTraits<First>::Stored;
        offset = alignUp(offset, alignof(Stored));
        auto *const value = reinterpret_cast<Stored *>(payload + offset);

        const std::size_t extra = [&] noexcept -> std::size_t
        {
            if constexpr (std::is_same_v<Stored, CapturedString>)
            {
                return value->inlineStorage ? value->size : 0;
            }
            else
            {
                return 0;
            }
        }();

        const std::size_t next = offset + sizeof(Stored) + extra;
        if constexpr (sizeof...(Rest) > 0)
        {
            std::size_t restOffset = next;
            destroyPack<Rest ...>(payload, restOffset);
        }

        std::destroy_at(value);
        offset = next;
    }

    /**
     * @brief Format thunk installed on the Record for this argument pack.
     *
     * Reloads each argument from the payload as ArgTraits::Formatted, then
     * formats @c record.format with {fmt} into @p out. An empty pack formats
     * the string with no arguments. Uses a runtime format because the backend
     * does not have the original Args... in its signature.
     *
     * @tparam Args Argument types from the original log call.
     * @param out    Destination buffer owned by the backend.
     * @param record Slot whose payload and format string are formatted.
     */
    template <typename ...Args>
    void formatThunk(fmt::memory_buffer &out, const Record &record)
    {
        if constexpr (sizeof...(Args) == 0)
        {
            fmt::format_to(std::back_inserter(out), fmt::runtime(record.format));
        }
        else
        {
            std::size_t offset = 0;
            const std::tuple<typename ArgTraits<Args>::Formatted ...> packed{
                loadArg<typename ArgTraits<Args>::Stored, typename ArgTraits<Args>::Formatted>(
                    record.payload.data(),
                    offset
                ) ...
            };

            std::apply(
                [&](const auto & ...args)
                {
                    fmt::format_to(std::back_inserter(out), fmt::runtime(record.format), args ...);
                },
                packed
            );
        }
    }

    /**
     * @brief Destroy thunk installed on the Record for this argument pack.
     *
     * Runs destroyPack<Args...>() when the pack is non-empty. enqueueLog()
     * passes nullptr instead of this function when every Stored type is
     * trivially destructible.
     *
     * @tparam Args Argument types from the original log call.
     * @param record Slot whose payload objects should be ended.
     */
    template <typename ...Args>
    void destroyThunk(Record &record)
    {
        if constexpr (sizeof...(Args) > 0)
        {
            std::size_t offset = 0;
            destroyPack<Args ...>(record.payload.data(), offset);
        }
    }

    /**
     * @brief Captures arguments into a ring slot and publishes the record.
     *
     * Called from logAt() after filtering and after timestamp, thread id, and
     * frame have been sampled. Never throws.
     *
     *  1. Static-asserts that every ArgTraits::Formatted type is formattable.
     *  2. Empty pack: acquire a slot and commit with formatThunk only.
     *  3. Otherwise measureArgs(); if the pack does not fit, count the
     *     argument-budget drop and return without taking a slot.
     *  4. Acquire a slot. If the ring is full, return (overflow is counted
     *     by acquireLogSlot).
     *  5. storeArgs() into the slot payload. If that fails, run destroyPack
     *     when any stored type is non-trivial, count argument-budget, and
     *     abort the slot so it is not published.
     *  6. Commit with formatThunk and, unless every stored type is trivially
     *     destructible, destroyThunk.
     *
     * @tparam Args Types of the format arguments.
     * @param level          Severity of the record.
     * @param category       Subsystem the record belongs to.
     * @param location       Call site stored on the record.
     * @param format         Compile-time-checked format string.
     * @param consoleWritten Whether a Fatal line was already written to the
     *                       console on this thread.
     * @param timestampNs    Producer timestamp already sampled by the caller.
     * @param threadId       Producer thread id already sampled by the caller.
     * @param frame          Frame number already sampled by the caller.
     * @param args           Values to capture into the payload.
     */
    template <typename ...Args>
    void enqueueLog(
        const Level level,
        const Category category,
        const std::source_location location,
        const fmt::format_string<Args ...> format,
        const bool consoleWritten,
        const std::uint64_t timestampNs,
        const std::uint32_t threadId,
        const std::uint64_t frame,
        Args && ...args
    ) noexcept
    {
        static_assert(
            (fmt::is_formattable < typename ArgTraits<Args>::Formatted > ::value &&  ...),
        "logged argument is not formattable"
        );

        const auto formatView = format.get();
        const auto takeSlot = [&] noexcept
        {
            return acquireLogSlot(
                level,
                category,
                timestampNs,
                threadId,
                frame,
                location.file_name(),
                location.function_name(),
                location.line(),
                std::string_view{formatView.data(), formatView.size()},
                consoleWritten
            );
        };

        if constexpr (sizeof...(Args) == 0)
        {
            if (takeSlot() == nullptr)
            {
                return;
            }

            commitLogSlot(&formatThunk<Args ...>, nullptr);
            return;
        }

        if (!measureArgs(args ...))
        {
            countArgumentBudget();
            return;
        }

        Record *const record = takeSlot();
        if (record == nullptr)
        {
            return;
        }

        if (!storeArgs(record->payload.data(), std::forward<Args>(args) ...))
        {
            if constexpr (!(std::is_trivially_destructible_v < typename ArgTraits<Args>::Stored > && ...))
            {
                std::size_t destroyOffset = 0;
                destroyPack<Args ...>(record->payload.data(), destroyOffset);
            }

            countArgumentBudget();
            abortLogSlot();
            return;
        }

        constexpr bool kTrivialDestroy = (std::is_trivially_destructible_v < typename ArgTraits<Args>::Stored > && ...);

        commitLogSlot(&formatThunk<Args ...>, kTrivialDestroy ? nullptr : &destroyThunk<Args ...>);
    }
} // namespace viscera::logging::detail
