#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "Record.hpp"

namespace viscera::logging::detail
{
    class Ring
    {
        public:
            void reset(std::size_t capacity);
            void clear() noexcept;

            [[nodiscard]] std::mutex &mutex() noexcept { return m_mutex; }

            [[nodiscard]] std::condition_variable &cv() noexcept { return m_cv; }

            void notifyOne() noexcept;
            void notifyAll() noexcept;

            [[nodiscard]] bool empty() const noexcept;
            [[nodiscard]] bool full() const noexcept;
            [[nodiscard]] std::uint64_t occupied() const noexcept;

            [[nodiscard]] Record *writeSlot() noexcept;
            void publish() noexcept;

            [[nodiscard]] Record *readSlot() noexcept;
            void consume() noexcept;

        private:
            std::mutex m_mutex;
            std::condition_variable m_cv;
            std::vector<Record> m_slots;
            std::uint64_t m_capacity{0};
            std::uint64_t m_mask{0};
            std::uint64_t m_write{0};
            std::uint64_t m_read{0};
    };
} // namespace viscera::logging::detail
