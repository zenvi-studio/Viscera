#pragma once

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string_view>

#include "Sink.hpp"

/**
 * @file
 * @brief Sink that appends formatted lines to a file.
 */

namespace viscera::logging
{
    /**
     * @brief Sink that appends UTF-8 log lines to a file.
     *
     * Opens the given path in append-binary mode. A failed open does not
     * throw: openFailed() is true, write() returns false, and initialize()
     * starts the sink disabled so the rest of the logger still runs. Writes
     * the plain line plus a newline; level is ignored (no color). No rotation
     * in this version. The FILE is owned and closed by FileCloser.
     */
    class FileSink : public Sink
    {
        public:
            /**
             * @brief Opens @p path for append.
             *
             * On Windows uses _wfopen so the path can be wide. On other
             * platforms uses fopen. Sets openFailed() if the handle is null.
             *
             * @param path File to append to; created if it does not exist.
             */
            explicit FileSink(const std::filesystem::path &path);

            FileSink(const FileSink &) = delete;
            FileSink &operator=(const FileSink &) = delete;
            FileSink(FileSink &&) = delete;
            FileSink &operator=(FileSink &&) = delete;

            ~FileSink() override = default;

            /**
             * @brief Appends @p line and a newline to the file.
             *
             * @param level Unused; the line already contains the level name.
             * @param line  Already formatted text, without a newline.
             * @return @c true if the handle is open and both writes succeed.
             */
            bool write(Level level, std::string_view line) override;

            /**
             * @brief fflush the file if it is open.
             */
            void flush() override;

            /**
             * @brief Returns @c "file".
             */
            [[nodiscard]] std::string_view name() const override;

            /**
             * @brief True when the constructor's fopen failed.
             *
             * initialize() uses this to disable the sink and count a write
             * failure without failing the whole logger.
             */
            [[nodiscard]] bool openFailed() const noexcept;

        private:
            /**
             * @brief unique_ptr deleter that fclose's a non-null FILE*.
             */
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
