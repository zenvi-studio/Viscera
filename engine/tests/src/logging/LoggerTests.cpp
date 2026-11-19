#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <viscera/logging/Color.hpp>
#include <viscera/logging/ConsoleSink.hpp>
#include <viscera/logging/Logger.hpp>
#include <viscera/logging/MemorySink.hpp>

#include <fmt/format.h>
#include <gtest/gtest.h>

void visceraLogFormatControl();

using viscera::logging::Category;
using viscera::logging::clearCategoryMinLevel;
using viscera::logging::Config;
using viscera::logging::ConsoleSink;
using viscera::logging::counters;
using viscera::logging::debug;
using viscera::logging::error;
using viscera::logging::fatal;
using viscera::logging::flush;
using viscera::logging::info;
using viscera::logging::initialize;
using viscera::logging::Level;
using viscera::logging::log;
using viscera::logging::logAt;
using viscera::logging::MemorySink;
using viscera::logging::setCategoryEnabled;
using viscera::logging::setCategoryMinLevel;
using viscera::logging::setFrame;
using viscera::logging::setMinLevel;
using viscera::logging::sgrPrefix;
using viscera::logging::sgrReset;
using viscera::logging::shutdown;
using viscera::logging::Sink;
using viscera::logging::trace;
using viscera::logging::warning;
using viscera::logging::writeMaybeColored;

namespace
{
    class LoggerTest : public ::testing::Test
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

    [[nodiscard]] std::string readTextFile(std::FILE * const file)
    {
        std::array < char, 4096 > buffer{};
        if (file == nullptr || std::fseek(file, 0, SEEK_SET) != 0)
        {
            return {};
        }

        const auto read = std::fread(buffer.data(), 1, buffer.size() - 1U, file);
        return {buffer.data(), read};
    }

    struct DtorProbe
    {
        static inline int live{0};

        DtorProbe() noexcept { ++live; }

        DtorProbe(const DtorProbe &) noexcept { ++live; }

        DtorProbe(DtorProbe &&) noexcept { ++live; }

        DtorProbe &operator=(const DtorProbe &) = delete;
        DtorProbe &operator=(DtorProbe &&) = delete;

        ~DtorProbe() { --live; }
    };

    struct ThrowOnFormat {};

    struct SlowArg {};

    class FailingSink : public Sink
    {
        public:
            explicit FailingSink(const int failAfter) :
                m_failAfter(failAfter) {}

            bool write(Level, std::string_view) override
            {
                if (m_writes >= m_failAfter)
                {
                    return false;
                }

                ++m_writes;
                return true;
            }

            void flush() override {}

            [[nodiscard]] std::string_view name() const override { return "failing"; }

        private:
            int m_failAfter{0};
            int m_writes{0};
    };
} // namespace

template <>
struct fmt::formatter<DtorProbe>
{
    static constexpr auto parse(const format_parse_context &ctx) { return ctx.begin(); }

    static auto format(const DtorProbe &, format_context &ctx) { return fmt::format_to(ctx.out(), "probe"); }
};

template <>
struct fmt::formatter<ThrowOnFormat>
{
    static constexpr auto parse(const format_parse_context &ctx) { return ctx.begin(); }

    static auto format(const ThrowOnFormat &, format_context &ctx)
    {
        throw std::runtime_error("formatter boom");
        return ctx.out(); // cppcheck-suppress duplicateBreak
    }
};

template <>
struct fmt::formatter<SlowArg>
{
    static constexpr auto parse(const format_parse_context &ctx) { return ctx.begin(); }

    static auto format(const SlowArg &, format_context &ctx)
    {
        constexpr auto kDelay = std::chrono::milliseconds{50};
        std::this_thread::sleep_for(kDelay);
        return fmt::format_to(ctx.out(), "slow");
    }
};

TEST(LoggingCompileControl, ConvenienceSignaturesCompile)
{
    (void)&visceraLogFormatControl;
}

TEST(ColorTests, WrapperEmitsSgrForEveryLevelOnTerminal)
{
    for (const auto level : {Level::Trace, Level::Debug, Level::Info, Level::Warning, Level::Error, Level::Fatal})
    {
        std::string out;
        writeMaybeColored(level, "line", true, [&](const std::string_view bytes) { out.append(bytes); });
        EXPECT_EQ(out, std::string(sgrPrefix(level)) + "line" + std::string(sgrReset()));
        EXPECT_FALSE(sgrPrefix(level).empty());
        EXPECT_EQ(sgrPrefix(level).front(), '\033');
    }
}

TEST(ColorTests, WrapperIsPlainOffTerminal)
{
    std::string out;
    writeMaybeColored(Level::Error, "plain", false, [&](const std::string_view bytes) { out.append(bytes); });
    EXPECT_EQ(out, "plain");
    EXPECT_EQ(out.find('\033'), std::string::npos);
}

TEST_F(LoggerTest, SourceLocationIsCallSiteForEveryEntry)
{
    const auto logLine = static_cast<std::uint32_t>(__LINE__ + 1);
    log(Level::Info, Category::Core, "via log");
    const auto traceLine = static_cast<std::uint32_t>(__LINE__ + 1);
    trace(Category::Core, "via trace");
    const auto debugLine = static_cast<std::uint32_t>(__LINE__ + 1);
    debug(Category::Core, "via debug");
    const auto infoLine = static_cast<std::uint32_t>(__LINE__ + 1);
    info(Category::Core, "via info");
    const auto warningLine = static_cast<std::uint32_t>(__LINE__ + 1);
    warning(Category::Core, "via warning");
    const auto errorLine = static_cast<std::uint32_t>(__LINE__ + 1);
    error(Category::Core, "via error");
    const auto fatalLine = static_cast<std::uint32_t>(__LINE__ + 1);
    fatal(Category::Core, "via fatal");

    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 7U);
    EXPECT_TRUE(lines.at(0).contains(fmt::format(":{}", logLine)));
    EXPECT_TRUE(lines.at(1).contains(fmt::format(":{}", traceLine)));
    EXPECT_TRUE(lines.at(2).contains(fmt::format(":{}", debugLine)));
    EXPECT_TRUE(lines.at(3).contains(fmt::format(":{}", infoLine)));
    EXPECT_TRUE(lines.at(4).contains(fmt::format(":{}", warningLine)));
    EXPECT_TRUE(lines.at(5).contains(fmt::format(":{}", errorLine)));
    EXPECT_TRUE(lines.at(6).contains(fmt::format(":{}", fatalLine)));
    EXPECT_TRUE(lines.at(0).contains("LoggerTests.cpp"));
}

TEST_F(LoggerTest, LevelBelowMinimumIsDropped)
{
    setMinLevel(Level::Error);
    info(Category::Core, "nope");
    EXPECT_TRUE(flushedLines().empty());
    error(Category::Core, "yes");
    EXPECT_EQ(flushedLines().size(), 1U);
}

TEST_F(LoggerTest, DisabledCategoryIsDropped)
{
    setCategoryEnabled(Category::Audio, false);
    info(Category::Audio, "silent");
    EXPECT_TRUE(flushedLines().empty());
    info(Category::Core, "loud");
    EXPECT_EQ(flushedLines().size(), 1U);
}

TEST_F(LoggerTest, CategoryMinLevelAffectsOnlyThatCategory)
{
    setCategoryMinLevel(Category::Render, Level::Error);
    info(Category::Render, "skip");
    info(Category::Core, "keep");
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines.at(0).contains("[core]"));
}

TEST_F(LoggerTest, ClearCategoryMinLevelFollowsLaterGlobalMin)
{
    setCategoryMinLevel(Category::Render, Level::Error);
    clearCategoryMinLevel(Category::Render);
    setMinLevel(Level::Warning);
    info(Category::Render, "skip");
    warning(Category::Render, "keep");
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines.at(0).contains("keep"));
}

TEST_F(LoggerTest, TwoThreadsPreservePerThreadOrder)
{
    shutdown();
    auto sink = std::make_unique<MemorySink>();
    m_sink = sink.get();
    Config config;
    config.queueCapacity = 1024;
    config.sinks.push_back(std::move(sink));
    ASSERT_TRUE(initialize(std::move(config)));

    constexpr int kCount = 40;
    std::thread first{
        []
        {
            for (int index = 0; index < kCount; ++index)
            {
                info(Category::Core, "A{}", index);
            }
        }
    };
    std::thread second{
        []
        {
            for (int index = 0; index < kCount; ++index)
            {
                info(Category::Game, "B{}", index);
            }
        }
    };
    first.join();
    second.join();

    const auto lines = flushedLines();
    std::vector<int> a;
    std::vector<int> b;
    for (const auto &line : lines)
    {
        if (line.contains("] A"))
        {
            a.push_back(std::stoi(line.substr(line.rfind('A') + 1)));
        }
        else if (line.contains("] B"))
        {
            b.push_back(std::stoi(line.substr(line.rfind('B') + 1)));
        }
    }

    ASSERT_EQ(a.size(), static_cast<std::size_t>(kCount));
    ASSERT_EQ(b.size(), static_cast<std::size_t>(kCount));
    for (int index = 0; index < kCount; ++index)
    {
        EXPECT_EQ(a.at(static_cast<std::size_t>(index)), index);
        EXPECT_EQ(b.at(static_cast<std::size_t>(index)), index);
    }
}

TEST_F(LoggerTest, OverflowDropsNewRecordsAndWritesOneSummary)
{
    shutdown();
    auto sink = std::make_unique<MemorySink>();
    m_sink = sink.get();
    Config config;
    config.queueCapacity = 1;
    config.sinks.push_back(std::move(sink));
    ASSERT_TRUE(initialize(std::move(config)));

    info(Category::Core, "{}", SlowArg{});
    for (int index = 0; index < 8; ++index)
    {
        info(Category::Core, "fast{}", index);
    }

    const auto lines = flushedLines();
    EXPECT_GE(counters().overflow, 1U);
    const auto summaries =
        std::count_if(lines.begin(), lines.end(), [](const std::string &line) { return line.contains("dropped "); });
    EXPECT_EQ(summaries, 1);
}

TEST_F(LoggerTest, OversizedArgumentsIncrementBudgetAndWriteNothing)
{
    constexpr std::size_t kOversizeBytes = 300;
    const std::string huge(kOversizeBytes, 'x');
    info(Category::Core, "{}", huge);
    EXPECT_TRUE(flushedLines().empty());
    EXPECT_EQ(counters().argumentBudget, 1U);
}

TEST_F(LoggerTest, UnterminatedCStringIncrementsBudget)
{
    constexpr std::size_t kOversizeBytes = 300;
    std::vector<char> buffer(kOversizeBytes, 'A');
    info(Category::Core, "{}", buffer.data());
    EXPECT_TRUE(flushedLines().empty());
    EXPECT_EQ(counters().argumentBudget, 1U);
}

TEST_F(LoggerTest, CopiedStringSurvivesCallerOverwrite)
{
    std::string text{"secret"};
    info(Category::Core, "{}", text.c_str());
    text.assign(text.size(), 'x');
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines.at(0).contains("secret"));
    EXPECT_FALSE(lines.at(0).contains("xxxxxx"));
}

TEST_F(LoggerTest, PointerIsNotDereferenced)
{
    constexpr int kPointerSentinel = 42;
    int value = kPointerSentinel;
    info(Category::Core, "{}", static_cast<const void*>(&value));
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_FALSE(lines.at(0).contains("42"));
}

TEST_F(LoggerTest, UserTypeDestroyedOnceAfterFormat)
{
    DtorProbe::live = 0;
    {
        const DtorProbe probe;
        info(Category::Core, "{}", probe);
        flush();
    }

    EXPECT_EQ(DtorProbe::live, 0);
    info(Category::Core, "reuse");
    EXPECT_EQ(flushedLines().size(), 2U);
}

TEST_F(LoggerTest, ThrowingFormatterWritesDiagnosticAndContinues)
{
    info(Category::Core, "{}", ThrowOnFormat{});
    info(Category::Core, "after");
    const auto lines = flushedLines();
    ASSERT_GE(lines.size(), 2U);
    EXPECT_TRUE(lines.at(0).contains("formatting failed"));
    EXPECT_TRUE(lines.back().contains("after"));
    EXPECT_EQ(counters().formattingFailures, 1U);
}

TEST_F(LoggerTest, FatalPathsAttemptDirectConsoleWrite)
{
    shutdown();
    const std::unique_ptr<std::FILE, int (*)(std::FILE *)> file{std::tmpfile(), &std::fclose};
    ASSERT_NE(file.get(), nullptr);
    auto console = std::make_unique<ConsoleSink>(file.get(), [](std::FILE *) { return false; });
    auto memory = std::make_unique<MemorySink>();
    m_sink = memory.get();
    Config config;
    config.sinks.push_back(std::move(console));
    config.sinks.push_back(std::move(memory));
    ASSERT_TRUE(initialize(std::move(config)));

    fatal(Category::Core, "fatal-fn");
    log(Level::Fatal, Category::Core, "fatal-log");
    logAt(Level::Fatal, Category::Core, std::source_location::current(), "fatal-at");
    flush();

    const auto text = readTextFile(file.get());
    EXPECT_TRUE(text.contains("fatal-fn"));
    EXPECT_TRUE(text.contains("fatal-log"));
    EXPECT_TRUE(text.contains("fatal-at"));

    const auto lines = m_sink->lines();
    EXPECT_GE(lines.size(), 3U);

    shutdown();
    m_sink = nullptr;
}

TEST_F(LoggerTest, SecondInitializeLeavesFirstSession)
{
    info(Category::Core, "first");
    Config config;
    EXPECT_FALSE(initialize(std::move(config)));
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines.at(0).contains("first"));
}

TEST(LoggerLifetime, NotRunningBeforeAndAfter)
{
    viscera::logging::detail::setContractAssertsEnabled(false);
    shutdown();
    info(Category::Core, "before");
    EXPECT_EQ(counters().notRunning, 1U);

    auto sink = std::make_unique<MemorySink>();
    const MemorySink *const raw = sink.get();
    Config config;
    config.sinks.push_back(std::move(sink));
    ASSERT_TRUE(initialize(std::move(config)));
    info(Category::Core, "during");
    flush();
    EXPECT_EQ(raw->lines().size(), 1U);
    shutdown();

    info(Category::Core, "after");
    EXPECT_GE(counters().notRunning, 1U);
    viscera::logging::detail::setContractAssertsEnabled(true);
}

TEST_F(LoggerTest, ShutdownDoesNotReturnWhileRecordUnflushed)
{
    shutdown();
    const auto written = std::make_shared<std::atomic<bool>>(false);

    class FlagSink : public Sink
    {
        public:
            explicit FlagSink(std::shared_ptr<std::atomic<bool>> flag) :
                m_flag(std::move(flag)) {}

            bool write(Level, std::string_view) override
            {
                m_flag->store(true);
                return true;
            }

            void flush() override {}

            [[nodiscard]] std::string_view name() const override { return "flag"; }

        private:
            std::shared_ptr<std::atomic<bool>> m_flag;
    };

    Config config;
    config.sinks.push_back(std::make_unique<FlagSink>(written));
    ASSERT_TRUE(initialize(std::move(config)));
    info(Category::Core, "drain-me");
    shutdown();
    EXPECT_TRUE(written->load());
    m_sink = nullptr;
}

TEST_F(LoggerTest, SetFrameAppearsOnSameThread)
{
    constexpr std::uint64_t kTestFrame = 42;
    setFrame(kTestFrame);
    info(Category::Core, "framed");
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines.at(0).contains("][42]["));
}

TEST_F(LoggerTest, CountersSnapshotIncludesExpectedFields)
{
    setMinLevel(Level::Error);
    info(Category::Core, "dropped-by-filter");
    const auto snap = counters();
    EXPECT_EQ(snap.overflow, 0U);
    EXPECT_EQ(snap.argumentBudget, 0U);
    EXPECT_EQ(snap.notRunning, 0U);
    EXPECT_EQ(snap.formattingFailures, 0U);
    EXPECT_EQ(snap.flushTimeouts, 0U);
    ASSERT_FALSE(snap.sinkWriteFailures.empty());
}

TEST_F(LoggerTest, ThreeWriteFailuresDisableSink)
{
    shutdown();
    auto failing = std::make_unique<FailingSink>(0);
    auto memory = std::make_unique<MemorySink>();
    m_sink = memory.get();
    Config config;
    config.sinks.push_back(std::move(failing));
    config.sinks.push_back(std::move(memory));
    ASSERT_TRUE(initialize(std::move(config)));

    info(Category::Core, "one");
    info(Category::Core, "two");
    info(Category::Core, "three");
    info(Category::Core, "four");
    const auto lines = flushedLines();
    bool sawDisable = false;
    for (const auto &line : lines)
    {
        if (line.contains("disabled after"))
        {
            sawDisable = true;
        }
    }

    EXPECT_TRUE(sawDisable);
    EXPECT_GE(counters().sinkWriteFailures.front(), 3U);
}

TEST_F(LoggerTest, FileOpenFailureStartsDisabled)
{
    shutdown();
    auto memory = std::make_unique<MemorySink>();
    m_sink = memory.get();
    Config config;
    config.filePath = std::filesystem::path("Z:") / "viscera_missing_dir" / "nope.log";
    config.sinks.push_back(std::move(memory));
    ASSERT_TRUE(initialize(std::move(config)));
    info(Category::Core, "still-works");
    EXPECT_EQ(flushedLines().size(), 1U);
    EXPECT_GE(counters().sinkWriteFailures.front(), 1U);
}

TEST_F(LoggerTest, NullCStringBecomesToken)
{
    const char *text = nullptr;
    info(Category::Core, "{}", text);
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines.at(0).contains("(null)"));
}

TEST_F(LoggerTest, LinePatternUsesLowercaseNames)
{
    info(Category::Render, "msg");
    const auto lines = flushedLines();
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines.at(0).contains("[info]"));
    EXPECT_TRUE(lines.at(0).contains("[render]"));
    EXPECT_EQ(lines.at(0).find('\033'), std::string::npos);
}
