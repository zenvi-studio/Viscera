#pragma once

#include <string_view>

#include "Sink.hpp"

namespace viscera::logging
{
    class DebuggerSink : public Sink
    {
        public:
            bool write(Level level, std::string_view line) override;
            void flush() override;

            [[nodiscard]] std::string_view name() const override;
    };
} // namespace viscera::logging
