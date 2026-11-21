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

namespace viscera::logging::detail
{
    [[nodiscard]] constexpr std::size_t alignUp(const std::size_t value, const std::size_t alignment) noexcept
    {
        return (value + alignment - 1U) & ~(alignment - 1U);
    }

    template <typename T, bool IsEnum = std::is_enum_v<T>>
    struct StoredEnum
    {
        using Type = T;
    };

    template <typename T>
    struct StoredEnum<T, true>
    {
        using Type = std::underlying_type_t<T>;
    };

    template <typename T>
    struct ArgTraits
    {
        using Decayed = std::decay_t<T>;

        static constexpr bool kIsString = std::is_same_v<Decayed, char*> ||
            std::is_same_v<Decayed, const char*> || std::is_same_v<Decayed, std::string> ||
            std::is_same_v<Decayed, std::string_view>;

        static constexpr bool kIsEnum = std::is_enum_v<Decayed>;
        static constexpr bool kIsPointer = std::is_pointer_v<Decayed> && !kIsString;

        using Stored = std::conditional_t<
            kIsString,
            CapturedString,
            std::conditional_t < kIsPointer, const void*, typename StoredEnum<Decayed>::Type>
        >;

        using Formatted = std::conditional_t<kIsString, std::string_view, Stored>;
    };

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

    template <typename T>
    [[nodiscard]] bool storeCString(std::byte * const payload, std::size_t & offset, const T * const value)
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
                });
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
            });
        offset += sizeof(CapturedString) + length;
        return true;
    }

    template <typename T>
    [[nodiscard]] bool storeArg(std::byte * const payload, std::size_t & offset, T && value)
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
                    });
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

    [[nodiscard]] constexpr bool measureArgsFrom(std::size_t) noexcept
    {
        return true;
    }

    template <typename T, typename ...Rest>
    [[nodiscard]] bool measureArgsFrom(std::size_t offset, const T &value, const Rest & ...rest) noexcept
    {
        if (!measureArg(offset, value))
        {
            return false;
        }

        return measureArgsFrom(offset, rest ...);
    }

    template <typename ...Args>
    [[nodiscard]] bool measureArgs(const Args & ...args) noexcept
    {
        return measureArgsFrom(0, args ...);
    }

    [[nodiscard]] constexpr bool storeArgsFrom(std::byte *, std::size_t) noexcept
    {
        return true;
    }

    template <typename T, typename ...Rest>
    [[nodiscard]] bool storeArgsFrom(std::byte * const payload, std::size_t offset, T && value, Rest && ...rest)
    noexcept
    {
        if (!storeArg(payload, offset, std::forward<T>(value)))
        {
            return false;
        }

        return storeArgsFrom(payload, offset, std::forward<Rest>(rest)...);
    }

    template <typename ...Args>
    [[nodiscard]] bool storeArgs(std::byte * const payload, Args && ...args)
    noexcept
    {
        return storeArgsFrom(payload, 0, std::forward<Args>(args)...);
    }

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

    template <typename First, typename ...Rest>
    void destroyPack(std::byte * const payload, std::size_t & offset)
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
            destroyPack<Rest...>(payload, restOffset);
        }

        std::destroy_at(value);
        offset = next;
    }

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

    template <typename ...Args>
    void destroyThunk(Record &record)
    {
        if constexpr (sizeof...(Args) > 0)
        {
            std::size_t offset = 0;
            destroyPack<Args ...>(record.payload.data(), offset);
        }
    }

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
        "logged argument is not formattable");

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
