#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "Sink.hpp"

namespace viscera::logging
{
    class MemorySink : public Sink
    {
        public:
            bool write(Level level, std::string_view line) override;
            void flush() override;

            [[nodiscard]] std::string_view name() const override;

            [[nodiscard]] std::vector<std::string> lines() const;
            [[nodiscard]] std::uint64_t flushCount() const;

        private:
            mutable std::mutex m_mutex;
            std::vector<std::string> m_lines;
            std::uint64_t m_flushCount{0};
    };
} // namespace viscera::logging
