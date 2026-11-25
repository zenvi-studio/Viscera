#include <string_view>
#include <viscera/error/ErrorType.hpp>

#include <fmt/format.h>

namespace
{
    using Output = fmt::format_context::iterator;

    [[nodiscard]] Output writeFrame(Output out, const viscera::Error &frame, const bool first, const bool codeChanged)
    {
        const std::string_view message = frame.message();
        if (!first && !codeChanged && message.empty())
        {
            return out;
        }

        if (!first)
        {
            out = fmt::format_to(out, ": ");
        }

        if (first || codeChanged)
        {
            out = fmt::format_to(out, "[{}] {}", viscera::domainName(frame.code().domain), frame.description());
            if (!message.empty())
            {
                out = fmt::format_to(out, ": {}", message);
            }

            return out;
        }

        return fmt::format_to(out, "{}", message);
    }
} // namespace

auto fmt::formatter<viscera::Error>::format(
    const viscera::Error &error,
    const format_context &context
)
    -> format_context::iterator
{
    try
    {
        auto out = context.out();
        const viscera::Error *current = &error;
        const viscera::Error *lastPrinted = nullptr;
        viscera::ErrorCode previous{};
        bool havePrevious = false;
        int printed = 0;

        while (current != nullptr && printed < viscera::kMaxErrorFormatFrames)
        {
            const viscera::ErrorCode code = current->code();
            const bool codeChanged = !havePrevious || code != previous;
            out = writeFrame(out, *current, printed == 0, codeChanged);
            previous = code;
            havePrevious = true;
            lastPrinted = current;
            current = current->cause();
            ++printed;
        }

        if (current != nullptr)
        {
            out = format_to(out, " ...");
        }

        if (lastPrinted != nullptr)
        {
            const char *const file = lastPrinted->location().file_name();
            out = format_to(out, " ({}:{})", file != nullptr ? file : "unknown", lastPrinted->location().line());
        }

        return out;
    }
    catch (...)
    {
        return fmt::format_to(context.out(), "[error] formatting failed");
    }
}
