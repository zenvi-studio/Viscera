#include "viscera/logging/DebuggerSink.hpp"

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace viscera::logging
{
    bool DebuggerSink::write([[maybe_unused]] const Level, const std::string_view line)
    {
        #ifdef _WIN32
        std::string text;
        text.reserve(line.size() + 1U);
        text.append(line);
        text.push_back('\n');
        OutputDebugStringA(text.c_str());
        #else
        (void)line;
        #endif
        return true;
    }

    void DebuggerSink::flush() {}

    std::string_view DebuggerSink::name() const
    {
        return "debugger";
    }
} // namespace viscera::logging
