#include "viscera/Engine.hpp"

#include "viscera/Version.hpp"

namespace viscera
{
    std::string_view Engine::version() noexcept
    {
        return kEngineVersion;
    }
}
