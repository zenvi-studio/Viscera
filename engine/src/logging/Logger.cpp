#include "viscera/logging/Logger.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "viscera/logging/ConsoleSink.hpp"
#include "viscera/logging/FileSink.hpp"
#include "viscera/logging/detail/Line.hpp"
#include "viscera/logging/detail/Ring.hpp"
#include "viscera/logging/detail/Runtime.hpp"
#include "viscera/logging/detail/ThreadId.hpp"

namespace viscera::logging
{
    namespace
    {
        [[nodiscard]] std::atomic<bool> &contractAssertsFlag() noexcept
        {
            static std::atomic<bool> enabled{true};
            return enabled;
        }

        void contractAssert(const bool condition, const char *const message) noexcept
        {
            #ifndef NDEBUG
            if (!condition && contractAssertsFlag().load(std::memory_order_relaxed))
            {
                assert(condition && message);
            }
            #else
            (void)condition;
            (void)message;
            #endif
        }

        [[nodiscard]] std::size_t nextPowerOfTwo(std::size_t value) noexcept
        {
            if (value <= 1U)
            {
                return 1U;
            }

            --value;
            for (std::size_t shift = 1; shift < sizeof(std::size_t) * 8U; shift *= 2U)
            {
                value |= value >> shift;
            }

            return value + 1U;
        }

        [[nodiscard]] std::uint32_t categoryIndex(const Category category) noexcept
        {
            return static_cast<std::uint32_t>(std::countr_zero(static_cast<std::uint64_t>(category)));
        }

        struct SinkState
        {
            std::unique_ptr<Sink> sink;
            std::atomic<std::uint64_t> writeFailures{0};
            std::uint32_t consecutiveFailures{0};
        };

        enum class RunState : std::uint8_t
        {
            Stopped,
            Running,
            Stopping,
        };

        class Logger
        {
            public:
                Logger() = default;

                ~Logger() { shutdown(); }

                Logger(const Logger &) = delete;
                Logger &operator=(const Logger &) = delete;
                Logger(Logger &&) = delete;
                Logger &operator=(Logger &&) = delete;

                [[nodiscard]] bool initialize(Config config);
                void shutdown() noexcept;
                void flush() noexcept;

                void setMinLevel(Level level) noexcept;
                void setCategoryEnabled(Category category, bool enabled) noexcept;
                void setCategoryMinLevel(Category category, Level level) noexcept;
                void clearCategoryMinLevel(Category category) noexcept;
                void setFrame(std::uint64_t frame) noexcept;
                void addSink(std::unique_ptr<Sink> sink) noexcept;

                [[nodiscard]] Counters snapshot() noexcept;

                [[nodiscard]] bool running() const noexcept;
                void countNotRunning() noexcept;
                void countArgumentBudget() noexcept;
                [[nodiscard]] bool shouldLog(Level level, Category category) const noexcept;
                [[nodiscard]] std::uint64_t sampleTimestampNs() const noexcept;
                [[nodiscard]] static std::uint32_t sampleThreadId() noexcept;
                [[nodiscard]] std::uint64_t sampleFrame() const noexcept;
                [[nodiscard]] std::string_view sourceRootPrefix() const noexcept;
                [[nodiscard]] bool tryWriteFatalConsole(std::string_view line) noexcept;

                [[nodiscard]] detail::Record *acquireLogSlot(
                    Level level,
                    Category category,
                    std::uint64_t timestampNs,
                    std::uint32_t threadId,
                    std::uint64_t frame,
                    const char *file,
                    const char *function,
                    std::uint32_t line,
                    std::string_view format,
                    bool consoleWritten
                ) noexcept;

                void commitLogSlot(detail::FormatThunk formatFn, detail::DestroyThunk destroyFn) noexcept;
                void abortLogSlot() noexcept;

            private:
                void resetCounters() noexcept;
                void backendLoop(const std::stop_token &stop);
                void abandonOccupiedSlots() noexcept;
                void applyControl(detail::Record &record);
                void writeLineToSinks(Level level, std::string_view line, bool skipConsole);
                void emitOverflowSummary(fmt::memory_buffer &buffer);
                void flushAllSinks() noexcept;
                void completeFlush() noexcept;
                void registerSink(std::unique_ptr<Sink> sink, bool openFailed);
                [[nodiscard]] Level effectiveMinLevel(Category category) const noexcept;
                [[nodiscard]] static Level clampToFloor(Level level) noexcept;

                std::atomic<RunState> m_state{RunState::Stopped};
                detail::Ring m_ring;
                std::jthread m_thread;
                std::vector<std::unique_ptr<SinkState>> m_sinks;
                ConsoleSink *m_console{nullptr};

                std::atomic<std::uint8_t> m_minLevel{VISCERA_LOG_MIN_LEVEL};
                std::atomic<std::uint64_t> m_categoryMask{kNamedCategoryMask};
                std::array<std::atomic<std::uint8_t>, 64> m_categoryMin{};
                std::array<std::atomic<bool>, 64> m_hasCategoryMin{};
                std::atomic<std::uint64_t> m_frame{0};

                std::chrono::steady_clock::time_point m_steadyBase;
                bool m_hasSteadyBase{false};
                std::string m_sourceRoot;
                std::chrono::milliseconds m_flushTimeout{std::chrono::seconds{1}};

                std::atomic<std::uint64_t> m_overflow{0};
                std::atomic<std::uint64_t> m_argumentBudget{0};
                std::atomic<std::uint64_t> m_notRunning{0};
                std::atomic<std::uint64_t> m_formattingFailures{0};
                std::atomic<std::uint64_t> m_flushTimeouts{0};
                std::uint64_t m_lastOverflowReported{0};

                std::atomic<bool> m_abandon{false};
                std::atomic<bool> m_flushRequested{false};
                std::atomic<std::uint64_t> m_flushGeneration{0};
                std::atomic<std::uint64_t> m_flushDoneGeneration{0};
                std::mutex m_flushMutex;
                std::condition_variable m_flushCv;
        };

        bool Logger::initialize(Config config)
        {
            if (m_state.load(std::memory_order_acquire) == RunState::Running)
            {
                contractAssert(false, "initialize called while the logger is already running");
                return false;
            }

            shutdown();

            const std::size_t capacity = nextPowerOfTwo(config.queueCapacity == 0 ? 1 : config.queueCapacity);
            m_ring.reset(capacity);
            m_flushTimeout = config.flushTimeout;
            m_sourceRoot = std::move(config.sourceRootPrefix);
            m_steadyBase = std::chrono::steady_clock::now();
            m_hasSteadyBase = true;
            m_console = nullptr;
            m_abandon.store(false, std::memory_order_relaxed);
            m_flushRequested.store(false, std::memory_order_relaxed);
            resetCounters();
            m_minLevel.store(VISCERA_LOG_MIN_LEVEL, std::memory_order_relaxed);
            m_categoryMask.store(kNamedCategoryMask, std::memory_order_relaxed);
            m_frame.store(0, std::memory_order_relaxed);
            for (auto &value : m_hasCategoryMin)
            {
                value.store(false, std::memory_order_relaxed);
            }

            if (config.filePath.has_value())
            {
                auto file = std::make_unique<FileSink>(*config.filePath);
                const bool openFailed = file->openFailed();
                if (openFailed)
                {
                    file->setEnabled(false);
                }

                registerSink(std::move(file), openFailed);
            }

            for (auto &sink : config.sinks)
            {
                registerSink(std::move(sink), false);
            }

            try
            {
                m_thread = std::jthread([this](const std::stop_token &stop) { backendLoop(stop); });
            }
            catch (...)
            {
                m_sinks.clear();
                m_console = nullptr;
                m_ring.clear();
                m_hasSteadyBase = false;
                return false;
            }

            m_state.store(RunState::Running, std::memory_order_release);
            return true;
        }

        void Logger::shutdown() noexcept
        {
            const auto state = m_state.load(std::memory_order_acquire);
            if (state == RunState::Stopped && !m_thread.joinable())
            {
                return;
            }

            m_state.store(RunState::Stopping, std::memory_order_release);

            if (m_thread.joinable())
            {
                const auto gen = m_flushGeneration.fetch_add(1, std::memory_order_acq_rel) + 1U;
                m_flushRequested.store(true, std::memory_order_release);
                m_thread.request_stop();
                m_ring.notifyAll();

                {
                    std::unique_lock lock{m_flushMutex};
                    if (!m_flushCv.wait_for(
                        lock,
                        m_flushTimeout,
                        [&] { return m_flushDoneGeneration.load(std::memory_order_acquire) >= gen; }
                    ))
                    {
                        m_flushTimeouts.fetch_add(1, std::memory_order_relaxed);
                        m_abandon.store(true, std::memory_order_release);
                        m_ring.notifyAll();
                        m_flushCv.wait(
                            lock,
                            [&]
                            {
                                return m_flushDoneGeneration.load(std::memory_order_acquire) >= gen ||
                                    !m_thread.joinable();
                            }
                        );
                    }
                }

                m_thread.join();
            }

            m_sinks.clear();
            m_console = nullptr;
            m_ring.clear();
            m_abandon.store(false, std::memory_order_relaxed);
            m_flushRequested.store(false, std::memory_order_relaxed);
            m_hasSteadyBase = false;
            m_state.store(RunState::Stopped, std::memory_order_release);
        }

        void Logger::flush() noexcept
        {
            if (m_state.load(std::memory_order_acquire) != RunState::Running)
            {
                return;
            }

            const auto gen = m_flushGeneration.fetch_add(1, std::memory_order_acq_rel) + 1U;
            m_flushRequested.store(true, std::memory_order_release);
            m_ring.notifyAll();

            std::unique_lock lock{m_flushMutex};
            if (!m_flushCv.wait_for(
                lock,
                m_flushTimeout,
                [&] { return m_flushDoneGeneration.load(std::memory_order_acquire) >= gen; }
            ))
            {
                m_flushTimeouts.fetch_add(1, std::memory_order_relaxed);
                m_abandon.store(true, std::memory_order_release);
                m_ring.notifyAll();
                m_flushCv.wait(lock, [&] { return m_flushDoneGeneration.load(std::memory_order_acquire) >= gen; });
            }
        }

        void Logger::setMinLevel(const Level level) noexcept
        {
            m_minLevel.store(static_cast<std::uint8_t>(clampToFloor(level)), std::memory_order_relaxed);
        }

        void Logger::setCategoryEnabled(const Category category, const bool enabled) noexcept
        {
            const auto bit = static_cast<std::uint64_t>(category);
            if (bit == 0)
            {
                return;
            }

            auto mask = m_categoryMask.load(std::memory_order_relaxed);
            while (!m_categoryMask.compare_exchange_weak(
                mask,
                enabled ? (mask | bit) : (mask & ~bit),
                std::memory_order_relaxed
            )) {}
        }

        void Logger::setCategoryMinLevel(const Category category, const Level level) noexcept
        {
            const auto index = categoryIndex(category);
            if (index >= m_categoryMin.size())
            {
                return;
            }

            m_categoryMin.at(index).store(static_cast<std::uint8_t>(clampToFloor(level)), std::memory_order_relaxed);
            m_hasCategoryMin.at(index).store(true, std::memory_order_relaxed);
        }

        void Logger::clearCategoryMinLevel(const Category category) noexcept
        {
            const auto index = categoryIndex(category);
            if (index >= m_hasCategoryMin.size())
            {
                return;
            }

            m_hasCategoryMin.at(index).store(false, std::memory_order_relaxed);
        }

        void Logger::setFrame(const std::uint64_t frame) noexcept
        {
            m_frame.store(frame, std::memory_order_relaxed);
        }

        void Logger::addSink(std::unique_ptr<Sink> sink) noexcept
        {
            if (sink == nullptr || m_state.load(std::memory_order_acquire) != RunState::Running)
            {
                return;
            }

            try
            {
                m_ring.mutex().lock();
            }
            catch (...)
            {
                m_overflow.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            if (m_state.load(std::memory_order_relaxed) != RunState::Running || m_ring.full())
            {
                if (m_ring.full())
                {
                    m_overflow.fetch_add(1, std::memory_order_relaxed);
                }

                m_ring.mutex().unlock();
                return;
            }

            auto *const record = m_ring.writeSlot();
            record->kind = detail::Record::Kind::AddSink;
            record->controlSink = sink.release();
            record->formatThunk = nullptr;
            record->destroyThunk = nullptr;
            m_ring.publish();
            m_ring.mutex().unlock();
            m_ring.notifyOne();
        }

        Counters Logger::snapshot() noexcept
        {
            Counters result;
            result.overflow = m_overflow.load(std::memory_order_relaxed);
            result.argumentBudget = m_argumentBudget.load(std::memory_order_relaxed);
            result.notRunning = m_notRunning.load(std::memory_order_relaxed);
            result.formattingFailures = m_formattingFailures.load(std::memory_order_relaxed);
            result.flushTimeouts = m_flushTimeouts.load(std::memory_order_relaxed);
            result.sinkWriteFailures.reserve(m_sinks.size());
            for (const auto &state : m_sinks)
            {
                result.sinkWriteFailures.push_back(state->writeFailures.load(std::memory_order_relaxed));
            }

            return result;
        }

        bool Logger::running() const noexcept
        {
            return m_state.load(std::memory_order_acquire) == RunState::Running;
        }

        void Logger::countNotRunning() noexcept
        {
            contractAssert(false, "log called while the logger is not running");
            m_notRunning.fetch_add(1, std::memory_order_relaxed);
        }

        void Logger::countArgumentBudget() noexcept
        {
            m_argumentBudget.fetch_add(1, std::memory_order_relaxed);
        }

        bool Logger::shouldLog(const Level level, const Category category) const noexcept
        {
            if (static_cast<std::uint8_t>(level) < static_cast<std::uint8_t>(effectiveMinLevel(category)))
            {
                return false;
            }

            const auto mask = m_categoryMask.load(std::memory_order_relaxed);
            return (mask & static_cast<std::uint64_t>(category)) != 0;
        }

        std::uint64_t Logger::sampleTimestampNs() const noexcept
        {
            if (!m_hasSteadyBase)
            {
                return 0;
            }

            const auto elapsed = std::chrono::steady_clock::now() - m_steadyBase;
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        }

        std::uint32_t Logger::sampleThreadId() noexcept
        {
            return detail::currentThreadId();
        }

        std::uint64_t Logger::sampleFrame() const noexcept
        {
            return m_frame.load(std::memory_order_relaxed);
        }

        std::string_view Logger::sourceRootPrefix() const noexcept
        {
            return m_sourceRoot;
        }

        bool Logger::tryWriteFatalConsole(const std::string_view line) noexcept
        {
            if (m_console == nullptr || !m_console->enabled())
            {
                return false;
            }

            return m_console->tryWriteFatal(line);
        }

        detail::Record *Logger::acquireLogSlot(
            const Level level,
            const Category category,
            const std::uint64_t timestampNs,
            const std::uint32_t threadId,
            const std::uint64_t frame,
            const char *const file,
            const char *const function,
            const std::uint32_t line,
            const std::string_view format,
            const bool consoleWritten
        ) noexcept
        {
            try
            {
                m_ring.mutex().lock();
            }
            catch (...)
            {
                m_overflow.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
            }

            if (m_state.load(std::memory_order_relaxed) != RunState::Running)
            {
                m_ring.mutex().unlock();
                countNotRunning();
                return nullptr;
            }

            if (m_ring.full())
            {
                m_overflow.fetch_add(1, std::memory_order_relaxed);
                m_ring.mutex().unlock();
                return nullptr;
            }

            auto *const record = m_ring.writeSlot();
            record->kind = detail::Record::Kind::Log;
            record->level = level;
            record->category = category;
            record->timestampNs = timestampNs;
            record->threadId = threadId;
            record->frame = frame;
            record->file = file;
            record->function = function;
            record->line = line;
            record->format = format;
            record->formatThunk = nullptr;
            record->destroyThunk = nullptr;
            record->flags = consoleWritten ? detail::kFlagConsoleWritten : 0;
            record->controlSink = nullptr;
            return record;
        }

        void Logger::commitLogSlot(const detail::FormatThunk formatFn, const detail::DestroyThunk destroyFn) noexcept
        {
            auto *const record = m_ring.writeSlot();
            record->formatThunk = formatFn;
            record->destroyThunk = destroyFn;
            m_ring.publish();
            m_ring.mutex().unlock();
            m_ring.notifyOne();
        }

        void Logger::abortLogSlot() noexcept
        {
            m_ring.mutex().unlock();
        }

        void Logger::resetCounters() noexcept
        {
            m_overflow.store(0, std::memory_order_relaxed);
            m_argumentBudget.store(0, std::memory_order_relaxed);
            m_notRunning.store(0, std::memory_order_relaxed);
            m_formattingFailures.store(0, std::memory_order_relaxed);
            m_flushTimeouts.store(0, std::memory_order_relaxed);
            m_lastOverflowReported = 0;
            m_flushGeneration.store(0, std::memory_order_relaxed);
            m_flushDoneGeneration.store(0, std::memory_order_relaxed);
        }

        void Logger::backendLoop(const std::stop_token &stop)
        {
            const std::stop_callback callback{stop, [this] { m_ring.notifyAll(); }};
            fmt::memory_buffer buffer;
            buffer.reserve(detail::kFormatBufferBytes);

            while (true)
            {
                std::unique_lock lock{m_ring.mutex()};
                m_ring.cv().wait(
                    lock,
                    [&]
                    {
                        return m_abandon.load(std::memory_order_relaxed) || !m_ring.empty() || stop.stop_requested() ||
                            m_flushRequested.load(std::memory_order_relaxed) ||
                            m_state.load(std::memory_order_relaxed) == RunState::Stopping;
                    }
                );

                if (m_abandon.load(std::memory_order_relaxed))
                {
                    abandonOccupiedSlots();
                    lock.unlock();
                    flushAllSinks();
                    completeFlush();
                    return;
                }

                if (m_ring.empty())
                {
                    lock.unlock();
                    emitOverflowSummary(buffer);
                    const bool stopping =
                        stop.stop_requested() || m_state.load(std::memory_order_relaxed) == RunState::Stopping;
                    if (m_flushRequested.load(std::memory_order_relaxed) || stopping)
                    {
                        flushAllSinks();
                        completeFlush();
                    }

                    if (stopping)
                    {
                        return;
                    }

                    continue;
                }

                auto *const record = m_ring.readSlot();
                const bool isControl = record->kind == detail::Record::Kind::AddSink;
                lock.unlock();

                if (isControl)
                {
                    applyControl(*record);
                    lock.lock();
                    m_ring.consume();
                    lock.unlock();
                    continue;
                }

                buffer.clear();
                const auto level = record->level;
                const bool skipConsole = (record->flags & detail::kFlagConsoleWritten) != 0;
                try
                {
                    fmt::format_to(
                        std::back_inserter(buffer),
                        "[{:.3f}][{}][{}][{}][{}][{}:{}] ",
                        detail::elapsedSeconds(record->timestampNs),
                        record->frame,
                        record->threadId,
                        levelName(record->level),
                        categoryName(record->category),
                        detail::shortFile(record->file, m_sourceRoot),
                        record->line
                    );
                    if (record->formatThunk != nullptr)
                    {
                        record->formatThunk(buffer, *record);
                    }
                }
                catch (...)
                {
                    m_formattingFailures.fetch_add(1, std::memory_order_relaxed);
                    buffer.clear();
                    fmt::format_to(
                        std::back_inserter(buffer),
                        "[{:.3f}][{}][{}][{}][{}][{}:{}] formatting failed",
                        detail::elapsedSeconds(record->timestampNs),
                        record->frame,
                        record->threadId,
                        levelName(record->level),
                        categoryName(record->category),
                        detail::shortFile(record->file, m_sourceRoot),
                        record->line
                    );
                }

                const std::string_view line{buffer.data(), buffer.size()};

                lock.lock();
                if (record->destroyThunk != nullptr)
                {
                    record->destroyThunk(*record);
                }

                m_ring.consume();
                lock.unlock();

                writeLineToSinks(level, line, skipConsole);
                emitOverflowSummary(buffer);
            }
        }

        void Logger::abandonOccupiedSlots() noexcept
        {
            while (!m_ring.empty())
            {
                auto *const record = m_ring.readSlot();
                if (record->kind == detail::Record::Kind::AddSink && record->controlSink != nullptr)
                {
                    const std::unique_ptr<Sink> discarded{record->controlSink};
                    record->controlSink = nullptr;
                }

                if (record->destroyThunk != nullptr)
                {
                    record->destroyThunk(*record);
                }

                m_ring.consume();
            }
        }

        void Logger::applyControl(detail::Record &record)
        {
            if (record.controlSink == nullptr)
            {
                return;
            }

            registerSink(std::unique_ptr<Sink>{record.controlSink}, false);
            record.controlSink = nullptr;
        }

        void Logger::writeLineToSinks(const Level level, const std::string_view line, const bool skipConsole)
        {
            std::vector<std::pair<std::string, std::uint32_t>> disabled;

            for (auto &state : m_sinks)
            {
                if (state->sink == nullptr || !state->sink->enabled())
                {
                    continue;
                }

                if (skipConsole && state->sink.get() == m_console)
                {
                    continue;
                }

                bool ok = false;
                try
                {
                    ok = state->sink->write(level, line);
                }
                catch (...)
                {
                    ok = false;
                }

                if (!ok)
                {
                    state->writeFailures.fetch_add(1, std::memory_order_relaxed);
                    ++state->consecutiveFailures;
                    if (state->consecutiveFailures >= detail::kConsecutiveFailuresToDisable)
                    {
                        state->sink->setEnabled(false);
                        disabled.emplace_back(std::string{state->sink->name()}, state->consecutiveFailures);
                    }
                }
                else
                {
                    state->consecutiveFailures = 0;
                }
            }

            for (const auto &[name, count] : disabled)
            {
                fmt::memory_buffer diagnostic;
                fmt::format_to(
                    std::back_inserter(diagnostic),
                    "[{:.3f}][{}][{}][warning][core][logging:0] sink '{}' disabled after {} consecutive write failures",
                    detail::elapsedSeconds(sampleTimestampNs()),
                    m_frame.load(std::memory_order_relaxed),
                    detail::currentThreadId(),
                    name,
                    count
                );
                writeLineToSinks(Level::Warning, std::string_view{diagnostic.data(), diagnostic.size()}, false);
            }
        }

        void Logger::emitOverflowSummary(fmt::memory_buffer &buffer)
        {
            const auto overflow = m_overflow.load(std::memory_order_relaxed);
            if (overflow <= m_lastOverflowReported)
            {
                return;
            }

            const auto delta = overflow - m_lastOverflowReported;
            m_lastOverflowReported = overflow;
            buffer.clear();
            fmt::format_to(
                std::back_inserter(buffer),
                "[{:.3f}][{}][{}][warning][core][logging:0] dropped {} records",
                detail::elapsedSeconds(sampleTimestampNs()),
                m_frame.load(std::memory_order_relaxed),
                detail::currentThreadId(),
                delta
            );
            writeLineToSinks(Level::Warning, std::string_view{buffer.data(), buffer.size()}, false);
        }

        void Logger::flushAllSinks() noexcept
        {
            for (const auto &state : m_sinks)
            {
                if (state->sink == nullptr || !state->sink->enabled())
                {
                    continue;
                }

                try
                {
                    state->sink->flush();
                }
                catch (...)
                {
                    state->writeFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        void Logger::completeFlush() noexcept
        {
            const auto gen = m_flushGeneration.load(std::memory_order_relaxed);
            {
                const std::scoped_lock lock{m_flushMutex};
                m_flushDoneGeneration.store(gen, std::memory_order_release);
                m_flushRequested.store(false, std::memory_order_relaxed);
            }

            m_flushCv.notify_all();
        }

        void Logger::registerSink(std::unique_ptr<Sink> sink, const bool openFailed)
        {
            if (sink == nullptr)
            {
                return;
            }

            auto state = std::make_unique<SinkState>();
            if (auto *const console = dynamic_cast<ConsoleSink*>(sink.get()); console != nullptr)
            {
                m_console = console;
            }

            if (openFailed)
            {
                state->writeFailures.store(1, std::memory_order_relaxed);
            }

            state->sink = std::move(sink);
            m_sinks.push_back(std::move(state));
        }

        Level Logger::effectiveMinLevel(const Category category) const noexcept
        {
            const auto index = categoryIndex(category);
            if (index < m_hasCategoryMin.size() && m_hasCategoryMin.at(index).load(std::memory_order_relaxed))
            {
                return static_cast<Level>(m_categoryMin.at(index).load(std::memory_order_relaxed));
            }

            return static_cast<Level>(m_minLevel.load(std::memory_order_relaxed));
        }

        Level Logger::clampToFloor(const Level level) noexcept
        {
            if (isBelowFloor(level))
            {
                return static_cast<Level>(VISCERA_LOG_MIN_LEVEL);
            }

            return level;
        }

        Logger &loggerInstance()
        {
            static Logger logger;
            return logger;
        }
    } // namespace

    bool initialize(Config config)
    {
        return loggerInstance().initialize(std::move(config));
    }

    void shutdown() noexcept
    {
        loggerInstance().shutdown();
    }

    void flush() noexcept
    {
        loggerInstance().flush();
    }

    void setMinLevel(const Level level) noexcept
    {
        loggerInstance().setMinLevel(level);
    }

    void setCategoryEnabled(const Category category, const bool enabled) noexcept
    {
        loggerInstance().setCategoryEnabled(category, enabled);
    }

    void setCategoryMinLevel(const Category category, const Level level) noexcept
    {
        loggerInstance().setCategoryMinLevel(category, level);
    }

    void clearCategoryMinLevel(const Category category) noexcept
    {
        loggerInstance().clearCategoryMinLevel(category);
    }

    void setFrame(const std::uint64_t frame) noexcept
    {
        loggerInstance().setFrame(frame);
    }

    void addSink(std::unique_ptr<Sink> sink) noexcept
    {
        loggerInstance().addSink(std::move(sink));
    }

    Counters counters() noexcept
    {
        return loggerInstance().snapshot();
    }

    namespace detail
    {
        void setContractAssertsEnabled(const bool enabled) noexcept
        {
            contractAssertsFlag().store(enabled, std::memory_order_relaxed);
        }

        bool loggerRunning() noexcept
        {
            return loggerInstance().running();
        }

        void countNotRunning() noexcept
        {
            loggerInstance().countNotRunning();
        }

        void countArgumentBudget() noexcept
        {
            loggerInstance().countArgumentBudget();
        }

        bool shouldLog(const Level level, const Category category) noexcept
        {
            return loggerInstance().shouldLog(level, category);
        }

        std::uint64_t sampleTimestampNs() noexcept
        {
            return loggerInstance().sampleTimestampNs();
        }

        std::uint32_t sampleThreadId() noexcept
        {
            return Logger::sampleThreadId();
        }

        std::uint64_t sampleFrame() noexcept
        {
            return loggerInstance().sampleFrame();
        }

        std::string_view sourceRootPrefix() noexcept
        {
            return loggerInstance().sourceRootPrefix();
        }

        bool tryWriteFatalConsole(const std::string_view line) noexcept
        {
            return loggerInstance().tryWriteFatalConsole(line);
        }

        Record *acquireLogSlot(
            const Level level,
            const Category category,
            const std::uint64_t timestampNs,
            const std::uint32_t threadId,
            const std::uint64_t frame,
            const char *const file,
            const char *const function,
            const std::uint32_t line,
            const std::string_view format,
            const bool consoleWritten
        ) noexcept
        {
            return loggerInstance().acquireLogSlot(
                level,
                category,
                timestampNs,
                threadId,
                frame,
                file,
                function,
                line,
                format,
                consoleWritten
            );
        }

        void commitLogSlot(const FormatThunk formatFn, const DestroyThunk destroyFn) noexcept
        {
            loggerInstance().commitLogSlot(formatFn, destroyFn);
        }

        void abortLogSlot() noexcept
        {
            loggerInstance().abortLogSlot();
        }
    } // namespace detail
}     // namespace viscera::logging
