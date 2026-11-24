#include "log_format_ok.hpp"

#include <viscera/logging/Logger.hpp>

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
