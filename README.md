# Polymorphic memory resource with constant allocation and deallocation times for real-time applications

**MultiArena** is a header-only library written in C++17.
It implements [a polymorphic memory resource](https://en.cppreference.com/w/cpp/memory/memory_resource) to be used with
[a polymorphic allocator](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator).
The allocation and deallocation times are constant, which makes it suitable for real-time applications.

## MultiArena algorithm

### Initialization of the memory resource

The entire memory resource is preallocated upon the time of construction from the stack (or static memory),
or alternatively using another (a.k.a upstream) memory resource in the constructor.
After the initialization phase, the upstream resource is not accessed any more.

The memory in the resource is divided into a fixed number of arenas.
Each arena consists of a fixed number of bytes.
Each arena has the same size.

### Management of the arenas

Initially every arena is empty and they are placed in a set of free arenas.
The first allocation request taps an arena and marks it
as active. From now on, all requested chunks of memory are carved from
the active arena until there is too little memory left to satisfy a new request.
Then the active arena is declared as full and the next free arena is tapped from the set
and marked as active.

The occupied arenas know how many allocations they hold. Hence, deallocations are very simple.
Only the allocation counter is decremented. Once the counter reaches zero, we know that
every allocation in the arena has been deallocated and the arena has become empty again.
It can now be returned to the list of free arenas.

### Synchronized vs unsynchronized memory resources

MultiArena resource classes come in synchronized and unsynchronized variants,
just like [std::pmr::synchronized_pool_resource](https://en.cppreference.com/w/cpp/memory/synchronized_pool_resource)
and [std::pmr::unsynchronized_pool_resource](https://en.cppreference.com/w/cpp/memory/unsynchronized_pool_resource).
The unsynchronized version is fast and thread-unsafe whereas the synchronized version is a bit slower and thread-safe.
The synchronized version uses locks on allocation so if several threads are using the same memory resource,
they will sometimes have to wait each other. Deallocation is mostly lock-free even though a lock is acquired
when an arena becomes empty and is returned to the list of free arenas.

The following example shows how to allocate synchronized and unsynchronized resources which live in either stack or heap.
In each case, there are 16 arenas, containing 1024 bytes each.

```c++
    MultiArena::UnsynchronizedArenaResource<16, 1024> unsynchronized_in_stack;
    MultiArena::UnsynchronizedArenaResource unsynchronized_in_heap(16, 1024);

    MultiArena::SynchronizedArenaResource<16, 1024> synchronized_in_stack;
    MultiArena::SynchronizedArenaResource synchronized_in_heap(16, 1024);
```

In [this chapter](https://github.com/tirimatangi/MultiArena#multiarena-resource-living-in-heap-or-in-another-resource) below we show how to store the data in memory reserved from an upstream memory resource instead of the heap (a.k.a. `std::pmr::new_delete_resource`).

During the debugging phase, when the number and the size of the arenas may not be known as yet,
the above memory resources can be replaced with a debug helper.
More on that [below](https://github.com/tirimatangi/MultiArena#debug-helper-resource-for-tuning-the-number-of-arenas-and-arena-sizes).

### Member functions for status inquiry

MultiArena classes have a few methods for inquiring the status of the memory resource.

The methods are as follows:

- `numArenas()` returns the number of arenas in the resource. It is `constexpr` if the memory resource is statically allocated.
- `arenaSize()` returns the size of each arena. It is `constexpr` if the memory resource is statically allocated.
- `numberOfAllocations()` returns the current number of allocations in the resource. The value is indeed the allocation count, not the number of allocated bytes.
- `numberOfBusyArenas()` reports how many arenas are busy. An arena is regarded as busy if there is at least one active allocation refering to the address space of the arena.

The latter two methods can for instance be used in an assert to detect possible memory leaks.
Note that this is not trivially possible with standard new-delete allocator.

Examples on calling these methods can be found in [example-1.cc](examples/example-1.cc).

## Using MultiArena with std-containers

Every dynamic container in `std` library like
[vector](https://en.cppreference.com/w/cpp/container/vector),
[list](https://en.cppreference.com/w/cpp/container/list),
[map](https://en.cppreference.com/w/cpp/container/map) and
[string](https://en.cppreference.com/w/cpp/string/basic_string)
have polymorphic counterparts in `std::pmr` namespace.
The polymorphic versions draw memory from the given
[memory resource](https://en.cppreference.com/w/cpp/memory/memory_resource)

Let's look at some examples on how MultiArena can be used as the memory resource
for `std` containers. These examples run on a single thread,
so `UnsynchronizedArenaResource` will be used. In a multi-threaded case, simply replace
`UnsynchronizedArenaResource` with `SynchronizedArenaResource`.

### MultiArena resource living in stack

```c++
    MultiArena::UnsynchronizedArenaResource<16, 1024> arenaResource; // 16 arenas, 1024 bytes/arena
    std::pmr::vector<int> vec(&arenaResource); // Could be any std container (list, deque, map, ...)
    vec.reserve(8);  // The entire vector will fit in a single arena
    for (int i : {1,2,3,4,5,6,7,8})
        vec.push_back(i);

    std::cout << "vec.size() = " << vec.size() <<  ", "
              << "number of allocated memory chunks = " << arenaResource.numberOfAllocations() << ".\n";
// Output:
// vec.size() = 8, number of allocated memory chunks = 1.
```

### MultiArena resource living in heap or in another resource

```c++
    MultiArena::UnsynchronizedArenaResource arenaResource(16, 1024); // 16 arenas, 1024 bytes/arena
    std::pmr::list<int> lst(&arenaResource);
    for (int i = 0; i < 256; ++i)
        lst.push_back(i);  // Will do one allocation per push_back.

    std::cout << "lst.size() = " << lst.size() <<  ", "
                << "number of allocated memory s = " << arenaResource.numberOfAllocations() << ".\n";
// Output:
// lst.size() = 256, number of allocated memory chunks = 256.
```
In this example, the memory for the arenas was allocated by default from the standard
[new-delete resource](https://en.cppreference.com/w/cpp/memory/new_delete_resource).
However, any upstream resource can be used by simply passing it to a MultiArena resource like so:
```c++
        MultiArena::UnsynchronizedArenaResource arenaResource(16, 1024, &my_upstream_resource);
        // like above from this on...
```

For a runnable example, see Example 1.1 in [example-1.cc](examples/example-1.cc).

## Using MultiArena with unique pointers

`std::unique_ptr` can own an object allocated from a MultiArena resource and deallocate it automatically when the object goes out of scope. The object can be allocated and passed to a unique pointer with function `MultiArena::makePolymorphicUnique<T>(pmr, args...)`.
The function returns a unique pointer pointing to an object of type `T`. The object has been is allocated from the MultiArena resource pointed by `pmr` and constructed with constructor `T(args...)`.

Let's look at an example.

```c++
    MultiArena::UnsynchronizedArenaResource<16, 1024> arenaResource;
    using T = std::pair<int, double>;
    {
        auto ptr = MultiArena::makePolymorphicUnique<T>(&arenaResource, 10, 3.14);
        std::cout << "*ptr = {" << ptr->first << ',' << ptr->second << "}\n";
        std::cout << "Number of allocations before ptr gone out of scope: "
                    << arenaResource.numberOfAllocations() << '\n';
    }
    std::cout << "Number of allocations after  ptr gone out of scope: "
                << arenaResource.numberOfAllocations() << '\n';
// Output:
//  *ptr = {10,3.14}
//  Number of allocations before ptr gone out of scope: 1
//  Number of allocations after  ptr gone out of scope: 0
```

For a runnable example, see Example 1.2 in [example-1.cc](examples/example-1.cc).

## Using MultiArena with shared pointers

A shared pointer pointing to data allocated from MultiArena (or any other polymorphic resource)
can be constructed with library function
[std::allocate_shared](https://en.cppreference.com/w/cpp/memory/shared_ptr/allocate_shared).
Notice that the first parameter will have to be a `std::pmr::polymorphic_allocator` which
drived the memory resource, not the memory resource itself.
Otherwise, the usage is very similar to that of the unique_ptr above. Both the payload data and
the control block required for shared_ptr's internal book keeping are allocated from
the memory resource with a single allocation.
Here is an example.

```c++
    MultiArena::UnsynchronizedArenaResource arenaResource(16, 1024);
    using T = std::pair<int, double>;
    std::shared_ptr<T> p1, p2;
    {
        auto ptr = std::allocate_shared<T>(std::pmr::polymorphic_allocator<T>(&arenaResource), 10, 3.14);
        std::cout << "*ptr = {" << ptr->first << ',' << ptr->second << "}\n";
        // Make two more references.
        p1 = ptr; p2 = ptr;
        std::cout << "Number of allocations before ptr gone out of scope: " << arenaResource.numberOfAllocations() << '\n';
        std::cout << "  Shared pointer use count = " << p2.use_count() << '\n';
    }
    std::cout << "Number of allocations after  ptr gone out of scope: " << arenaResource.numberOfAllocations() << '\n';
    std::cout << "  Shared pointer use count = " << p2.use_count() << '\n';
    p1.reset();
    std::cout << "Number of allocations after  p1 released: " << arenaResource.numberOfAllocations() << '\n';
    std::cout << "  Shared pointer use count = " << p2.use_count() << '\n';
    p2.reset();
    std::cout << "Number of allocations after  p2 released: " << arenaResource.numberOfAllocations() << '\n';
    std::cout << "  Shared pointer use count = " << p2.use_count() << '\n';
// Output:
// *ptr = {10,3.14}
// Number of allocations before ptr gone out of scope: 1
//   Shared pointer use count = 3
// Number of allocations after  ptr gone out of scope: 1
//   Shared pointer use count = 2
// Number of allocations after  p1 released: 1
//   Shared pointer use count = 1
// Number of allocations after  p2 released: 0
//   Shared pointer use count = 0
```

For a runnable example, see Example 1.3 in [example-1.cc](examples/example-1.cc).


## Using MultiArena with std::pmr::polymorphic_allocator

[std::pmr::polymorphic_allocator](https://en.cppreference.com/w/cpp/memory/polymorphic_allocator) allows you to
carve memory from a memory resource into a raw pointer, just like operator `new`.
We demonstrate this by allocating an array of as many objects of type `T` as
it fits in a single arena. Note that this is the maximum amount of memory
one can extract in a single allocation as the allocation will fill the entire arena.
In total, there can be as many such allocations as there are arenas.

```c++
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
// Output:
// Allocating...
//   Number of busy arenas = 16
//   Number of all  arenas = 16
// Deallocating...
//   Number of busy arenas = 0
//   Number of all  arenas = 16
```

For a runnable example, see Example 1.4 in [example-1.cc](examples/example-1.cc).


## On exceptions

There are two possible reasons why an allocation can fail.

1. The size of the requested chunk of memory is larger than the size of the arena. The size of the arena is fixed when the memory resource is allocated. For example, if the memory resource has 16 arenas, each of which has 1024 bytes, the maximum size of the requested chunk will be 1024 bytes regardless of how many arenas are still free.

2. Every arena is already busy (i.e. holding unreleased allocations) and the remaining amount of memory in the active arena (i.e. the arena from which the memory is currently extracted from) is not enough for the requested chunk.

In both cases, an exception derived from `std::bad_alloc` will be thrown (unless the exceptions have been disabled with a compiler flag, see below) and `nullptr` will be returned.

In the first case, the type of the exception object will be `MultiArena::AllocateTooLargeBlock`. It has two member variables.
- `bytesNeeded` tells the size of the requested chunk in bytes.
- `bytesAvailable` tells the maximum allowed size of an allocation.

In the second case, the type of the exception object will be `MultiArena::OutOfFreeArenas`.
It has a member variable `numArenas` which tells how many arenas the memory resource has in total.

Note that an exception does not mean corruption. The memory resource is still alive after an exception
and can be used normally.

For a runnable example on how to catch these exceptions and recover,
see Example 1.4 in [example-1.cc](examples/example-1.cc).

### No exceptions? No problems!

Exceptions may be prohibited in some applications. In that case you can turn them off by defining
flag MULTIARENA_DISABLE_EXCEPTIONS (i.e. `-D MULTIARENA_DISABLE_EXCEPTIONS`) when compiling.
Now a failed allocation will return `nullptr` without throwing an exception.

## Debug helper resource for tuning the number of arenas and arena sizes

In addition to the four MultiArena resource classes explained above,
there is a fifth class called `MultiArena::StatisticsArenaResource`.
It helps you optimize the capacity of arena resources
in terms of how many and how large arenas the resource should own.
It can also help you trouble-shoot memory leaks by telling
the addresses of active memory allocations.

`StatisticsArenaResource` behaves otherwise like a thread-safe `SynchronizedArenaResource` except
that it keeps track of the addresses of the allocated chunks and their sizes. This information
is stored internally in a map.
Normally it is not the duty of a MultiArena resource to keep track of this information,
so `StatisticsArenaResource` is a bit slower than the other four versions.

The constructor is like so:

```c++
    StatisticsArenaResource(SizeType numArenas,
                            SizeType arenaSize,
                            std::pmr::memory_resource* mrData = nullptr,
                            std::pmr::memory_resource* mrStatistics = nullptr)
```

- If the 3rd argument `mrData` is not null, the arenas will be stored in memory
extracted from the given upstream memory resource.
- If the 4th argument `mrStatistics` is not null, the statistical information will be stored in memory
extracted from the given upstream memory resource.
- A null pointer means that the default system heap will be used.

Example 3.2 in [example-3.cc](examples/example-3.cc)
shows how to use other MultiArenas as upstream resources to avoid heap usage.

Let's look at an example where the we allocate four vectors from the memory resource
and dig out the allocated addresses and allocation sizes from the map.

```c++
    MultiArena::StatisticsArenaResource arenaResource(16, 1024);
    auto oldDefaultResource = std::pmr::set_default_resource(&arenaResource);

    // Allocate 4 vectors
    std::pmr::vector<int> vectors[4];
    for (int i = 0; i < 4; ++i) {
        vectors[i].resize(10*(i+1));
        std::cout << "vectors["<<i<<"].size() = " << vectors[i].size() <<  " ints, "
                    << "number of allocated chunks = " << arenaResource.numberOfAllocations() << '\n';
    }

    // Print {address, allocation size} - pairs.
    auto pMap = arenaResource.addressToBytesMap(); // Returns const pointer to an std::map.
    cout << "\nAddress map has " << pMap->size() << " active allocations:\n";
    for (const auto& val : *pMap)
        cout << "  Address " << std::hex << val.first << std::dec << " has " << val.second << " bytes\n";

    std::pmr::set_default_resource(oldDefaultResource);
// output:
// vectors[0].size() = 10 ints, number of allocated chunks = 1
// vectors[1].size() = 20 ints, number of allocated chunks = 2
// vectors[2].size() = 30 ints, number of allocated chunks = 3
// vectors[3].size() = 40 ints, number of allocated chunks = 4
//
// Address map has 4 active allocations:
//   Address 0x560e0bf3b1c0 has 160 bytes
//   Address 0x560e0bf3b260 has 120 bytes
//   Address 0x560e0bf3b2d8 has 80 bytes
//   Address 0x560e0bf3b328 has 40 bytes
```
Example 3.1 in [example-3.cc](examples/example-3.cc)
is an important example which demonstrates how exceptions and the statictical resource can be used
together to optimize the sizes and the number of arenas. In the example, the capacity of the memory resource
is progressively increased until the mockup application runs smoothly.


### Helper methods in StatisticsArenaResource

With the following methods you can glean information about the state of the memory resource.
Example 3.2 in [example-3.cc](examples/example-3.cc)
shows a runnable example of each of these methods.

- `addressToBytesMap()` returns a const pointer to `std::pmr::map<void*,uint32_t>`. It maps an allocated (but not yet deallocated) address to the number of bytes requested at the said allocation. Note that the space for the map is allocated from the system heap.
- `bytesAllocated()` returns the aggregate number of bytes from all active allocations. Note that number of active allocations can be found with either `arenaResource.numberOfAllocations()` or `arenaResource.addressToBytesMap()->size()`.
- `histogram()` returns an `std::pmr::map<uint32_t, uint32_t>` which maps an allocation size in bytes to the frequency of such allocations. That is, it tells how many chunks of the given size there are in the set of active allocations. For example, if there are 10 allocations of 256 bytes, then `arenaResource.histogram().at(256) == 10`. The memory for the histogram is allcoated from the system heap.

The next 3 methods use the histogram to calculate the percentile, mean and standard deviation of the distribution of active allocations.

- `percentile(double pc)`, where `pc=0...1` return a block size such that (100*pc)% of all allocated blocks are smaller or equal to the returned value. for example, `arenaResource.percentile(0.5)` returns the median size of active allocations.
- `mean()` returns the mean size of allocated blocks.
- `stdDev()` returns the standard deviation of allocated blocks.

## Example use case

Suppose you get compressed images from a camera at regular intervals.
The size of each image is different. Due to the compression,
the image sizes depend on the amount of details and can vary a lot.
Each image goes to analysis which is done in parallel on several threads.
The analysis takes a random amount of time depending on whether or not there is
anything interesting in the image. Once the analysis is done, the image
is not needed anymore.
So the sizes as well as the life times of the images are random but limited.

The images are stored into memory allocated from a MultiArena resource.
The size of the arenas should be properly tuned to accommodate a reasonable
number of images per arena.
Likewise, the number of the arenas must be tuned according to the frame rate
and the time the analysis takes so that the arenas don't run out during the operation.

## So, was it worth it?

[example-2.cc](examples/example-2.cc)
simulates the use case described above. On each round, the mock-up application
allocates a random chunk of memory.
It then writes something to the chunk and keeps it allocated for a random number of rounds.
Before the chunk expires, the application reads the memory and verifies that the contents have not changed.
Once this is done, the chunk is deallocated.
The same run is done using both unsynchronized mode with a single thread or synchronized mode with 16 threads.

The unsynchronized test is run with three different memory resources:
- `MultiArena::UnsynchronizedArenaResource`
- [std::pmr::new_delete_resource](https://en.cppreference.com/w/cpp/memory/new_delete_resource)
- [std::pmr::unsynchronized_pool_resource](https://en.cppreference.com/w/cpp/memory/unsynchronized_pool_resource)

Likewise, the synchronized test is also run three memory resources:
- `MultiArena::SynchronizedArenaResource`
- [std::pmr::new_delete_resource](https://en.cppreference.com/w/cpp/memory/new_delete_resource)
- [std::pmr::synchronized_pool_resource](https://en.cppreference.com/w/cpp/memory/synchronized_pool_resource)

The measured time includes everything, i.e. the time needed for memory allocation and the time needed
for reading and writing into the memory. This way the effect of cache locality are taken into account.

The results are as follows:

1. In unsynchronized mode (i.e. one thread only), the application using the arena resource is
   - +22% faster than the default `std::pmr::new_delete_resource`.
   - +10% faster than `std::pmr::unsynchronized_pool_resource` (which uses the new_delete_resource as the upstream allocator).

2. In synchronized mode with 16 parallel threads, the application using the arena resource is
   - -1% to -5% _slower_ than the default `std::pmr::new_delete_resource`.
   - +7% to +10% faster than `std::pmr::synchronized_pool_resource` (which uses the new_delete_resource as the upstream allocator).

The tests were run in (an ancient) Core i5-4210U machine with 4 cores on Ubuntu 22.04.

We note that in the synchronized mode, the default new-delete resource is very good.
In every other case, the MultiArena resource is faster.
But even so, the difference in speed is not necessarily the most significant argument for choosing a MultiArena resource.
The most important argument is the peace of mind. The allocations and deallocations
run in constant time. The memory resource is entirely your playground which is immune to memory fragmentation and
disturbance from the other processes which may be running in the system.

Just the ticket for a real-time application.

## Compilation

The easiest way to compile all examples is to do
`cmake -DCMAKE_BUILD_TYPE=Release examples` followed by `make`.
If you don't want to use cmake, the examples can be compiled manually one by one. For instance, <br>
`g++ examples/example-2.cc -std=c++17 -I include/ -O3 -pthread -o example-2`

Exceptions can be disabled by defining flag `MULTIARENA_DISABLE_EXCEPTIONS` like so <br>
`g++ examples/example-2.cc -I include/ -std=c++17 -O3 -pthread -DMULTIARENA_DISABLE_EXCEPTIONS`

The examples have been tested with g++ 11.2.0  and clang++ 14.0.0 but any compiler which complies with C++17 standard should do.
