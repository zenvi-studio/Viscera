#define VISCERA_LOG_MIN_LEVEL 2

#include <memory>
#include <viscera/logging/Logger.hpp>
#include <viscera/logging/MemorySink.hpp>

#include <gtest/gtest.h>

using viscera::logging::Category;
using viscera::logging::Config;
using viscera::logging::debug;
using viscera::logging::flush;
using viscera::logging::info;
using viscera::logging::initialize;
using viscera::logging::Level;
using viscera::logging::log;
using viscera::logging::MemorySink;
using viscera::logging::shutdown;
using viscera::logging::trace;

TEST(LogFloorInfo, TraceAndDebugDoNotEnqueue)
{
    viscera::logging::detail::setContractAssertsEnabled(false);
    shutdown();
    auto sink = std::make_unique<MemorySink>();
    const MemorySink *const raw = sink.get();
    Config config;
    config.sinks.push_back(std::move(sink));
    ASSERT_TRUE(initialize(std::move(config)));

    int evaluated = 0;
    const auto bump = [&evaluated]
    {
        ++evaluated;
        return 1;
    };

    trace(Category::Core, "{}", bump());
    debug(Category::Core, "{}", bump());
    log(Level::Debug, Category::Core, "{}", bump());
    flush();
    EXPECT_TRUE(raw->lines().empty());
    EXPECT_EQ(evaluated, 3);

    info(Category::Core, "kept");
    flush();
    ASSERT_EQ(raw->lines().size(), 1U);

    shutdown();
    viscera::logging::detail::setContractAssertsEnabled(true);
}
