#pragma once

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string_view>

#include "Sink.hpp"

namespace viscera::logging
{
    class FileSink : public Sink
    {
        public:
            explicit FileSink(const std::filesystem::path &path);

            FileSink(const FileSink &) = delete;
            FileSink &operator=(const FileSink &) = delete;
            FileSink(FileSink &&) = delete;
            FileSink &operator=(FileSink &&) = delete;

            ~FileSink() override = default;

            bool write(Level level, std::string_view line) override;
            void flush() override;

            [[nodiscard]] std::string_view name() const override;
            [[nodiscard]] bool openFailed() const noexcept;

        private:
            struct FileCloser
            {
                void operator()(std::FILE *file) const noexcept
                {
                    if (file != nullptr)
                    {
                        std::fclose(file);
                    }
                }
            };

            std::unique_ptr<std::FILE, FileCloser> m_file;
            bool m_openFailed{false};
    };
} // namespace viscera::logging
