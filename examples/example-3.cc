#include <array>
#include <vector>
#include <cmath>
#include <numeric>
#include <cassert>
#include <variant>
#include <iostream>
#include <sstream>

#include <MultiArena/MultiArena.h>

using std::array;
using std::vector;
using std::cout;

// Maximum running time for each test
constexpr double runtimeSecs = 4.0;

using Variant = std::variant<double, MultiArena::AllocateTooLargeBlock, MultiArena::OutOfFreeArenas>;

// Run an "application" and return either a double as a sign of success
// or an exception which tells what the problem was with the memory resource.
Variant runApplication(MultiArena::StatisticsArenaResource* mr)
{
    constexpr std::size_t numVectors = 64;
    constexpr std::size_t vectorSize = 4096;
    using T = int;
    constexpr unsigned numIterationsPerRound = 1 << 16;
    std::srand(0x1234abcd);
    try {
        std::array<std::pmr::vector<int>, numVectors> aVec;
        std::size_t numRoundsDone = 0;
        double timeSoFar = 0;
        while (timeSoFar < runtimeSecs) {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < numIterationsPerRound; ++i) {
                int j = std::rand() % numVectors; // Destination index of the new vector
                // Verify that the previous vector at index j is still sane before overwriting it.
                int k = 0;
                for (auto val : aVec[j])
                    if (val != ++k) {
                        throw std::runtime_error("runApplication: memory corruption detected!");
                    }

                // Replace the vector at index j with the new vector.
                aVec[j] = std::pmr::vector<T>();
                auto sz = std::rand() % (vectorSize / sizeof(T)); // Size of the new vector
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
        cout << " pass!\n";
        cout << "The function has run successfully for " << runtimeSecs << " seconds.\n";
        cout << "It looks like feasible parameters for the arena memory resource are:\n";
        cout << "  Number of arenas = " << mr->numArenas() << "\n";
        cout << "        Arena size = " << mr->arenaSize() << " bytes\n";

        // Return the number of rounds per second
        return (numRoundsDone * numIterationsPerRound) / timeSoFar;
    }
    catch (const MultiArena::AllocateTooLargeBlock& e) {
        // The allocations should have been cleared from the memory resource.
        assert(mr->addressToBytesMap()->size() == 0);
        return e;
    }
    catch (const MultiArena::OutOfFreeArenas& e) {
        // The allocations should have been cleared from the memory resource.
        assert(mr->addressToBytesMap()->size() == 0);
        return e;
    }
}

int main()
{
    // Example 3.1: Determine the minimum requirements for the number of arenas and the size of an arena.
    cout << "\n*** Example 3.1 *** Determine the minimum requirements for the number of arenas and the size of an arena.\n";
    if constexpr (MultiArena::exceptionsEnabled == false)
    {
        cout << "  !! Skipped because MultiArena::exceptionsEnabled == false !!\n"
             << "  Unset flag MULTIARENA_DISABLE_EXCEPTIONS to enable exceptions.\n";
    }
    else
    {
        // Make an initial guess.
        unsigned numArenasCandidate = 32;
        unsigned arenaSizeCandidate = 2 * alignof(std::max_align_t);
        Variant result;
        do {
            cout << "Trying with (numArenas = " << numArenasCandidate
                << ", arenaSize = " << arenaSizeCandidate
                << ") for " << runtimeSecs << " secs ..." << std::flush;

            // Replace the default memory resource with the arena resource
            auto arenaResource = MultiArena::StatisticsArenaResource(numArenasCandidate, arenaSizeCandidate);
            auto oldDefaultResource = std::pmr::set_default_resource(&arenaResource);

            // Try to run the function with the candidate sizes.
            // The result is either a performance index (i.e. double) or an exception object.
            result = runApplication(&arenaResource);

            if (std::holds_alternative<MultiArena::AllocateTooLargeBlock>(result)) { //  AllocateTooLargeBlock was thrown.
                cout << " nope.\n  --> Arena size is to small. Increase arena size.\n";
                // Check the size of the block which caused the exception.
                arenaSizeCandidate = std::get<MultiArena::AllocateTooLargeBlock>(result).bytesNeeded;
                // The next arena size candidate is the failing blocksize rouned up to the required alignment.
                arenaSizeCandidate += (alignof(std::max_align_t) - arenaSizeCandidate % alignof(std::max_align_t));
            }
            else if (std::holds_alternative<MultiArena::OutOfFreeArenas>(result))  { //  OutOfFreeArenas was thrown.
                cout << " nope.\n  --> Too few arenas. Add one more arena.\n";
                ++numArenasCandidate;
            }

            // Restore the default memory resource.
            std::pmr::set_default_resource(oldDefaultResource);
        } while (result.index() != 0);
    }
    // Example 3.2: Demonstrate statistical analysis of active allocations.
    cout << "\n*** Example 3.2 *** Demonstrate statistical analysis with a histogram and an address map.\n";
    {
        using namespace MultiArena;
        // Demonstrate how the statistical resource can be used entirely without heap.
        // Both the arenas and the statistical data (address map and histogram) can be
        // allocated from separate upstream resources.
        constexpr std::size_t numArenas = 16;
        constexpr std::size_t bytesPerArena = 256;
        UnsynchronizedArenaResource<2, numArenas * bytesPerArena> upstreamDataResource, upstreamStatisticsResource;
        try {
            // Define an allocator using the statistical memory resource.
            using T = double;

#define NO_HEAP // Undefine to use system heap.
#ifdef NO_HEAP
            cout << "Using separate upstream resources for the arenas and the statistics map.\n";
            StatisticsArenaResource arenaResource(numArenas, bytesPerArena, &upstreamDataResource, &upstreamStatisticsResource);
#else
            cout << "Using the default new_delete_resource() for the arenas and the statistics map.\n";
            StatisticsArenaResource arenaResource(numArenas, bytesPerArena);
#endif
            std::pmr::polymorphic_allocator<T> allocator(&arenaResource);

            // Make a few allocations of type T[N]
            array<unsigned, 12> aN {1, 2, 2, 4, 8, 8, 16, 20, 20, 20, 20, 30};
            array<T*, aN.size()> aPtr;
            for (unsigned i = 0; i < aN.size(); ++i)
                aPtr[i] = allocator.allocate(aN[i]);

            cout << "The memory resource has:\n  "
                << arenaResource.numberOfAllocations() << " allocations,\n  "
                << arenaResource.bytesAllocated() << " bytes allocated in total,\n  "
                << arenaResource.numberOfBusyArenas() << " occupied arenas out of " << arenaResource.numArenas() << ".\n";

            // Demonstrate address map.
            auto pMap = arenaResource.addressToBytesMap(); // Returns const pointer to an std::map.
            cout << "\nAddress map of the " << pMap->size() << " allocations:\n";
            for (const auto& val : *pMap)
                cout << "  Address " << std::hex << val.first << std::dec << " has " << val.second << " bytes\n";

            // Demonstrate histogram of allocation sizes.
            cout << "\nHistogram of allocation sizes:\n";
            auto hist = arenaResource.histogram();
            for (auto x : hist)
                cout << "  A chunk of " <<x.first << " bytes has been allocated " << x.second << " times\n";

            // Demonstrate percentile calculator.
            cout << "\nPercentiles of allocated chunks:\n";
            for (double pc : {0.0, 0.1, 0.5, 0.9, 1.0})
                cout << "  " << pc*100 << "% of allocated chunks are smaller than or equal to "
                    << arenaResource.percentile(pc) << " bytes.\n";

            cout << "\nAverage size of allocations = " << arenaResource.mean() << " bytes.\n"
                << "Standard deviations of allocations = " << arenaResource.stdDev() << " bytes.\n";

            // There is no need to deallocate as the arenas will just wink out of existance
            // as they go out of scope.
            // The deallocation is done here for demonstration only.
            for (unsigned i = 0; i < aN.size(); ++i)
                allocator.deallocate(aPtr[i], aN[i]);
            cout << "\nAfter deallocate, the number of allocations in StatisticsArenaResource is " << arenaResource.numberOfAllocations() << ".\n";

            cout << "Before StatisticsArenaResource has gone out of scope, \n";
            cout << "  upstreamDataResource has " << upstreamDataResource.numberOfAllocations() << " allocations.\n";
            cout << "  upstreamStatisticsResource has " << upstreamStatisticsResource.numberOfAllocations() << " allocations.\n";
#ifdef NO_HEAP
            assert(upstreamDataResource.numberOfAllocations() > 0);
            assert(upstreamStatisticsResource.numberOfAllocations() > 0);
#endif

            // Double free will throw an exception which will call std::terminate.
            // Uncomment the next line to test double-free detection.
            // allocator.deallocate(aPtr[0], aN[0]);

            // The outcome will be:
            /*
            terminate called after throwing an instance of 'std::runtime_error'
            what():  Attempt to deallocate from an address which does not hold allocated data.
            */
        }
        catch (const std::bad_alloc& e) {
            cout << "std::bad_alloc has been thrown. e.what() = " << e.what() << '\n';
        }
        cout << "When the StatisticsArenaResource has gone out of scope, \n";
        cout << "  upstreamDataResource has " << upstreamDataResource.numberOfAllocations() << " allocations.\n";
        cout << "  upstreamStatisticsResource has " << upstreamStatisticsResource.numberOfAllocations() << " allocations.\n";
        assert(upstreamDataResource.numberOfAllocations() == 0);
        assert(upstreamStatisticsResource.numberOfAllocations() == 0);
   }
    return 0;
}