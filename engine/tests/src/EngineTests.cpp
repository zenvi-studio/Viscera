#include <viscera/Engine.hpp>

#include <gtest/gtest.h>

TEST(EngineTests, ReturnsCorrectVersion)
{
    EXPECT_EQ(viscera::Engine::version(), "0.0.1");
}
