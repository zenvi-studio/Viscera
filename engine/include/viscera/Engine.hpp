#pragma once
#include <string_view>

namespace viscera
{
    class Engine
    {
        public:
            [[nodiscard]] static std::string_view version() noexcept;
    };
}
