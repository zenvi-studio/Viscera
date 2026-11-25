#include <array>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <source_location>
#include <string_view>
#include <viscera/error/Assert.hpp>

#include <fmt/format.h>

#ifdef _WIN32
#include <cstdio>

#include <io.h>
#else
#include <cerrno>

#include <unistd.h>
#endif

namespace
{
    constexpr std::size_t kPanicBufferBytes = 1024;
    constexpr std::size_t kAssertMessageBytes = 512;
    constexpr std::size_t kUnsignedDigitCapacity =
        static_cast<std::size_t>(std::numeric_limits<unsigned long>::digits10) + 1U;
    constexpr std::string_view kTruncationMarker = "...\n";
    constexpr std::size_t kTruncationMarkerLength = kTruncationMarker.size();

    void writeStderr(const char *data, std::size_t size) noexcept
    {
        while (size > 0U && data != nullptr)
        {
            #ifdef _WIN32
            const unsigned int chunk = size > static_cast<std::size_t>(INT_MAX)
                ? static_cast<unsigned int>(INT_MAX)
                : static_cast<unsigned int>(size);
            #ifdef _MSC_VER
            #pragma warning(push)
            #pragma warning(disable : 4996)
            #endif
            const int written = _write(_fileno(stderr), data, chunk);
            #ifdef _MSC_VER
            #pragma warning(pop)
            #endif
            #else
            const ssize_t written = ::write(STDERR_FILENO, data, size);
            #endif
            if (written < 0)
            {
                #ifndef _WIN32
                if (errno == EINTR)
                {
                    continue;
                }
                #endif
                return;
            }

            if (written == 0)
            {
                return;
            }

            data += written;
            size -= static_cast<std::size_t>(written);
        }
    }

    void appendText(char *const buffer, const std::size_t capacity, std::size_t &used, const char *text) noexcept
    {
        if (text == nullptr || capacity == 0U)
        {
            return;
        }

        while (*text != '\0' && used + 1U < capacity)
        {
            buffer[used] = *text;
            ++used;
            ++text;
        }
    }

    void appendUnsigned(char *const buffer, const std::size_t capacity, std::size_t &used, unsigned long value) noexcept
    {
        std::array<char, kUnsignedDigitCapacity> digits{};
        std::size_t count = 0;
        do
        {
            digits.at(count) = static_cast<char>('0' + (value % 10UL));
            ++count;
            value /= 10UL;
        }
        while (value > 0UL && count < digits.size());

        while (count > 0U && used + 1U < capacity)
        {
            --count;
            buffer[used] = digits.at(count);
            ++used;
        }
    }

    void writeFallback(const std::source_location where) noexcept
    {
        std::array<char, kPanicBufferBytes> buffer{};
        std::size_t used = 0;
        appendText(buffer.data(), buffer.size(), used, "panic: diagnostic formatting failed (");
        appendText(buffer.data(), buffer.size(), used, where.file_name() != nullptr ? where.file_name() : "unknown");
        appendText(buffer.data(), buffer.size(), used, ":");
        appendUnsigned(buffer.data(), buffer.size(), used, where.line());
        appendText(buffer.data(), buffer.size(), used, ")\n");
        writeStderr(buffer.data(), used);
    }

    [[nodiscard]] bool tryFormatDiagnostic(
        char *const buffer,
        const std::size_t capacity,
        std::size_t &used,
        const char *const message,
        const std::source_location where
        ) noexcept
    {
        try
        {
            if (message == nullptr || buffer == nullptr || capacity <= kTruncationMarkerLength)
            {
                return false;
            }

            const auto [out, size] = fmt::format_to_n(
                buffer,
                capacity,
                "panic: {} ({}:{})\n",
                message,
                where.file_name() != nullptr ? where.file_name() : "unknown",
                where.line()
                );
            if (size > capacity)
            {
                std::memcpy(
                    buffer + (capacity - kTruncationMarkerLength),
                    kTruncationMarker.data(),
                    kTruncationMarkerLength
                    );
                used = capacity;
            }
            else
            {
                used = size;
            }

            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace

[[noreturn]] void viscera::panic(const char *const message, const std::source_location where) noexcept
{
    std::array<char, kPanicBufferBytes> buffer{};
    std::size_t used = 0;
    if (!tryFormatDiagnostic(buffer.data(), buffer.size(), used, message, where))
    {
        writeFallback(where);
    }
    else
    {
        writeStderr(buffer.data(), used);
    }

    std::abort();
}

[[noreturn]] void viscera::detail::assertFailed(
    const char *const condition,
    const char *const message,
    const std::source_location where
    ) noexcept
{
    std::array<char, kAssertMessageBytes> buffer{};
    std::size_t used = 0;
    appendText(buffer.data(), buffer.size(), used, "assertion failed: ");
    appendText(buffer.data(), buffer.size(), used, condition != nullptr ? condition : "unknown");
    appendText(buffer.data(), buffer.size(), used, ": ");
    appendText(buffer.data(), buffer.size(), used, message != nullptr ? message : "");
    buffer.at(used) = '\0';
    viscera::panic(buffer.data(), where);
}
