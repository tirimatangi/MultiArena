#include <array>
#include <vector>
#include <cmath>
#include <numeric>
#include <cassert>
#include <iostream>

#include <MultiArena/MultiArena.h>

using std::array;
using std::vector;
using std::cout;

// Number of arenas and bytes per arena.
constexpr std::size_t numArenas = 64;
constexpr std::size_t arenaSize = 4*1024;
// Number of threads in synchronized mode tests.
constexpr int threadCount = 16;
// Running time for each test
constexpr double runtimeSecs = 4.0;

// true means that the arenas are allocated from heap.
// false means that the arenas are allocated from stack or static memory.
constexpr bool bArenasInHeap = false;

auto makeUnsynchronizedArenaResource()
{
    if constexpr (bArenasInHeap)
        return MultiArena::UnsynchronizedArenaResource(numArenas, arenaSize);
    else
        return MultiArena::UnsynchronizedArenaResource<numArenas, arenaSize>();
}

auto makeSynchronizedArenaResource()
{
    if constexpr (bArenasInHeap)
        return MultiArena::SynchronizedArenaResource(numArenas, arenaSize);
    else
        return MultiArena::SynchronizedArenaResource<numArenas, arenaSize>();
}

// Run an "application" and return a performance index. The large the index, the better the performance.
template <unsigned ARRAY_SIZE, unsigned  VEC_SIZE>
double runApplication(int id = 0)
{
    using T = int;
    constexpr unsigned numIterationsPerRound = 1 << 16;
    std::srand(0x1234abcd + id);
    std::array<std::pmr::vector<int>, ARRAY_SIZE> aVec;

    std::size_t numRoundsDone = 0;
    double timeSoFar = 0;
    while (timeSoFar < runtimeSecs) {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < numIterationsPerRound; ++i) {
            int j = std::rand() % ARRAY_SIZE; // Destination index of the new vector
            // Verify that the previous vector at index j is still sane before overwriting it.
            int k = 0;
            for (auto val : aVec[j])
                if (val != ++k) {
                    throw std::runtime_error("runApplication: memory corruption detected!");
                }

            // Replace the vector at index j with a new vector
            // allocated from the default memory resource
            // which is either a MultiArena allocator or the system new/delete allocator.
            aVec[j] = std::pmr::vector<T>();
            auto sz = std::rand() % (VEC_SIZE / sizeof(T)); // Size of the new vector
            aVec[j].resize(sz);
            // Fill the vector with known numbers
            k = 0;
            for (auto& val : aVec[j])
                val = ++k;
        } // for i
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        timeSoFar += diff.count();
        ++numRoundsDone;
    }
    // Return the number of rounds per second.
    return (numRoundsDone * numIterationsPerRound) / timeSoFar;
}

int main()
{
    // Symmary of performance indices
    enum TestList { UnsyncArena, UnsyncSystem, UnsyncPool, SyncArena, SyncSystem, SyncPool, NumTests };
    double aPerformanceIndex[NumTests] = {};

    cout << "[Arenas will be stored in " << (bArenasInHeap ? "heap" : "stack") << "]\n";

    // Example 2.1: Measure the speed of an operation using unsynchronized arena resource.
    cout << "\n*** Example 2.1 *** Measuring speed of unsynchronized arena on a single thread ("
         << runtimeSecs << " secs...)\n";
    {
        // Replace the default memory resource with the arena resource
        auto arenaResource = makeUnsynchronizedArenaResource();
        auto oldDefaultResource = std::pmr::set_default_resource(&arenaResource);

        double perfIndex = runApplication<numArenas, arenaSize>(0);
        cout << "    Performance index = " << perfIndex << "\n";
        aPerformanceIndex[UnsyncArena] = perfIndex;
        // Restore the default memory resource.
        std::pmr::set_default_resource(oldDefaultResource);
    }

    // Example 2.2: Measure the speed of an operation using default system memory resource.
    cout << "\n*** Example 2.2a *** Measuring speed of default system memory resource on a single thread ("
         << runtimeSecs << " secs...)\n";
    {
        double perfIndex = runApplication<numArenas, arenaSize>(0);
        cout << "    Performance index = " << perfIndex << "\n";
        aPerformanceIndex[UnsyncSystem] = perfIndex;
    }

    cout << "\n*** Example 2.2b *** Measuring speed of std::pmr::unsynchronized_pool_resource on a single thread ("
         << runtimeSecs << " secs...)\n";
    {
        // Replace the default memory resource with the arena resource
        auto poolResource = std::pmr::unsynchronized_pool_resource(std::pmr::new_delete_resource());
        auto oldDefaultResource = std::pmr::set_default_resource(&poolResource);

        double perfIndex = runApplication<numArenas, arenaSize>(0);
        cout << "    Performance index = " << perfIndex << "\n";
        aPerformanceIndex[UnsyncPool] = perfIndex;
        // Restore the default memory resource.
        std::pmr::set_default_resource(oldDefaultResource);
    }

    // Example 2.3: Measure the speed of an operation using synchronized arena resource.
    cout << "\n*** Example 2.3 *** Measuring speed of synchronized arena shared by " << threadCount << " threads ("
         << runtimeSecs << " secs...)\n";
    {
        array<double, threadCount> aPerformanceOfThread;

        // Run the application and store the performance index to an array.
        auto job = [&](int id) {
            aPerformanceOfThread[id] = runApplication<numArenas / threadCount, arenaSize / threadCount>(id);
        };

        std::vector<std::thread> vecThreads;
        std::vector<double> vecTimes(threadCount);

        // Replace the default memory resource with the arena resource
        auto arenaResource = makeSynchronizedArenaResource();
        auto oldDefaultResource = std::pmr::set_default_resource(&arenaResource);

        for (int i = 0; i < threadCount; ++i)
            vecThreads.push_back(std::thread(job, i));

        for(std::thread& thr : vecThreads)
            if (thr.joinable())
                thr.join();

        for (auto v : aPerformanceOfThread) {
            aPerformanceIndex[SyncArena] += v;
        }
        cout << "    Performance index = " << aPerformanceIndex[SyncArena] << "\n";
        // Restore the default memory resource.
        std::pmr::set_default_resource(oldDefaultResource);
    }

    // Example 2.4: Measure the speed of an operation using synchronized arena resource.
    cout << "\n*** Example 2.4a *** Measuring speed of default system memory resource shared by " << threadCount << " threads ("
         << runtimeSecs << " secs...)\n";
    {
        array<double, threadCount> aPerformanceOfThread;

        // Run the application and store the performance index to an array.
        auto job = [&](int id) {
            aPerformanceOfThread[id] = runApplication<numArenas / threadCount, arenaSize / threadCount>(id);
        };

        std::vector<std::thread> vecThreads;
        std::vector<double> vecTimes(threadCount);

        for (int i = 0; i < threadCount; ++i)
            vecThreads.push_back(std::thread(job, i));

        for(std::thread& thr : vecThreads)
            if (thr.joinable())
                thr.join();

        for (auto v : aPerformanceOfThread) {
            aPerformanceIndex[SyncSystem] += v;
        }
        cout << "    Performance index = " << aPerformanceIndex[SyncSystem] << "\n";
    }

    cout << "\n*** Example 2.4b *** Measuring speed of synchronized pool resource shared by " << threadCount << " threads ("
         << runtimeSecs << " secs...)\n";
    {
        array<double, threadCount> aPerformanceOfThread;

        // Run the application and store the performance index to an array.
        auto job = [&](int id) {
            aPerformanceOfThread[id] = runApplication<numArenas / threadCount, arenaSize / threadCount>(id);
        };

        std::vector<std::thread> vecThreads;
        std::vector<double> vecTimes(threadCount);

        // Replace the default memory resource with the pool resource
        auto poolResource = std::pmr::synchronized_pool_resource(std::pmr::new_delete_resource());
        auto oldDefaultResource = std::pmr::set_default_resource(&poolResource);

        for (int i = 0; i < threadCount; ++i)
            vecThreads.push_back(std::thread(job, i));

        for(std::thread& thr : vecThreads)
            if (thr.joinable())
                thr.join();

        for (auto v : aPerformanceOfThread) {
            aPerformanceIndex[SyncPool] += v;
        }
        cout << "    Performance index = " << aPerformanceIndex[SyncPool] << "\n";
        // Restore the default memory resource.
        std::pmr::set_default_resource(oldDefaultResource);
    }
    cout << '\n';
    cout << "Performance indices (the bigger the better):\n";
    cout << "  Unsynchronized, arena resource   = " << aPerformanceIndex[UnsyncArena] << '\n';
    cout << "  Unsynchronized, default resource = " << aPerformanceIndex[UnsyncSystem] << '\n';
    cout << "  Unsynchronized, pool resource = " << aPerformanceIndex[UnsyncPool] << '\n';
    cout << "    --> Relative performance in unsynchronized mode:\n"
         << "        perf(arena allocator) / perf(system allocator) = "
         << int(100 * aPerformanceIndex[UnsyncArena] / aPerformanceIndex[UnsyncSystem] + 0.5) << "%\n";
    cout << "    --> Relative performance in unsynchronized mode:\n"
         << "        perf(arena allocator) / perf(pool allocator) = "
         << int(100 * aPerformanceIndex[UnsyncArena] / aPerformanceIndex[UnsyncPool] + 0.5) << "%\n";
    cout << "  Synchronized, " << threadCount << " threads, arena resource   = " << aPerformanceIndex[SyncArena] << '\n';
    cout << "  Synchronized, " << threadCount << " threads, default resource = " << aPerformanceIndex[SyncSystem] << '\n';
    cout << "  Synchronized, " << threadCount << " threads, default resource = " << aPerformanceIndex[SyncPool] << '\n';
    cout << "    --> Relative performance in synchronized mode:\n"
         << "        perf(arena allocator) / perf(system allocator) = "
         << int(100 * aPerformanceIndex[SyncArena] / aPerformanceIndex[SyncSystem] + 0.5) << "%\n";
    cout << "    --> Relative performance in synchronized mode:\n"
         << "        perf(arena allocator) / perf(pool allocator) = "
         << int(100 * aPerformanceIndex[SyncArena] / aPerformanceIndex[SyncPool] + 0.5) << "%\n";

    return 0;
}
