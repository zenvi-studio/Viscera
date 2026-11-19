#pragma once

#include <atomic>
#include <string_view>

#include "Types.hpp"

namespace viscera::logging
{
    class Sink
    {
        public:
            virtual ~Sink() = default;

            virtual bool write(Level level, std::string_view line) = 0;
            virtual void flush() = 0;

            [[nodiscard]] virtual std::string_view name() const = 0;

            void setEnabled(const bool isEnabled) noexcept { m_enabled.store(isEnabled, std::memory_order_relaxed); }

            [[nodiscard]] bool enabled() const noexcept { return m_enabled.load(std::memory_order_relaxed); }

        private:
            std::atomic<bool> m_enabled{true};
    };
} // namespace viscera::logging
