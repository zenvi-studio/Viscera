#include <memory>

#include "viscera/Engine.hpp"
#include "viscera/logging/logging.hpp"

int main()
{
    try
    {
        viscera::logging::Config config;
        config.sinks.push_back(std::make_unique<viscera::logging::ConsoleSink>());
        config.sinks.push_back(std::make_unique<viscera::logging::FileSink>("latest.log"));
        if (!viscera::logging::initialize(std::move(config)))
        {
            return 1;
        }

        viscera::logging::info(viscera::logging::Category::Core, "Engine version: {}", viscera::Engine::version());
        viscera::logging::shutdown();
        return 0;
    }
    catch (...)
    {
        return 1;
    }
}
