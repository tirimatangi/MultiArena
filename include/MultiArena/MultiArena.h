#ifndef MULTIARENA_H
#define MULTIARENA_H

#include <array>
#include <vector>
#include <map>
#include <numeric>
#include <atomic>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <functional>
#include <memory>
#include <memory_resource>
#include <cstddef>
#include <cstdint>
#include <new>
#include <algorithm>
#include <cmath>

/**
 * This library implements 4 memory resources which can be used
 * with C++17 polymorphic memory resources.
 * See https://en.cppreference.com/w/cpp/memory/polymorphic_allocator
 *
 * In each memory resource, the allocation and deallocation
 * times are constant so they suit well for real-time applications.
 * Each memory resource has two parameters: the number of memory arenas
 * and the size of each arena. The size of each arena determines
 * the maximum size of an allocated object. The number of arenas
 * determine how many such maximum size objects fit in the memory resource.
 * On allocation, the algorithm packs as many objects as it can
 * into an arena before tapping the next free arena.
 * On deallocation, it waits until every allocation in an arena
 * has been freed before recycling the arena.
 * The memory resources are:
 * 1. UnsynchronizedArenaResource where the arenas are allocated from
 *    the stack or static memory.
 *    This variant is fast and cache-friendly but not thread-safe.
 * 2. UnsynchronizedArenaResource where the arenas are allocated from
 *    the "up-stream" memory resource given in the constructor.
 *    Ordinary system heap new-delete memory resource is used by default.
 *    Note that the up-stream resource is used only in the constructor
 *    when the arenas are initialized.
 *    This variant is fast but not thread-safe.
 * 3. SynchronizedArenaResource where the arenas are allocated from
 *    the stack or static memory.
 *    This variant is cache-friendly and thread-safe but slightly slower
 *    than the unsynchronized one.
 * 4. SynchronizedArenaResource where the arenas are allocated from
 *    the "up-stream" memory resource given in the constructor.
 *    Ordinary system heap new-delete memory resource is used by default.
 *    Note that the up-stream resource is used only in the constructor
 *    when the arenas are initialized.
 *    This variant thread-safe but slightly slower than the unsynchronized one.
 *
 * In addition there is a fifth memory resource (StatisticsArenaResource)
 * which can be used for finding a suitable arena size and the number of arenas.
 * It keeps track of the sizes of all allocations and the maximum
 * number of occipied arenas. These values can be requested with
 * member functions. It can also generate a histogram of allocations
 * for statistical analysis. This helps trouble-shoot memory leaks.
 *
 * Each memory resource throws an exception derived from std::bad_alloc
 * if all arenas are exhausted. The exception means that the allocation
 * operation failed but the memory resource is otherwise still alive
 * and can be used for smaller allocations.
 * Exceptions can be disabled by setting compiler flag MULTIARENA_DISABLE_EXCEPTIONS
 * The state of the flag is copied to constexpr bool exceptionsEnabled.
 *
 * Finally, helper function makePolymorphicUnique returns an std::unique_ptr
 * with an object allocated from a polymorphic memory resource.
 */

// Enable / disable asserts
// #define MULTIARENA_DEBUG 1

#if MULTIARENA_DEBUG
# include <iostream>
# include <sstream>
# define MULTIARENA_ASSERT(x) assert(x)
#else
# define MULTIARENA_ASSERT(x)
#endif

// Enable / disable exceptions
// #define MULTIARENA_DISABLE_EXCEPTIONS 1

#if MULTIARENA_DISABLE_EXCEPTIONS
// No exceptions needed
#else
#   include <exception>
#endif

namespace MultiArena
{
#if MULTIARENA_DISABLE_EXCEPTIONS
    static constexpr bool exceptionsEnabled = false;
#else
    static constexpr bool exceptionsEnabled = true;
#endif

#if MULTIARENA_DEBUG
// Helper function for debug prints.
template <class... Args>
void atomicPrint(Args&&... args)
{
    std::stringstream ss;
    (ss << ... << args);
    std::cout << ss.str() << std::flush;
}

// Helper class for variable type debugging. Example: whatType<decltype(x)>("Type of x=")();
template <class T>
struct whatType
{
    const char* _msg;
    whatType(const char* msg = "") : _msg(msg) {};
    void operator()()
    {
        atomicPrint(_msg, __PRETTY_FUNCTION__, '\n');
    }
};
#endif

// If SizeType is 32 bits, the size
// of a single arena (so also the size of a single allocation) can not exceed 4GB.
// There can be as many such arenas as needed so the total
// amount of memory in the pool can be very large.
// However, if bigger arenas are needed, use uint64_t.
using SizeType = uint32_t;

// Work out the cache line length.
#ifdef __cpp_lib_hardware_interference_size
    using std::hardware_constructive_interference_size;
#else  // Make a reasonable guess.
    constexpr std::size_t hardware_constructive_interference_size = 64;
#endif


// Exception for detecting an attempt to allocate a too large block of memory.
struct AllocateTooLargeBlock : std::bad_alloc
{
    AllocateTooLargeBlock(std::size_t bytesNeededIn, std::size_t bytesAvailableIn ) :
        std::bad_alloc(), bytesNeeded(bytesNeededIn), bytesAvailable(bytesAvailableIn)
    { }
    // Tells how large the arena should have been to satisfy the request.
    std::size_t bytesNeeded = 0;
    // Tells how many bytes actually were available.
    std::size_t bytesAvailable = 0;
};

// Exception which reports that we have run out of free arenas.
struct OutOfFreeArenas : std::bad_alloc
{
    OutOfFreeArenas(std::size_t numArenasIn) :
        std::bad_alloc(), numArenas(numArenasIn)
    { }
    // The original number of arenas in the memory resource, none of which are still available.
    std::size_t numArenas = 0;
};

// Exception for memory corruption detection.
struct ArenaMemoryResourceCorruption : std::runtime_error
{
    ArenaMemoryResourceCorruption(void* addressIn, std::size_t bytesIn, std::size_t alignmentIn) :
        std::runtime_error("Double-free or memory corruption in polymorphic memory resource."),
        address(addressIn), bytes(bytesIn), alignment(alignmentIn)
    { }
    void* address = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
};

template <SizeType NUM_ARENAS = 0, SizeType ARENA_SIZE = 0>
class UnsynchronizedArenaResource;

// Base class for all variants of polymorphic memory resources.
template <class Derived>
class UnsynchronizedArenaResourceBase : public std::pmr::memory_resource
{
public:
    UnsynchronizedArenaResourceBase(SizeType numArenas, SizeType arenaSize)
    {}

    // Total number of allocations combined in all arenas.
    std::size_t numberOfAllocations()
    {
        std::size_t result = 0;
        for (SizeType i = 0; i < derived()->numArenas(); ++i)
            result += allocationsInArena(i);
        return result;
    }

    // Number of non-empty arenas.
    SizeType numberOfBusyArenas()
    {
        auto result = derived()->numArenas() - _freeListHead;
        // The active arena is counted as a busy even if there
        // are no allocations yet.
        // If the active arena is the only one that possibly have allocations,
        // we must check if the arena is actually empty.
        if (result == 1 && allocationsInArena(_activeArenaId) == 0)
            result = 0;
        return result;
    }

protected:
    void initializeArenas()
    {
        // Initialize arena free list
        for (SizeType i = 0; i < derived()->numArenas(); ++i) {
            derived()->_freeList[i] = derived()->numArenas() - 1 - i;
            derived()->_numAllocationsInArena[i] = 0;
        }
        _freeListHead = derived()->numArenas();
        // Activate the first arena. Al least one arena must be active at all times.
        reserveNextArena();
    }

    void* _data;                // Pointer to the beginning of the allocated section within the active arena.

    SizeType _bytesLeft;        // Number of free bytes remaining in the active arena, including alignment.
    SizeType _activeArenaId;    // Id of the active arena;
    SizeType _freeListHead;     // Indices smaller than this contain free arenas.

    // Returns true and updates the active arena member variables if a free arena is available.
    // Otherwise, returns false and doesn't change anything.
    // Note: the mutex must be locked before this function is called in synchronized mode.
    bool reserveNextArena()
    {
        if (_freeListHead == 0)
            return false;
        --_freeListHead;
        _bytesLeft = derived()->arenaSize();
        _activeArenaId = derived()->_freeList[_freeListHead];
        // Initially, _data points to one past the last byte of the arena.
        _data = derived()->_arenaData.data() + derived()->arenaSize() * (_activeArenaId + 1);
        return true;
    }

    // Re-initialize the active arena in an optimized way without
    // release/reserve cycle.
    // Note: mutex must be locked before this function is called in synchronized mode.
    void resetActiveArena()
    {
        MULTIARENA_ASSERT(allocationsInArena(_activeArenaId) == 0);
        _bytesLeft = derived()->arenaSize();
        _data = derived()->_arenaData.data() + derived()->arenaSize() * (_activeArenaId + 1);
        derived()->_numAllocationsInArena[_activeArenaId] = 0;
    }

    // Recycle the given arena by moving it to the freelist.
    // Returns true if all arenas become empty.
    // Note: mutex must be locked before this function is called in synchronized mode.
    void releaseArena(SizeType arenaId)
    {
        MULTIARENA_ASSERT(allocationsInArena(arenaId) == 0);
        MULTIARENA_ASSERT(_freeListHead < derived()->numArenas());
        derived()->_freeList[_freeListHead++] = arenaId;
        derived()->_numAllocationsInArena[arenaId] = 0;
    }

private:
    auto derived() const
    {
        return static_cast<const Derived*>(this);
    }

    auto derived()
    {
        return static_cast<Derived*>(this);
    }

    // Returns nullptr if all arenas are out of memory and the allocation can't hence be made.
    void* do_allocate_details(std::size_t bytes, std::size_t alignment)
    {
        uintptr_t ptrAsInteger = reinterpret_cast<uintptr_t>(_data);
        ptrAsInteger -= bytes;  // Tentative result excluding alignment.
        SizeType alignmentOffset = ptrAsInteger & (alignment - 1); // Assume alignment is a power of 2
        SizeType numBytesNeeded = SizeType(bytes) + alignmentOffset; // Final amount of bytes needed
        if (numBytesNeeded > _bytesLeft) {
            // Not enough space in this arena. Tap the next one.
            if (bytes <= derived()->arenaSize() && reserveNextArena())
                // There is enough space in the next arena so the recursion will occur only once.
                return do_allocate_details(bytes, alignment);
            else  // Out of luck. bad_alloc will be thrown if exceptions are enabled.
                return nullptr;
        }
        ptrAsInteger -= alignmentOffset;
        _data = reinterpret_cast<void*>(ptrAsInteger);
        _bytesLeft -= numBytesNeeded;

        // Update the number of allocations made in the current arena.
        ++(derived()->_numAllocationsInArena[_activeArenaId]);
        return _data;
    }

protected:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (bytes == 0)
            return nullptr;
        void* result = do_allocate_details(bytes, alignment);
        if constexpr (exceptionsEnabled) {
            if (result == nullptr) { // Find out the reason for failure.
                if (bytes > derived()->arenaSize()) // Too large block requested
                    throw AllocateTooLargeBlock(bytes, derived()->arenaSize());
                else
                    throw OutOfFreeArenas(derived()->numArenas());
            }
        }
        return result;
    }

    // Virtual allocate function.
    // Note that bytes and alignment are used only when an exception is thrown
    // so they are actually only debug helpers and may be left out.
    void do_deallocate(void* p,
                       std::size_t bytes = 0,
                       std::size_t alignment = alignof(std::max_align_t)) override
    {
        // Calculate the id of the arena where the address has come from.
        uintptr_t ptrAsInteger = reinterpret_cast<uintptr_t>(p);
        uintptr_t dataAsInteger = reinterpret_cast<uintptr_t>(derived()->_arenaData.data());
        SizeType arenaId = SizeType(ptrAsInteger - dataAsInteger) / derived()->arenaSize();
        if constexpr (exceptionsEnabled) {
            if (arenaId >= derived()->numArenas()) // There is either double-free or memory corruption
                throw ArenaMemoryResourceCorruption(p, bytes, alignment);
        }
        // Did the arena become vacant? If so, either reuse or release.
        SizeType numAllocs = --(derived()->_numAllocationsInArena[arenaId]);
        if (numAllocs == 0) {
            if (arenaId == _activeArenaId)
                resetActiveArena(); // The currently active arena became empty so reuse it.
            else
                releaseArena(arenaId); // Release the arena back to the free list.
        }
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return (this == &other);
    }

    // Number of currently active allocation in the given arena.
    SizeType allocationsInArena(SizeType arenaId) const
    {
        return derived()->_numAllocationsInArena[arenaId];
    }
}; // UnsynchronizedArenaResourceBase


// Unsynchronized (i.e. non-thread-safe) memory resource where the data
// is allocated from the stack.
template <SizeType NUM_ARENAS, SizeType ARENA_SIZE>
class UnsynchronizedArenaResource :
    public UnsynchronizedArenaResourceBase<UnsynchronizedArenaResource<NUM_ARENAS, ARENA_SIZE>>
{
public:
    using Base = UnsynchronizedArenaResourceBase<UnsynchronizedArenaResource<NUM_ARENAS, ARENA_SIZE>>;
    explicit UnsynchronizedArenaResource(SizeType = 0, SizeType = 0, std::pmr::memory_resource* = nullptr) : Base(NUM_ARENAS, ARENA_SIZE)
    {
        static_assert(NUM_ARENAS > 0, "There must be at least one arena.");
        static_assert(ARENA_SIZE % alignof(std::max_align_t) == 0," Arena size must be divisible by max alignment.");
        this->initializeArenas();
    }

    constexpr SizeType numArenas() { return NUM_ARENAS; }
    constexpr SizeType arenaSize() { return ARENA_SIZE; }

    friend class UnsynchronizedArenaResourceBase<UnsynchronizedArenaResource<NUM_ARENAS, ARENA_SIZE>>;
protected:
    // Number of allocations in each arena since the arena was activated.
    std::array<SizeType, NUM_ARENAS> _numAllocationsInArena;
    // List of free arenas.
    std::array<SizeType, NUM_ARENAS> _freeList;
    alignas(hardware_constructive_interference_size) // Align to a cache line.
        std::array<std::byte, ARENA_SIZE * NUM_ARENAS> _arenaData;
};  // UnsynchronizedArenaResource in stack

// Unsynchronized (i.e. non-thread-safe) memory resource where the data
// is allocated from the given memory resource (system heap by default.)
template <>
class UnsynchronizedArenaResource<0, 0> :
    public UnsynchronizedArenaResourceBase<UnsynchronizedArenaResource<0, 0>>
{
public:
    using Base = UnsynchronizedArenaResourceBase<UnsynchronizedArenaResource<0, 0>>;
    explicit UnsynchronizedArenaResource(SizeType numArenas, SizeType arenaSize, std::pmr::memory_resource* mr = nullptr)
        : Base(numArenas, arenaSize), _numArenas(numArenas), _arenaSize(arenaSize)
    {
        assert(numArenas > 0);
        assert(arenaSize % alignof(std::max_align_t) == 0);
        _memory_resource = mr ? mr : std::pmr::new_delete_resource();

        // Allocate arenas using the given memory resource.
        _numAllocationsInArena = std::pmr::vector<SizeType>(numArenas, _memory_resource);
        _freeList = std::pmr::vector<SizeType>(numArenas, _memory_resource);
        _arenaData = std::pmr::vector<std::byte>(numArenas * arenaSize, std::byte{}, _memory_resource);
        this->initializeArenas();
    }

    SizeType numArenas() const { return _numArenas; }
    SizeType arenaSize() const { return _arenaSize; }

    friend class UnsynchronizedArenaResourceBase<UnsynchronizedArenaResource<0, 0>>;
protected:
    // Memory resource from which the vectors below will be allocated.
    std::pmr::memory_resource* _memory_resource;
    // Number of allocations in each arena since the arena was activated.
    std::pmr::vector<SizeType> _numAllocationsInArena;
    // List of free arenas.
    std::pmr::vector<SizeType> _freeList;
    std::pmr::vector<std::byte> _arenaData;

    SizeType _numArenas;  // Number of arenas.
    SizeType _arenaSize;  // Size of each arena in bytes.
};  // UnsynchronizedArenaResource in heap


// Forward declarations of memory resource classes.
template <SizeType NUM_ARENAS = 0, SizeType ARENA_SIZE = 0>
class SynchronizedArenaResource;

// Base class for all variants of polymorphic memory resources.
template <class Derived>
class SynchronizedArenaResourceBase : public std::pmr::memory_resource
{
public:
    SynchronizedArenaResourceBase(SizeType numArenas, SizeType arenaSize)
    {}

    // Total number of allocations combined in all arenas.
    std::size_t numberOfAllocations()
    {
        const std::lock_guard<std::mutex> lock(_mtx);
        std::size_t result = 0;
        for (SizeType i = 0; i < derived()->numArenas(); ++i)
            result += allocationsInArena(i);
        return result;
    }

    // Number of non-empty arenas.
    SizeType numberOfBusyArenas()
    {
        const std::lock_guard<std::mutex> lock(_mtx);
        auto result = derived()->numArenas() - _freeListHead;
        // The active arena is counted as a busy even if there
        // are no allocations yet.
        // If the active arena is the only one that possibly have allocations,
        // we must check if the arena is actually empty.
        if (result == 1 && allocationsInArena(_activeArenaId) == 0)
            result = 0;
        return result;
    }

protected:
    void initializeArenas()
    {
        // Initialize arena free list
        for (SizeType i = 0; i < derived()->numArenas(); ++i) {
            derived()->_freeList[i] = derived()->numArenas() - 1 - i;
            derived()->_numAllocationsInArena[i] = 0;
            derived()->_numDeallocationsInArena[i] = 0;
        }
        _freeListHead = derived()->numArenas();
        // Activate the first arena. Al least one arena must be active at all times.
        reserveNextArena();
    }

    uintptr_t _data;    // Pointer to the next free address within the active arena.
    uintptr_t _bytesReserved; // Number of bytes reserved in the active arena

    SizeType _activeArenaId;    // Id of the active arena;
    SizeType _freeListHead;     // Indices smaller than this contain free arenas.
    std::mutex _mtx;

    // Returns true and updates the active arena member variables if a free arena is available.
    // Otherwise, returns false and doesn't change anything.
    // Note: the mutex must be locked before this function is called.
    bool reserveNextArena()
    {
        if (_freeListHead == 0)
            return false;
        --_freeListHead;
        _activeArenaId = derived()->_freeList[_freeListHead];
        // _data points to the first byte of the arena.
        _data = reinterpret_cast<uintptr_t>(derived()->_arenaData.data()) + _activeArenaId * derived()->arenaSize();
        _bytesReserved = 0;
        return true;
    }

    // Re-initialize the active arena in an optimized way without
    // release/reserve cycle.
    // Note: mutex must be locked before this function is called.
    void resetActiveArena()
    {
        MULTIARENA_ASSERT(allocationsInArena(_activeArenaId) == 0);
        _data = reinterpret_cast<uintptr_t>(derived()->_arenaData.data()) + _activeArenaId * derived()->arenaSize();
        _bytesReserved = 0;
        derived()->_numAllocationsInArena[_activeArenaId] =
            derived()->_numDeallocationsInArena[_activeArenaId] = 0;
    }

    // Recycle the given arena by moving it to the freelist.
    // Returns true if all arenas become empty.
    // Note: mutex must be locked before this function is called in synchronized mode.
    void releaseArena(SizeType arenaId)
    {
        MULTIARENA_ASSERT(allocationsInArena(arenaId) == 0);
        MULTIARENA_ASSERT(arenaId != _activeArenaId);
        MULTIARENA_ASSERT(_freeListHead < derived()->numArenas());
        derived()->_freeList[_freeListHead++] = arenaId;
        derived()->_numAllocationsInArena[arenaId] =
            derived()->_numDeallocationsInArena[arenaId] = 0;
    }

private:
    auto derived() const
    {
        return static_cast<const Derived*>(this);
    }

    auto derived()
    {
        return static_cast<Derived*>(this);
    }

    // Returns nullptr if all arenas are out of memory and the allocation can't hence be made.
    // Assume that alignment is a power of 2.
    void* do_allocate_details(std::size_t bytes) noexcept
    {
        // Is there still space in the currently active arena?
        auto numReservedBytes = _bytesReserved + bytes;
        if (numReservedBytes > derived()->arenaSize()) { // Tap a new arena.
            if (reserveNextArena())
                return do_allocate_details(bytes);
            return nullptr; // We are out of arenas
        }
        void* result = reinterpret_cast<void*>(_data);
        _bytesReserved = numReservedBytes;
        _data += bytes;
        // Update the number of allocations made in the current arena.
        derived()->_numAllocationsInArena[_activeArenaId].fetch_add(1, std::memory_order_relaxed);
        return result;
    }

protected:
    // Returns pointer to a block of data whose size it at least bytes
    // and which is aligned to alignof(max_align_t).
    // So the alignment argument is ignored.
    void* do_allocate(std::size_t bytes, std::size_t) override
    {
        if (bytes == 0)
            return nullptr;
        // Assume that _bytesReserved is divisible by alignof(max_align_t) (== usually 16)
        // and the arena is split into bins of size binSize.
        constexpr std::size_t binSize = alignof(max_align_t);

        // How many bins are needed for data?
        std::size_t numBinsForData = (bytes + binSize - 1) / binSize;
        // Calculate how many bytes will be reserved from the active arena, including alignment to binSize.
        uintptr_t numBytesNeeded = numBinsForData * binSize;
        if (numBytesNeeded > derived()->arenaSize()) // Too large request
            return nullptr;

        void* result;
        _mtx.lock();
        result = do_allocate_details(numBytesNeeded);
        _mtx.unlock();
        if constexpr (exceptionsEnabled) {
            if (result == nullptr) { // Find out the reason for failure.
                if (numBytesNeeded > derived()->arenaSize()) // Too large block requested
                    throw AllocateTooLargeBlock(bytes, derived()->arenaSize());
                else
                    throw OutOfFreeArenas(derived()->numArenas());
            }
        }
        return result;
    }

    // Virtual allocate function.
    // Note that bytes and alignment are used only when an exception is thrown
    // so they are actually only debug helpers and may be left out.
    void do_deallocate(void* p,
                       std::size_t bytes = 0,
                       std::size_t alignment = alignof(std::max_align_t)) override
    {
        // Calculate the id of the arena where the address has come from.
        uintptr_t ptrAsInteger = reinterpret_cast<uintptr_t>(p);
        uintptr_t dataAsInteger = reinterpret_cast<uintptr_t>(derived()->_arenaData.data());
        SizeType arenaId = SizeType(ptrAsInteger - dataAsInteger) / derived()->arenaSize();
        if constexpr (exceptionsEnabled) {
            if (arenaId >= derived()->numArenas()) // There is either double-free or memory corruption
                throw ArenaMemoryResourceCorruption(p, bytes, alignment);
        }
        // Did the arena become vacant? If so, either reuse or release.
        SizeType numDeallocs = derived()->_numDeallocationsInArena[arenaId].fetch_add(1, std::memory_order_relaxed) + 1;
        SizeType numAllocs = derived()->_numAllocationsInArena[arenaId];
        if (numAllocs == numDeallocs) {
            // Lock and double check.
            const std::lock_guard<std::mutex> lock(_mtx);
            MULTIARENA_ASSERT(derived()->_numAllocationsInArena[arenaId] >=
                              derived()->_numDeallocationsInArena[arenaId]);
            bool arenaIsVacant = (numAllocs == derived()->_numAllocationsInArena[arenaId]) &&
                                 (numAllocs == derived()->_numDeallocationsInArena[arenaId]);

            if (arenaIsVacant) {
                if (arenaId == _activeArenaId)
                    resetActiveArena(); // The currently active arena became empty so reuse it.
                else
                    releaseArena(arenaId); // Release the arena back to the free list.
            }
        } // Release the lock
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return (this == &other);
    }

    // Number of currently active allocation in the given arena.
    SizeType allocationsInArena(SizeType arenaId) const
    {
        MULTIARENA_ASSERT(derived()->_numAllocationsInArena[arenaId] >= derived()->_numDeallocationsInArena[arenaId]);
        return derived()->_numAllocationsInArena[arenaId] - derived()->_numDeallocationsInArena[arenaId];
    }
}; // SynchronizedArenaResourceBase


// Synchronized (i.e. thread-safe) memory resource where the data
// is allocated from the  stack.
template <SizeType NUM_ARENAS, SizeType ARENA_SIZE>
class SynchronizedArenaResource :
    public SynchronizedArenaResourceBase<SynchronizedArenaResource<NUM_ARENAS, ARENA_SIZE>>
{
public:
    using Base = SynchronizedArenaResourceBase<SynchronizedArenaResource<NUM_ARENAS, ARENA_SIZE>>;
    explicit SynchronizedArenaResource(SizeType = 0, SizeType = 0)
        noexcept(!exceptionsEnabled) : Base(NUM_ARENAS, ARENA_SIZE)
    {
        static_assert(NUM_ARENAS > 0, "There must be at least one arena.");
        static_assert(ARENA_SIZE % alignof(std::max_align_t) == 0," Arena size must be divisible by max alignment.");
        this->initializeArenas();
    }

    constexpr SizeType numArenas() { return NUM_ARENAS; }
    constexpr SizeType arenaSize() { return ARENA_SIZE; }

    friend class SynchronizedArenaResourceBase<SynchronizedArenaResource<NUM_ARENAS, ARENA_SIZE>>;
protected:

    // Number of allocations done in each arena since the arena was activated.
    std::array<std::atomic<SizeType>, NUM_ARENAS> _numAllocationsInArena;
    // Number of de-allocations done in each arena since the arena was activated.
    std::array<std::atomic<SizeType>, NUM_ARENAS> _numDeallocationsInArena;
    // List of free arenas.
    std::array<SizeType, NUM_ARENAS> _freeList;
    alignas(hardware_constructive_interference_size) // Align to a cache line.
        std::array<std::byte, NUM_ARENAS * ARENA_SIZE> _arenaData;
};  // SynchronizedArenaResource in stack


// Synchronized (i.e. thread-safe) memory resource where the data
// is allocated from the given memory resource (system heap by default.)
template <>
class SynchronizedArenaResource<0, 0> :
    public SynchronizedArenaResourceBase<SynchronizedArenaResource<0, 0>>
{
public:
    using Base = SynchronizedArenaResourceBase<SynchronizedArenaResource<0, 0>>;
    explicit SynchronizedArenaResource(SizeType numArenas, SizeType arenaSize, std::pmr::memory_resource* mr = nullptr)
        : Base(numArenas, arenaSize), _numArenas(numArenas), _arenaSize(arenaSize)
    {
        assert(numArenas > 0);
        assert(arenaSize % alignof(std::max_align_t) == 0);
        _memory_resource = mr ? mr : std::pmr::new_delete_resource();

        // Allocate arenas using the given memory resource.
        _arenaData = std::pmr::vector<std::byte>(numArenas * arenaSize, std::byte{}, _memory_resource);

        // operator= does not work if the value type is atomic so use swap.
        {
            std::pmr::vector<std::atomic<SizeType>> vec(numArenas, _memory_resource);
            std::swap(vec, _numAllocationsInArena);
        }
        {
            std::pmr::vector<std::atomic<SizeType>> vec(numArenas, _memory_resource);
            std::swap(vec, _numDeallocationsInArena);
        }

        _freeList = std::pmr::vector<SizeType>(numArenas, _memory_resource);
        this->initializeArenas();
    }

    SizeType numArenas() const { return _numArenas; }
    SizeType arenaSize() const { return _arenaSize; }

    friend class SynchronizedArenaResourceBase<SynchronizedArenaResource<0, 0>>;
protected:

    // Memory resource from which the vectors below will be allocated.
    std::pmr::memory_resource* _memory_resource;
    // Number of allocations done in each arena since the arena was activated.
    std::pmr::vector<std::atomic<SizeType>> _numAllocationsInArena;
    // Number of de-allocations done in each arena since the arena was activated.
    std::pmr::vector<std::atomic<SizeType>> _numDeallocationsInArena;
    // List of free arenas.
    std::pmr::vector<SizeType> _freeList;
    std::pmr::vector<std::byte> _arenaData;
    SizeType _numArenas;  // Number of arenas.
    SizeType _arenaSize;  // Size of each arena in bytes.
};  // SynchronizedArenaResource in stack

// Synchronized (i.e. thread-safe) memory resource which otherwise is
// like SynchronizedArenaResource above except that it keep track of every
// allocation for later analysis. It can be used for tuning the number of
// arenas and the size of each arena by looking into the allocation statistics.
class StatisticsArenaResource : public UnsynchronizedArenaResource<>
{
public:
    using Base = UnsynchronizedArenaResource<>;
    using MapType = std::pmr::map<void*, SizeType>;
    using HistogramType = std::pmr::map<SizeType, SizeType>;

    explicit StatisticsArenaResource(SizeType numArenas, SizeType arenaSize, std::pmr::memory_resource* mr = nullptr)
        : UnsynchronizedArenaResource(numArenas, arenaSize, mr)
    {
        if constexpr (exceptionsEnabled) {
            if (numArenas <= 0)
                throw std::runtime_error("Number of arenas must be positive.");
            if (arenaSize % alignof(std::max_align_t) != 0)
                throw std::runtime_error("Arena size must be divisible by max alignment.");
        }
        _memory_resource = mr ? mr : std::pmr::new_delete_resource();

        _map = MapType(_memory_resource);
    }

    // Returns a const pointer to the map which maps allocated addresses to
    // the sizes of allocated blocks in bytes.
    const MapType* addressToBytesMap() const
    {
        return &_map;
    }

    // Returns the sum of all active allocations in bytes
    std::size_t bytesAllocated() const
    {
        std::size_t sumBytes =
            std::accumulate(_map.cbegin(), _map.cend(), std::size_t(0),
                            [](std::size_t init, auto ptrBytesPair)
                            { return init + ptrBytesPair.second; });
        return sumBytes;
    }

    // Returns a map which maps the sizes of currently active allocations to
    // the number of such blocks, aka histogram of alloction sizes in bytes.
    HistogramType histogram() const
    {
        HistogramType hist(_memory_resource);
        for (auto it = _map.cbegin(); it != _map.cend(); ++it)
            hist[it->second] += 1;
        return hist;
    }

    // Returns a block size such that the cumulative sum of all active allocations
    // up to the returned block size is smaller or equal to (pc*100) percent of
    // the total sum of all allocations.
    // So if p = 0.5, returns the median of the active block sizes in bytes.
    // If p = 1, return the maximum block size.
    std::size_t percentile(double pc) const
    {
        pc = std::clamp(pc, 0.0, 1.0);
        if (pc == 0)
            return 0;
        // Calculate histogram
        auto hist = this->histogram();
        // Calculate the number of all allocations
        std::size_t sum =
            std::accumulate(hist.cbegin(), hist.cend(), std::size_t(0),
                            [](std::size_t init, auto sizeFreqPair)
                            { return init + sizeFreqPair.second; });

        if (sum == 0)
            return 0;
        // Find the smallest block size such that the accumulated frequency of all block sizes
        // is less or equal to pc * sum.
        std::size_t upperLimit = pc * sum;
        std::size_t accumulatedSum = 0;
        auto it = hist.cbegin();
        while (accumulatedSum < upperLimit) {
            accumulatedSum += it->second;
            ++it;
        }
        return std::prev(it)->first;
    }

    // returns  mean of the allocated block size
    double mean() const
    {
        auto hist = this->histogram();
        // Calculate the number of all allocations
        std::size_t numAllocs =
            std::accumulate(hist.cbegin(), hist.cend(), std::size_t(0),
                [](std::size_t init, auto sizeFreqPair)
                { return init + sizeFreqPair.second; });

        if (numAllocs == 0)
            return 0;
        // Calculate the mean
        double dMean = std::accumulate(hist.cbegin(), hist.cend(), double(0),
                            [invAllocs = 1.0 / numAllocs](std::size_t init, auto sizeFreqPair)
                            { return init + sizeFreqPair.first * (sizeFreqPair.second * invAllocs); });
        return dMean;
    }

    // returns  standard deviation of the allocated block size
    double stdDev() const
    {
        auto hist = this->histogram();
        // Calculate the number of all allocations
        std::size_t numAllocs =
            std::accumulate(hist.cbegin(), hist.cend(), std::size_t(0),
                [](std::size_t init, auto sizeFreqPair)
                { return init + sizeFreqPair.second; });

        if (numAllocs == 0)
            return 0;
        // Calculate the mean
        double dMean = std::accumulate(hist.cbegin(), hist.cend(), double(0),
                            [invAllocs = 1.0 / numAllocs](std::size_t init, auto sizeFreqPair)
                            { return init + sizeFreqPair.first * (sizeFreqPair.second * invAllocs); });

        // Calculate variance
        double dVar =
            std::accumulate(hist.cbegin(), hist.cend(), double(0),
                [invAllocs = 1.0 / numAllocs, mean = dMean](std::size_t init, auto sizeFreqPair)
                {
                    double dDiff = sizeFreqPair.first - mean;  // block size - mean
                    return init + dDiff * dDiff * (sizeFreqPair.second * invAllocs);
                });
        return std::sqrt(dVar);
    }

    // All-time high number of arenas in use.
    std::size_t maxBusyArenas = 0;
    // All-time high number of allocations
    std::size_t maxNumberOfAllocations = 0;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        const std::lock_guard<std::mutex> lock(_mtx);
        void* p = Base::do_allocate(bytes, alignment);
        _map[p] = bytes;
        maxBusyArenas = std::max(maxBusyArenas, std::size_t(this->numberOfBusyArenas()));
        maxNumberOfAllocations = std::max(maxNumberOfAllocations, _map.size());
        return p;
    }

    void do_deallocate(void* p,
                       std::size_t bytes = 0,
                       std::size_t alignment = alignof(std::max_align_t)) override
    {
        const std::lock_guard<std::mutex> lock(_mtx);
        if constexpr (exceptionsEnabled) {
            if (_map.erase(p) == 0)
                throw std::runtime_error("Attempt to deallocate from an address which does not hold allocated data.");
        }
        return Base::do_deallocate(p, bytes, alignment);
    }

    std::mutex _mtx;
    // Memory resource from which the arenas and the statistics map will be allocated.
    std::pmr::memory_resource* _memory_resource;
    // Map of currently allocated addresses to the number of bytes allocated to those addresses.
    MapType _map;
};

// Deleter for a unique_ptr allocated with a polymorphic allocator.
template <class T>
class PolymorphicDeleter
{
public:
  explicit PolymorphicDeleter(std::pmr::memory_resource *mr) : _mr(mr)
  { }

  void operator()(T* p) const
  {
      p->~T();
      _mr->deallocate(p, sizeof(T), alignof(T));
  }
private:
  std::pmr::memory_resource* _mr = nullptr;
};

// Type of polymorphic unique pointer
template <class T>
using PolymorphicUniquePointer = std::unique_ptr<T, PolymorphicDeleter<T>>;

// Makes a unique_ptr using the given polymorphic_allocator.
template <class T, class... Args>
PolymorphicUniquePointer<T> makePolymorphicUnique(std::pmr::polymorphic_allocator<T>& alloc, Args&&... args)
{
  T* pT = alloc.allocate(1); // Allocate one object of type T
  alloc.construct(pT, std::forward<Args>(args)...);
  return std::unique_ptr<T, PolymorphicDeleter<T>>(pT, PolymorphicDeleter<T>(alloc.resource()));
}

// Makes a unique_ptr using a plain polymorphic memory resource.
template <class T, class... Args>
PolymorphicUniquePointer<T> makePolymorphicUnique(std::pmr::memory_resource *mr, Args&&... args)
{
  std::pmr::polymorphic_allocator<T> alloc(mr);
  T* pT = alloc.allocate(1); // Allocate one object of type T
  alloc.construct(pT, std::forward<Args>(args)...);
  return std::unique_ptr<T, PolymorphicDeleter<T>>(pT, PolymorphicDeleter<T>(mr));
}

} // namespace MultiArena

#endif // MULTIARENA_H
