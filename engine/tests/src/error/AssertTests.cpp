#include <viscera/error/error.hpp>

#include <gtest/gtest.h>

#include "DeathTest.hpp"

namespace
{
    class AssertDeath : public viscera::error_tests::DeathTest {};
} // namespace

#ifdef VISCERA_ENABLE_ASSERTS
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(AssertDeath, FailingAssertAborts)
{
    EXPECT_DEATH(VISCERA_ASSERT(false, "broken invariant"), "broken invariant");
}
#endif
