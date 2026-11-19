#include "viscera/logging/detail/Ring.hpp"

namespace viscera::logging::detail
{
    void Ring::reset(const std::size_t capacity)
    {
        m_slots.clear();
        m_slots.resize(capacity);
        m_capacity = capacity;
        m_mask = capacity == 0 ? 0 : capacity - 1U;
        m_write = 0;
        m_read = 0;
    }

    void Ring::clear() noexcept
    {
        m_slots.clear();
        m_slots.shrink_to_fit();
        m_capacity = 0;
        m_mask = 0;
        m_write = 0;
        m_read = 0;
    }

    void Ring::notifyOne() noexcept
    {
        m_cv.notify_one();
    }

    void Ring::notifyAll() noexcept
    {
        m_cv.notify_all();
    }

    bool Ring::empty() const noexcept
    {
        return m_write == m_read;
    }

    bool Ring::full() const noexcept
    {
        return (m_write - m_read) == m_capacity;
    }

    std::uint64_t Ring::occupied() const noexcept
    {
        return m_write - m_read;
    }

    Record *Ring::writeSlot() noexcept
    {
        return &m_slots.at(static_cast<std::size_t>(m_write & m_mask));
    }

    void Ring::publish() noexcept
    {
        ++m_write;
    }

    Record *Ring::readSlot() noexcept
    {
        return &m_slots.at(static_cast<std::size_t>(m_read & m_mask));
    }

    void Ring::consume() noexcept
    {
        ++m_read;
    }
} // namespace viscera::logging::detail
