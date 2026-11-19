#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <viscera/logging/Logger.hpp>
#include <viscera/logging/MemorySink.hpp>

// Voorkomt dat de compiler loops wegoptimaliseert in Release-modus (-O3)
template <typename T>
inline void doNotOptimize(T const &value)
{
    asm volatile("" : : "r,m"(value) : "memory");
}

int main()
{
    using clock = std::chrono::steady_clock;
    using namespace viscera::logging;

    auto sink = std::make_unique<MemorySink>();
    Config config;
    config.queueCapacity = 8192;
    config.sinks.push_back(std::move(sink));

    if (!initialize(std::move(config)))
    {
        std::cerr << "initialize failed\n";
        return 1;
    }

    constexpr int kWarmup = 10000;
    constexpr int kIterations = 100000;

    // 1. WARM-UP: Zorgt dat caches en thread-pools al opgestart zijn
    setMinLevel(Level::Trace);
    for (int i = 0; i < kWarmup; ++i)
    {
        info(Category::Core, "warmup {}", i);
    }
    flush();

    // 2. DISABLED BENCHMARK (Fast-path check)
    setMinLevel(Level::Fatal);
    const auto disabledStart = clock::now();
    for (int index = 0; index < kIterations; ++index)
    {
        doNotOptimize(index);
        info(Category::Core, "disabled {}", index);
    }
    const auto disabledNs = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - disabledStart).count();

    // 3. SINGLE-THREADED ENABLED BENCHMARK (Enqueue vs. Flush gescheiden)
    setMinLevel(Level::Trace);
    const auto enabledStart = clock::now();
    for (int index = 0; index < kIterations; ++index)
    {
        doNotOptimize(index);
        info(Category::Core, "enabled {}", index);
    }
    const auto enqueueDone = clock::now(); // Hoe lang de main-thread echt moest wachten
    flush();
    const auto flushDone = clock::now(); // Hoe lang de achtergrond-thread nodig had

    const auto enqueueNs = std::chrono::duration_cast<std::chrono::nanoseconds>(enqueueDone - enabledStart).count();
    const auto totalEnabledNs = std::chrono::duration_cast<std::chrono::nanoseconds>(flushDone - enabledStart).count();

    // 4. MULTI-THREADED CONTENTION BENCHMARK (4 threads tegelijk)
    constexpr int kThreads = 4;
    constexpr int kPerThread = kIterations / kThreads;
    std::vector<std::thread> workers;
    std::atomic<bool> startFlag{false};

    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back(
            [&, t]()
            {
                while (!startFlag.load(std::memory_order_acquire))
                {
                    // Wacht tot alle threads klaarstaan
                }
                for (int i = 0; i < kPerThread; ++i)
                {
                    info(Category::Core, "thread {} msg {}", t, i);
                }
            }
        );
    }

    const auto mtStart = clock::now();
    startFlag.store(true, std::memory_order_release);
    for (auto &w : workers)
    {
        w.join();
    }
    const auto mtEnqueueDone = clock::now();
    flush();
    const auto mtFlushDone = clock::now();

    const auto mtEnqueueNs = std::chrono::duration_cast<std::chrono::nanoseconds>(mtEnqueueDone - mtStart).count();
    const auto mtTotalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(mtFlushDone - mtStart).count();

    // RESULTATEN PRINTEN
    std::cout << "--- SINGLE THREAD ---\n";
    std::cout << "Disabled per_call_ns:        " << (disabledNs / kIterations) << " ns\n";
    std::cout << "Enabled enqueue per_call_ns: " << (enqueueNs / kIterations) << " ns\n";
    std::cout << "Enabled + flush per_call_ns: " << (totalEnabledNs / kIterations) << " ns\n\n";

    std::cout << "--- MULTI THREAD (" << kThreads << " threads) ---\n";
    std::cout << "MT enqueue per_call_ns:      " << (mtEnqueueNs / kIterations) << " ns\n";
    std::cout << "MT + flush per_call_ns:      " << (mtTotalNs / kIterations) << " ns\n";

    shutdown();
    return 0;
}
