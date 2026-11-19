#include <viscera/logging/Logger.hpp>

// Linked from LoggerTests to prove convenience signatures compile; must stay external.
// NOLINTNEXTLINE(misc-use-internal-linkage)
void visceraLogFormatControl()
{
    using viscera::logging::Category;
    viscera::logging::trace(Category::Core, "ok {}", 1);
    viscera::logging::debug(Category::Core, "ok {}", 1);
    viscera::logging::info(Category::Core, "ok {}", 1);
    viscera::logging::warning(Category::Core, "ok {}", 1);
    viscera::logging::error(Category::Core, "ok {}", 1);
    viscera::logging::fatal(Category::Core, "ok {}", 1);
    viscera::logging::log(viscera::logging::Level::Info, Category::Core, "ok {}", 1);
}
