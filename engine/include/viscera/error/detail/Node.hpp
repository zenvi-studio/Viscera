#pragma once

#include <optional>
#include <string>

// Internal to Error. Include ErrorType.hpp (or the umbrella), not this file.
// Error methods need a complete Node; Node stores optional<Error>. ErrorType.hpp
// includes this header after class Error is defined.

namespace viscera
{
    class Error;

    namespace detail
    {
        /// @cond
        /**
         * Immutable context frame. cause is empty when this frame is a root
         * that still has a message. Frame location lives on Error.
         */
        struct Node
        {
            std::string message;
            std::optional<Error> cause;

            Node(std::string messageIn, std::optional<Error> causeIn) noexcept;
        };

        /// @endcond
    } // namespace detail
}     // namespace viscera
