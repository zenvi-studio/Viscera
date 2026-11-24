#pragma once

/**
 * @file
 * @brief Umbrella header for viscera::logging.
 *
 * Call sites include this header only. They do not include fmt headers
 * unless they define an fmt::formatter for a logged type. DebuggerSink is
 * included only when VISCERA_LOG_DEBUG_SINKS is on (Debug and RelWithDebInfo
 * by default).
 */

#include "Color.hpp"
#include "ConsoleSink.hpp"
#include "FileSink.hpp"
#include "Logger.hpp"
#include "MemorySink.hpp"
#include "Sink.hpp"
#include "Types.hpp"

#ifdef VISCERA_LOG_DEBUG_SINKS
#include "DebuggerSink.hpp"
#endif
