#include <viscera/logging/Logger.hpp>

void visceraLogFormatMismatch()
{
    using viscera::logging::Category;
    viscera::logging::trace(Category::Core, "{} {}", 1);
    viscera::logging::debug(Category::Core, "{} {}", 1);
    viscera::logging::info(Category::Core, "{} {}", 1);
    viscera::logging::warning(Category::Core, "{} {}", 1);
    viscera::logging::error(Category::Core, "{} {}", 1);
    viscera::logging::fatal(Category::Core, "{} {}", 1);
    viscera::logging::log(viscera::logging::Level::Info, Category::Core, "{} {}", 1);
}
