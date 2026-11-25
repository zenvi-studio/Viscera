#include "viscera/logging/FileSink.hpp"

#include <utility>

namespace viscera::logging
{
    FileSink::FileSink(const std::filesystem::path &path)
    {
        #ifdef _MSC_VER
        // _wfopen is deprecated under /WX (C4996). _wfopen_s is the same open.
        std::FILE *file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"ab") != 0)
        {
            file = nullptr;
        }
        m_file.reset(file);
        #elifdef _WIN32
        m_file.reset(_wfopen(path.c_str(), L"ab"));
        #else
        m_file.reset(std::fopen(path.c_str(), "ab"));
        #endif
        m_openFailed = m_file == nullptr;
    }

    bool FileSink::write([[maybe_unused]] const Level, const std::string_view line)
    {
        if (m_file == nullptr)
        {
            return false;
        }

        if (std::fwrite(line.data(), 1, line.size(), m_file.get()) != line.size())
        {
            return false;
        }

        return std::fputc('\n', m_file.get()) != EOF;
    }

    void FileSink::flush()
    {
        if (m_file != nullptr)
        {
            std::fflush(m_file.get());
        }
    }

    std::string_view FileSink::name() const
    {
        return "file";
    }

    bool FileSink::openFailed() const noexcept
    {
        return m_openFailed;
    }
} // namespace viscera::logging
