#undef VISCERA_ENABLE_ASSERTS

#include <viscera/error/Assert.hpp>

#include <gtest/gtest.h>

TEST(AssertTests, DisabledAssertDoesNotEvaluateCondition)
{
    // Non-const so ++value is valid if the assert were accidentally evaluated.
    // NOLINTNEXTLINE(misc-const-correctness)
    int value = 0;
    VISCERA_ASSERT(++value, "side effect");
    EXPECT_EQ(value, 0);
}
