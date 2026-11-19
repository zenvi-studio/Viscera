#pragma once

#include <cstdint>
#include <source_location>
#include <string_view>

#include "Record.hpp"
#include "viscera/logging/Types.hpp"

namespace viscera::logging::detail
{
    void setContractAssertsEnabled(bool enabled) noexcept;

    [[nodiscard]] bool loggerRunning() noexcept;
    void countNotRunning() noexcept;
    void countArgumentBudget() noexcept;

    [[nodiscard]] bool shouldLog(Level level, Category category) noexcept;
    [[nodiscard]] std::uint64_t sampleTimestampNs() noexcept;
    [[nodiscard]] std::uint32_t sampleThreadId() noexcept;
    [[nodiscard]] std::uint64_t sampleFrame() noexcept;
    [[nodiscard]] std::string_view sourceRootPrefix() noexcept;
    [[nodiscard]] bool tryWriteFatalConsole(std::string_view line) noexcept;

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

    void commitLogSlot(FormatThunk formatFn, DestroyThunk destroyFn) noexcept;
    void abortLogSlot() noexcept;
} // namespace viscera::logging::detail
