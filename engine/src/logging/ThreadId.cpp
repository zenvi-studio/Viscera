#include "viscera/logging/detail/ThreadId.hpp"

#include <functional>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace viscera::logging::detail
{
    namespace
    {
        [[nodiscard]] std::uint32_t queryOsThreadId() noexcept
        {
            #ifdef _WIN32
            return static_cast<std::uint32_t>(::GetCurrentThreadId());
            #elifdef __linux__
            return static_cast<std::uint32_t>(::syscall(SYS_gettid));
            #else
            const auto hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
            return static_cast<std::uint32_t>(hash);
            #endif
        }
    } // namespace

    std::uint32_t currentThreadId() noexcept
    {
        thread_local const std::uint32_t kCached = queryOsThreadId();
        return kCached;
    }
} // namespace viscera::logging::detail
