#pragma once

#include <cstdint>

namespace viscera::logging::detail
{
    [[nodiscard]] std::uint32_t currentThreadId() noexcept;
}
