#include "viscera/logging/MemorySink.hpp"

namespace viscera::logging
{
    bool MemorySink::write(const Level, const std::string_view line)
    {
        const std::scoped_lock lock{m_mutex};
        m_lines.emplace_back(line);
        return true;
    }

    void MemorySink::flush()
    {
        const std::scoped_lock lock{m_mutex};
        ++m_flushCount;
    }

    std::string_view MemorySink::name() const
    {
        return "memory";
    }

    std::vector<std::string> MemorySink::lines() const
    {
        const std::scoped_lock lock{m_mutex};
        return m_lines;
    }

    std::uint64_t MemorySink::flushCount() const
    {
        const std::scoped_lock lock{m_mutex};
        return m_flushCount;
    }
} // namespace viscera::logging
