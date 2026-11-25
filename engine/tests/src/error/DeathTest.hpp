#pragma once

#include <gtest/gtest.h>

namespace viscera::error_tests
{
    class DeathTest : public testing::Test
    {
        protected:
            void SetUp() override { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
    };
} // namespace viscera::error_tests
