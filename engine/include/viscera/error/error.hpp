#pragma once

/**
 * @file
 * @brief Umbrella header for viscera error handling.
 *
 * Call sites include this header. It does not include a logging header.
 * Logging headers do not include this one. The Error class lives in
 * ErrorType.hpp because this filename and Error.hpp are one path on Windows.
 */

#include "Assert.hpp"
#include "ErrorCode.hpp"
#include "ErrorDomain.hpp"
#include "ErrorType.hpp"
#include "Panic.hpp"
#include "Result.hpp"
