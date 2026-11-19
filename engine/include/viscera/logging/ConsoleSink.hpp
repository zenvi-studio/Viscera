#pragma once

#include <cstdio>
#include <mutex>
#include <string_view>

#include "Sink.hpp"

namespace viscera::logging
{
    class ConsoleSink : public Sink
    {
        public:
            using IsTerminal = bool (*)(std::FILE *);

            ConsoleSink();
            explicit ConsoleSink(std::FILE *file, IsTerminal isTerminal = nullptr);

            ConsoleSink(const ConsoleSink &) = delete;
            ConsoleSink &operator=(const ConsoleSink &) = delete;
            ConsoleSink(ConsoleSink &&) = delete;
            ConsoleSink &operator=(ConsoleSink &&) = delete;

            ~ConsoleSink() override = default;

            bool write(Level level, std::string_view line) override;
            void flush() override;

            [[nodiscard]] std::string_view name() const override;

            [[nodiscard]] bool tryWriteFatal(std::string_view line) noexcept;

        private:
            [[nodiscard]] bool shouldColor() const noexcept;
            [[nodiscard]] bool writeLocked(Level level, std::string_view line) noexcept;
            void enableVirtualTerminal() noexcept;

            std::FILE *m_file{stderr};
            IsTerminal m_isTerminal{nullptr};
            bool m_vtEnabled{true};
            std::mutex m_mutex;
    };
} // namespace viscera::logging
