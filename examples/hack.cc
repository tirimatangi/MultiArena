#include <array>
#include <list>
#include <vector>
#include <cmath>
#include <numeric>
#include <cassert>
#include <iostream>
#include <tuple>

#include <MultiArena/MultiArena.h>

using std::array;
using std::vector;
using std::cout;
using std::list;
int main()
{
    {
        MultiArena::StatisticsArenaResource arenaResource(16, 1024);
        auto oldDefaultResource = std::pmr::set_default_resource(&arenaResource);

        std::pmr::vector<int> vectors[4];
        for (int i = 0; i < 4; ++i) {
            vectors[i].resize(10*(i+1));
            std::cout << "vectors["<<i<<"].size() = " << vectors[i].size() <<  " ints, "
                      << "number of allocated chucks = " << arenaResource.numberOfAllocations() << '\n';
        }

        auto pMap = arenaResource.addressToBytesMap(); // Returns const pointer to an std::map.
        cout << "\nAddress map has " << arenaResource.addressToBytesMap()->size() << " active allocations:\n";
        for (const auto& val : *pMap)
            cout << "  Address " << std::hex << val.first << std::dec << " has " << val.second << " bytes\n";

        std::pmr::set_default_resource(oldDefaultResource);
    }
    return 0;

    {
            MultiArena::UnsynchronizedArenaResource<16, 1024> unsynchronized_in_stack;
    MultiArena::UnsynchronizedArenaResource unsynchronized_in_heap(16, 1024);

    MultiArena::SynchronizedArenaResource<16, 1024> synchronized_in_stack;
    MultiArena::SynchronizedArenaResource synchronized_in_heap(16, 1024);


        using T = std::pair<int, float>;
        MultiArena::UnsynchronizedArenaResource<16, 256> arenaResource;
        // Allocator driving the MultiArena.
        std::pmr::polymorphic_allocator<T> allocator(&arenaResource);

        // This many objects fit in one arena (== 256 / 8 == 32)
        constexpr std::size_t maxObjectsPerArena = arenaResource.arenaSize() / sizeof(T);

        // A raw pointer for each chunk of maxObjectsPerArena objects
        T* chunk[arenaResource.numArenas()];

        // Consume the entire arena resource.
        std::cout << "Allocating...\n";
        for (std::size_t i = 0; i < arenaResource.numArenas(); ++i)
            chunk[i] = allocator.allocate(maxObjectsPerArena);
        std::cout << "  Number of busy arenas = " << arenaResource.numberOfBusyArenas() << '\n';
        std::cout << "  Number of all  arenas = " << arenaResource.numArenas() << '\n';

        // Deallocate everything
        std::cout << "Deallocating...\n";
        for (std::size_t i = 0; i < arenaResource.numArenas(); ++i)
            allocator.deallocate(chunk[i], maxObjectsPerArena);
        std::cout << "  Number of busy arenas = " << arenaResource.numberOfBusyArenas() << '\n';
        std::cout << "  Number of all  arenas = " << arenaResource.numArenas() << '\n';
    }
//    return 0;

    {
        MultiArena::StatisticsArenaResource arenaResource(16,1024);
        using T = std::pair<int, double>;
        std::shared_ptr<T> p1, p2;
        {
            auto ptr = MultiArena::makePolymorphicUnique<T>(&arenaResource, 10, 3.14);
            std::cout << "unique Bytes = " << arenaResource.bytesAllocated() << "\n";
        }

        {
            auto ptr = std::allocate_shared<T>(std::pmr::polymorphic_allocator<T>(&arenaResource), 10, 3.14);
            std::cout << "*ptr = {" << ptr->first << ',' << ptr->second << "}\n";
            // Make two more references.
            p1 = ptr; p2 = ptr;
            std::cout << "Number of allocations before ptr gone out of scope: "
                      << arenaResource.numberOfAllocations() << '\n';
            std::cout << "  Shared pointer use count = " << p2.use_count() << '\n';
        }
        std::cout << "Bytes = " << arenaResource.bytesAllocated() << "\n";

        std::cout << "Number of allocations after  ptr gone out of scope: "
                    << arenaResource.numberOfAllocations() << '\n';
        std::cout << "  Shared pointer use count = " << p2.use_count() << '\n';
        p1.reset();
        std::cout << "Number of allocations after  p1 released: "
                    << arenaResource.numberOfAllocations() << '\n';
        std::cout << "  Shared pointer use count = " << p2.use_count() << '\n';
        p2.reset();
        std::cout << "Number of allocations after  p2 released: "
                    << arenaResource.numberOfAllocations() << '\n';
        std::cout << "  Shared pointer use count = " << p2.use_count() << '\n';
        std::cout << "Bytes = " << arenaResource.bytesAllocated() << "\n";
    }
// *ptr = {10,3.14}
// Number of allocations before ptr gone out of scope: 1
//   Shared pointer use count = 3
// Number of allocations after  ptr gone out of scope: 1
//   Shared pointer use count = 2
// Number of allocations after  p1 released: 1
//   Shared pointer use count = 1
// Number of allocations after  p2 released: 0
//   Shared pointer use count = 0

  //  return 0;
// Output:
//  *ptr = {10,3.14}
//  Number of allocations before ptr gone out of scope: 1
//  Number of allocations after  ptr gone out of scope: 0


    {
        MultiArena::UnsynchronizedArenaResource arenaResource(16, 1024, std::pmr::new_delete_resource());
        std::pmr::list<int> lst(&arenaResource); // Could be any std container (list, deque, map, ...)
        for (int i = 0; i < 256; ++i)
            lst.push_back(i);  // Will do one allocation per push_back.

        std::cout << "lst.size() = " << lst.size() <<  ", "
                  << "number of allocated memory chucks = " << arenaResource.numberOfAllocations()
                  << ",\n";
    }
    //return 0;

    // Example 1.1: Use std-containers with a MultiArena memory resource.
    cout << "\n*** Example 1.1 *** How to use std containers with MultiArena allocator.\n";
    {
        auto runDemo = [](auto* memoryResource, const char* info)
        {
            std::pmr::vector<int> vec(memoryResource); // Could be any std container (list, deque, map, ...)
            for (int i : {1,2,3,4,5,6,7,8})
                vec.push_back(i);

            cout << "  Integer vector allocated from a polymorphic resource (" << info << ")\n";
            cout << "    vector = { ";
            for (auto x : vec)
                cout << x << ' ';
            cout << "}\n";
            cout << "    Number of allocations before the vector goes out of scope = " << memoryResource->numberOfAllocations() << '\n';
        };

        MultiArena::UnsynchronizedArenaResource<16, 1024> stackArenaResource;
        runDemo(&stackArenaResource, "living on the stack");
        // Now all memory should have been released.
        cout << "    Number of allocations after the vector is released = " << stackArenaResource.numberOfAllocations() << '\n';

        MultiArena::UnsynchronizedArenaResource heapArenaResource(16, 1024);
        runDemo(&heapArenaResource, "living on the heap");
        // Now all memory should have been released.
        cout << "    Number of allocations after the vector is released = " << heapArenaResource.numberOfAllocations() << '\n';
    }

    // Example 1.2: Use std::unique_ptr with a MultiArena memory resource.
    //              Note that MultiArena::PolymorphicDeleter custom deleter is
    //              used automatically.
    cout << "\n*** Example 1.2 *** Allocate an object and wrap it into an std::unique_ptr.\n";
    {
        struct MyStruct
        {
            MyStruct(char cc, int ii, double dd) : c(cc), i(ii), d(dd) {}
            char c = 0;
            int i = 0;
            float d = 0;
            ~MyStruct()
            {
                std::cout << "      ~MyStruct {'" << c << "' " << i << " " << d << "}\n";
            }
        };

        auto runDemo = [](auto* memoryResource, const char* info)
        {
            cout << "  Allocating a struct of {char, int, float} for a unique_ptr (" << info << ")...\n";
            MultiArena::PolymorphicUniquePointer<MyStruct> uniquePtr =
                MultiArena::makePolymorphicUnique<MyStruct>(memoryResource, 'X', 12, 3.14);
            cout << "    *uniquePtr = {'" << uniquePtr->c <<"' " << uniquePtr->i << " " << uniquePtr->d << "}\n";
            cout << "    Number of allocations after the unique_ptr has been allocated = " << memoryResource->numberOfAllocations() << '\n';
            return uniquePtr;
        };

        MultiArena::UnsynchronizedArenaResource<16, 1024> stackArenaResource;
        {
            auto unqPtr = runDemo(&stackArenaResource, "living on the stack");
            cout << "    Number of allocations after the unique_ptr is returned = " << stackArenaResource.numberOfAllocations() << '\n';
        }
        cout << "    Number of allocations after the unique_ptr is released = " <<  stackArenaResource.numberOfAllocations() << '\n';

        MultiArena::UnsynchronizedArenaResource heapArenaResource(16, 1024);
        {
            auto unqPtr = runDemo(&heapArenaResource, "living on the heap");
            cout << "    Number of allocations after the unique_ptr is returned = " << heapArenaResource.numberOfAllocations() << '\n';
        }
        cout << "    Number of allocations after the unique_ptr is released = " <<  heapArenaResource.numberOfAllocations() << '\n';
    }

    // Example 1.3: Use std::shared_ptr with a MultiArena memory resource.
    cout << "\n*** Example 1.3 *** Allocate an object and wrap it into an std::shared_ptr.\n";
    {
        struct MyStruct
        {
            MyStruct(char cc, int ii, double dd) : c(cc), i(ii), d(dd) {}
            char c = 0;
            int i = 0;
            float d = 0;
            ~MyStruct()
            {
                std::cout << "      ~MyStruct {'" << c << "' " << i << " " << d << "}\n";
            }
        };

        auto runDemo = [](auto* memoryResource, const char* info)
        {
            cout << "  Allocating a struct of {char, int, float} for a shared_ptr (" << info << ")...\n";
            std::pmr::polymorphic_allocator<MyStruct> polymorphicAllocator(memoryResource);
            auto sharedPtr = std::allocate_shared<MyStruct>(polymorphicAllocator, 'Y', 24, 6.28);
            cout << "    Number of allocations after shared_ptr has been allocated = " << memoryResource->numberOfAllocations() << '\n';
            return sharedPtr;
        };

        MultiArena::UnsynchronizedArenaResource<16, 1024> stackArenaResource;
        {
            auto sharedPtr = runDemo(&stackArenaResource, "living on the stack");
            cout << "    *sharedPtr = {'" << sharedPtr->c <<"' " << sharedPtr->i << " " << sharedPtr->d << "}\n";
            cout << "    Number of allocations after the shared_ptr is returned = " << stackArenaResource.numberOfAllocations() << '\n';
        }
        cout << "    Number of allocations after the shared_ptr is released = " << stackArenaResource.numberOfAllocations() << '\n';

        MultiArena::UnsynchronizedArenaResource heapArenaResource(16, 1024);
        {
            auto sharedPtr = runDemo(&heapArenaResource, "living on the heap");
            cout << "    *sharedPtr = {'" << sharedPtr->c <<"' " << sharedPtr->i << " " << sharedPtr->d << "}\n";
            cout << "    Number of allocations after the shared_ptr is returned = " << heapArenaResource.numberOfAllocations() << '\n';
        }
        cout << "    Number of allocations after the shared_ptr is released = " << heapArenaResource.numberOfAllocations() << '\n';
    }

    // Example 1.4: Demonstrate how to use arena resource with std::pmr::polymorphic_allocator.
    //              Also show how exception derived from std::bad_alloc can be used for finding
    //              out the reason for a bad allocation.
    cout << "\n*** Example 1.4 *** Use std::pmr::polymorphic_allocator and find out the reason\n";
    cout << "                    in case a std::bad_alloc is thrown.\n";
    if constexpr (MultiArena::exceptionsEnabled == false)
    {
        cout << "  !! Skipped because MultiArena::exceptionsEnabled == false !!\n"
             << "  Unset flag MULTIARENA_DISABLE_EXCEPTIONS to enable exceptions.\n";
    }
    else
    {
        using T = double;  //...or any custom class.

        MultiArena::UnsynchronizedArenaResource<16, 256> arenaResource;
        std::pmr::polymorphic_allocator<T> alloc(&arenaResource);

        // Allocate as many objects of type T as it fits in one arena.
        std::size_t maxObjectsPerArena = arenaResource.arenaSize() / sizeof(T);
        cout << "  Allocating an array of " << maxObjectsPerArena << " objects with one allocation...\n";
        T* pT = alloc.allocate(maxObjectsPerArena);
        cout << "  1. Number of allocations = " << arenaResource.numberOfAllocations()
             << ", number of busy arenas = " << arenaResource.numberOfBusyArenas() << ".\n";

        alloc.deallocate(pT, maxObjectsPerArena);
        cout << "  2. Number of allocations after freeing " << maxObjectsPerArena << " objects = " << arenaResource.numberOfAllocations() << '\n';
        pT = nullptr;
        cout << "  Trying to allocate " << maxObjectsPerArena + 1 << " objects...\n";
        try {
            pT = alloc.allocate(maxObjectsPerArena + 1);
        }
        catch (const MultiArena::AllocateTooLargeBlock& e) {
          cout << "    Attempt to allocate too a large chunk of memory.\n"
               << "    exception = " << e.what()
               << ", bytes needed = " << e.bytesNeeded
               << ", bytes available " << e.bytesAvailable << "\n";
        }
        cout << "  3. After exception the number of allocations = " << arenaResource.numberOfAllocations()
             << ", number of busy arenas = " << arenaResource.numberOfBusyArenas() << ".\n";
        // The memory resource is stll usable for allocation of smaller chunks.
        assert(pT == nullptr);

        cout << "  Allocating " << arenaResource.numArenas() + 1 << " sets of "
             << maxObjectsPerArena << " objects (which is one set too many)...\n";
        std::array<T*, arenaResource.numArenas()> aPointers;
        try {
          for (int i = 0; i < int(aPointers.size()) + 1; ++i)
            aPointers[i] = alloc.allocate(maxObjectsPerArena);
        }
        catch (const MultiArena::OutOfFreeArenas& e) {
            cout << "    exception = " << e.what() << ", all " << e.numArenas << " arenas are already occupied.\n";
        }
        cout << "  4. After exception the number of allocations = " << arenaResource.numberOfAllocations()
             << ", number of busy arenas = " << arenaResource.numberOfBusyArenas() << ".\n";
        cout << "     So the memory resource contains "
             << arenaResource.numberOfBusyArenas() << " * " << maxObjectsPerArena
             << " objects which were allocated before the exception.\n";

        // Deallocate and verify that the memory resource became empty.
        for (T* p : aPointers)
          alloc.deallocate(p, maxObjectsPerArena);
        cout << "  5. After deallocating everything, the number of allocations = " << arenaResource.numberOfAllocations()
             << ", number of busy arenas = " << arenaResource.numberOfBusyArenas() << ".\n";
        assert(arenaResource.numberOfAllocations() == 0);
    }
    return 0;
}