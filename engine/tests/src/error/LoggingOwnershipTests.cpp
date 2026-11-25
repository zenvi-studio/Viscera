#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <viscera/error/error.hpp>
#include <viscera/logging/Logger.hpp>
#include <viscera/logging/MemorySink.hpp>

#include <gtest/gtest.h>

#include "SampleError.hpp"

using viscera::Error;
using viscera::fail;
using viscera::Result;
using viscera::error_tests::SampleError;
using viscera::logging::Category;
using viscera::logging::Config;
using viscera::logging::counters;
using viscera::logging::fatal;
using viscera::logging::flush;
using viscera::logging::initialize;
using viscera::logging::MemorySink;
using viscera::logging::shutdown;
using viscera::logging::warning;

namespace
{
    class ErrorLogTest : public testing::Test
    {
        protected:
            MemorySink *m_sink{nullptr};

            void SetUp() override
            {
                viscera::logging::detail::setContractAssertsEnabled(false);
                shutdown();
                auto sink = std::make_unique<MemorySink>();
                m_sink = sink.get();
                Config config;
                config.queueCapacity = 16;
                config.sinks.push_back(std::move(sink));
                ASSERT_TRUE(initialize(std::move(config)));
            }

            void TearDown() override
            {
                shutdown();
                m_sink = nullptr;
                viscera::logging::detail::setContractAssertsEnabled(true);
            }

            [[nodiscard]] std::vector<std::string> flushedLines() const
            {
                flush();
                return m_sink->lines();
            }
    };

    [[nodiscard]] Result<int> readFile()
    {
        return fail(SampleError::Missing, "open '{}' failed", "a.png");
    }

    [[nodiscard]] Result<int> loadTexture()
    {
        auto bytes = readFile();
        if (!bytes)
        {
            return std::unexpected{std::move(bytes).error().wrap(SampleError::Broken, "texture '{}'", "a.png")};
        }

        return *bytes;
    }

    [[nodiscard]] Result<int> loadLevel()
    {
        auto texture = loadTexture();
        if (!texture)
        {
            return std::unexpected{std::move(texture).error().withContext("loading level '{}'", "ship")};
        }

        return *texture;
    }
} // namespace

TEST_F(ErrorLogTest, CreationAndPropagationDoNotLog)
{
    const auto result = loadLevel();
    ASSERT_FALSE(result);
    EXPECT_TRUE(flushedLines().empty());
    EXPECT_EQ(counters().argumentBudget, 0U);
}

TEST_F(ErrorLogTest, HandlerLogsOnceAndCaptureDoesNotMutate)
{
    auto result = loadLevel();
    ASSERT_FALSE(result);
    const Error error = result.error();
    const char *const message = error.message().data();
    const Error *const cause = error.cause();

    warning(Category::Asset, "using placeholder: {}", error);
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines.at(0).contains("[warning]"));
    EXPECT_TRUE(lines.at(0).contains("using placeholder"));
    EXPECT_TRUE(lines.at(0).contains("loading level 'ship'"));
    EXPECT_TRUE(lines.at(0).contains("texture 'a.png'"));
    EXPECT_TRUE(lines.at(0).contains("[core] missing thing"));
    EXPECT_EQ(error.message().data(), message);
    EXPECT_EQ(error.cause(), cause);
    EXPECT_EQ(counters().argumentBudget, 0U);
    EXPECT_EQ(counters().formattingFailures, 0U);
}

TEST_F(ErrorLogTest, TopLevelHandlerLogsOneFatal)
{
    auto result = loadLevel();
    ASSERT_FALSE(result);
    fatal(Category::Core, "level load failed: {}", result.error());
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines.at(0).contains("[fatal]"));
    EXPECT_TRUE(lines.at(0).contains("level load failed"));
    EXPECT_EQ(counters().formattingFailures, 0U);
}

TEST_F(ErrorLogTest, WorkerReturnsTheErrorAndConsumerLogsOnce)
{
    std::promise<Error> promise;
    std::thread worker{[&promise] { promise.set_value(fail(SampleError::Missing, "from worker").error()); }};
    worker.join();
    const Error error = promise.get_future().get();
    EXPECT_TRUE(flushedLines().empty());

    warning(Category::Core, "handled: {}", error);
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines.at(0).contains("from worker"));
    EXPECT_TRUE(lines.at(0).contains("handled"));
}
