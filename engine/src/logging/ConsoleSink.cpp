#include "viscera/logging/ConsoleSink.hpp"

#include <cstdio>

#include "viscera/logging/Color.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace viscera::logging
{
    namespace
    {
        [[nodiscard]] bool defaultIsTerminal(std::FILE * const file)
        noexcept
        {
            if (file == nullptr)
            {
                return false;
            }

            #ifdef _WIN32
            return _isatty (_fileno(file)) != 0;
            #else
            return ::isatty(::fileno(file)) != 0;
            #endif
        }

        [[nodiscard]] bool writeBytes(std::FILE * const file, const std::string_view bytes)
        noexcept
        {
            if (bytes.empty())
            {
                return true;
            }

            return std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
        }
    } // namespace

    ConsoleSink::ConsoleSink() :
        ConsoleSink(stderr, nullptr) {}

    ConsoleSink::ConsoleSink(std::FILE * const file, const IsTerminal isTerminal) :
        m_file(file == nullptr ? stderr : file),
        m_isTerminal(isTerminal)
    {
        enableVirtualTerminal();
    }

    bool ConsoleSink::write(const Level level, const std::string_view line)
    {
        const std::scoped_lock lock{m_mutex};
        return writeLocked(level, line);
    }

    void ConsoleSink::flush()
    {
        const std::scoped_lock lock{m_mutex};
        if (m_file != nullptr)
        {
            std::fflush(m_file);
        }
    }

    std::string_view ConsoleSink::name() const
    {
        return "console";
    }

    bool ConsoleSink::tryWriteFatal(const std::string_view line) noexcept
    {
        if (!m_mutex.try_lock())
        {
            return false;
        }

        const bool written = writeLocked(Level::Fatal, line);
        m_mutex.unlock();
        return written;
    }

    bool ConsoleSink::shouldColor() const noexcept
    {
        const bool terminal = m_isTerminal != nullptr ? m_isTerminal(m_file) : defaultIsTerminal(m_file);
        return terminal && m_vtEnabled;
    }

    bool ConsoleSink::writeLocked(const Level level, const std::string_view line) noexcept
    {
        if (m_file == nullptr)
        {
            return false;
        }

        bool ok = true;
        writeMaybeColored(
            level,
            line,
            shouldColor(),
            [&](const std::string_view bytes) { ok = ok && writeBytes(m_file, bytes); }
        );

        return ok && std::fputc('\n', m_file) != EOF;
    }

    void ConsoleSink::enableVirtualTerminal() noexcept
    {
        #ifdef _WIN32
        m_vtEnabled = false;
        if (m_file != stderr && m_file != stdout)
        {
            m_vtEnabled = true;
            return;
        }

        HANDLE handle = GetStdHandle(m_file == stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode) == FALSE)
        {
            return;
        }

        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        m_vtEnabled = SetConsoleMode(handle, mode) != FALSE;
        #else
        m_vtEnabled = true;
        #endif
    }
} // namespace viscera::logging
