//
//  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
//  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
//  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
//
//  © 2025 Lorenz Bucher - all rights reserved
//  https://github.com/Sidelobe/MemorySentinel

#include <catch2/catch.hpp>

#include "MemorySentinel.hpp"

#include <vector>

#if defined(__APPLE__)
    #include <Availability.h>
#endif

// memalign is a GLIBC extension declared in <malloc.h>
#if defined(__GLIBC__)
    #include <malloc.h>
    #define SLB_HAS_MEMALIGN 1
#endif

#if (defined(__clang__) || defined(__GNUC__)) && !defined(__APPLE__)
    #define SLB_HAS_ALIGNED_ALLOC 1
// aligned_alloc() is macOS 10.15+ / iOS 13+
#elif (defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED >= __MAC_10_15) \
   || (defined(__IPHONE_OS_VERSION_MIN_REQUIRED) && __IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_13_0)
    #define SLB_HAS_ALIGNED_ALLOC 1
#endif

// When exceptions are disabled (e.g. in coverage build), we redefine catch2's REQUIRE_THROWS, so we can compile.
// Any REQUIRE_THROWS statements in tests will dissappear / do nothing
#ifdef SLB_EXCEPTIONS_DISABLED
    #define REQUIRE_THROWS_CATCH2 REQUIRE_THROWS
    #undef REQUIRE_THROWS
    #define REQUIRE_THROWS(...)
    #define REQUIRE_THROWS_AS_CATCH2 REQUIRE_THROWS_AS
    #undef REQUIRE_THROWS_AS
    #define REQUIRE_THROWS_AS(...)
#endif

static decltype(auto) allocWithNew()        { return new std::vector<float>(32); }
static decltype(auto) allocWithNewArray()   { return new float[32]; }
static decltype(auto) allocWithMalloc()     { return std::malloc(32*sizeof(float)); }
static decltype(auto) allocWithCalloc()     { return std::calloc(32, sizeof(float)); }
static decltype(auto) allocWithRealloc()    { return std::realloc(nullptr, 32*sizeof(float)); }
static decltype(auto) allocWithNewNoExcept()      noexcept { return operator new(sizeof(std::vector<float>(32)), std::nothrow); }
static decltype(auto) allocWithNewArrayNoExcept() noexcept { return operator new[](sizeof(float[32]), std::nothrow); }

#if defined(__clang__) || defined(__GNUC__)
static decltype(auto) allocWithPosixMemalign()
{
    void* p = nullptr;
    if (posix_memalign(&p, 32, 32*sizeof(float)) != 0) { p = nullptr; }
    return p;
}
#   ifdef SLB_HAS_ALIGNED_ALLOC
    static decltype(auto) allocWithAlignedAlloc() { return aligned_alloc(32, 32*sizeof(float)); }
#   endif
#   ifdef SLB_HAS_MEMALIGN
    static decltype(auto) allocWithMemalign()     { return memalign(32, 32*sizeof(float)); }
#   endif

#endif

// Sink for allocations whose result is not used otherwise - this prevents the compiler from optimizing away the allocation
static volatile void* allocSink = nullptr;

// Turn off clang optimizations for these functions
#if defined(__clang__)
#pragma clang optimize off
#endif

template<typename T>
static void testAllocation(MemorySentinel& sentinel, T& allocFunc)
{
    sentinel.clearTransgressions();
    sentinel.setArmed(true);
    
    volatile float* a = nullptr; // dummy to avoid optimization
    
    // NOTE: Catch's REQUIRE_THROWS may allocate memory under certain circumstances, therefore we avoid it!
    bool hasThrown = false;
    try {
        a = (float*) allocFunc();
    } catch (const std::bad_alloc& e) {
        hasThrown = true;
    }
    sentinel.setArmed(false);
    
    REQUIRE(hasThrown);
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
    // freeing not necessary, since allocation was intercepted by exception
}

template<typename AllocFunc, typename FreeFunc>
static void testDetection(MemorySentinel& sentinel, AllocFunc& allocFunc, FreeFunc freeFunc)
{
    sentinel.clearTransgressions();
    sentinel.setArmed(true);

    volatile auto m = allocFunc();
    const bool wasArmed = sentinel.isArmed();
    const bool allocationDetected = sentinel.getAndClearTransgressionsOccured();

    freeFunc(m); // always noexcept
    const bool deallocationDetected = sentinel.getAndClearTransgressionsOccured();

    sentinel.setArmed(false);

    // NOTE: Catch's macros may allocate memory, therefore we only use them after disarming
    REQUIRE(wasArmed);
    REQUIRE(m != nullptr);
    REQUIRE(allocationDetected);
    REQUIRE(deallocationDetected);
}

template<typename T, typename U>
static void testFreeing(MemorySentinel& sentinel, T& allocFunc, U& freeFunc)
{
    sentinel.clearTransgressions();
    // allocate with unarmed sentinel
    sentinel.setArmed(false);
    volatile auto m = allocFunc();
    sentinel.setArmed(true);

    freeFunc(m); // always noexcept

    // freeing took place, no exception was thrown
    sentinel.setArmed(false);
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
}

template<typename T>
static void testDelete(MemorySentinel& sentinel, T& allocFunc)
{
    sentinel.clearTransgressions();
    // allocate with unarmed sentinel
    sentinel.setArmed(false);
    auto m = allocFunc();
    sentinel.setArmed(true);
    
    operator delete(m);  // always noexcept
    
    // deletion took place, no exception was thrown
    sentinel.setArmed(false);
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
}

template<typename T>
static void testDeleteArray(MemorySentinel& sentinel, T&& allocFunc)
{
    sentinel.clearTransgressions();
    // allocate with unarmed sentinel
    sentinel.setArmed(false);
    auto m = allocFunc();
    sentinel.setArmed(true);
    
    operator delete[](m);  // always noexcept
    
    // deletion took place, no exception was thrown
    sentinel.setArmed(false);
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
}

#if defined(__clang__)
#pragma clang optimize on
#endif

TEST_CASE("MemorySentinel Tests: zero allocation quota (default)")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    sentinel.clearTransgressions();
    
    SECTION("SILENT") {
        MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);
        
        // while not armed, nothing is detected
        sentinel.setArmed(false);
        REQUIRE_FALSE(sentinel.isArmed());
        std::vector<float>* heapObject = allocWithNew();
        REQUIRE(heapObject != nullptr);
        REQUIRE_FALSE(sentinel.hasTransgressionOccured());
        sentinel.clearTransgressions();
        delete heapObject; // clean up
        
        auto deleteObject = [](auto* p) { delete p; };
        auto deleteArray  = [](auto* p) { delete[] p; };
        auto freeMemory   = [](auto* p) { std::free(p); };
        
        testDetection(sentinel, allocWithNew,      deleteObject);
        testDetection(sentinel, allocWithNewArray, deleteArray);
        
    // NOTE: the C allocators are only hijacked on GCC / Clang
    #if defined(__clang__) || defined(__GNUC__)
        testDetection(sentinel, allocWithMalloc,        freeMemory);
        testDetection(sentinel, allocWithCalloc,        freeMemory);
        testDetection(sentinel, allocWithRealloc,       freeMemory);
        testDetection(sentinel, allocWithPosixMemalign, freeMemory);
        
        #if SLB_HAS_ALIGNED_ALLOC
        testDetection(sentinel, allocWithAlignedAlloc,  freeMemory);
        #endif
        #if SLB_HAS_MEMALIGN
        testDetection(sentinel, allocWithMemalign,      freeMemory);
        #endif
    #endif
    }
    
    SECTION("LOG") {
        MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::LOG);
        std::vector<float>* heapObject = allocWithNew();
        sentinel.setArmed(true);
        REQUIRE(sentinel.isArmed());
        heapObject = allocWithNew();
        REQUIRE(heapObject != nullptr);
        REQUIRE(sentinel.getAndClearTransgressionsOccured());
        delete heapObject; // clean up
        REQUIRE(sentinel.getAndClearTransgressionsOccured());
        sentinel.setArmed(false);
    }
    
#ifndef SLB_EXCEPTIONS_DISABLED
    SECTION("THROW_EXCEPTION - new/delete") {
        MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
        
        testAllocation(sentinel, allocWithNew);
        testAllocation(sentinel, allocWithNewArray);
    
        sentinel.setArmed(true);
        void* p1;
        p1 = allocWithNewNoExcept();
        REQUIRE(p1 == nullptr);
        REQUIRE(sentinel.getAndClearTransgressionsOccured());
        // freeing not necessary, since allocation was intercepted by exception
        
        sentinel.setArmed(true);
        void* p2;
        p2 = allocWithNewArrayNoExcept();
        REQUIRE(p2 == nullptr);
        REQUIRE(sentinel.getAndClearTransgressionsOccured());
        // freeing not necessary, since allocation was intercepted by exception
        
        testDelete(sentinel, allocWithNew);
        testDelete(sentinel, allocWithNewNoExcept);
        
        testDeleteArray(sentinel, allocWithNewArray);
        testDeleteArray(sentinel, allocWithNewArrayNoExcept);
        
        sentinel.setArmed(false);
    }
    #if (defined(__clang__) || defined(__GNUC__)) && !defined(__GLIBC__)
        // NOTE: with GLIBC the C allocators are declared noexcept, so a thrown exception may bypass the caller's handlers
        // we thus skip these tests for GLIBC
        
        SECTION("THROW_EXCEPTION - malloc/free") {
            MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
            testAllocation(sentinel, allocWithMalloc);
            testFreeing(sentinel, allocWithMalloc, free);
        }
    
        SECTION("THROW_EXCEPTION - calloc/free") {
            MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
            testAllocation(sentinel, allocWithCalloc);
            testFreeing(sentinel, allocWithCalloc, free);
        }
    
        SECTION("THROW_EXCEPTION - realloc/free") {
            MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
            testAllocation(sentinel, allocWithRealloc);
            testFreeing(sentinel, allocWithRealloc, free);
        }

        SECTION("THROW_EXCEPTION - posix_memalign/free") {
            MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
            testAllocation(sentinel, allocWithPosixMemalign);
            testFreeing(sentinel, allocWithPosixMemalign, free);
        }

    #if SLB_HAS_ALIGNED_ALLOC
        SECTION("THROW_EXCEPTION - aligned_alloc/free") {
            MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
            testAllocation(sentinel, allocWithAlignedAlloc);
            testFreeing(sentinel, allocWithAlignedAlloc, free);
        }
    #endif
    
    #if SLB_HAS_MEMALIGN
        SECTION("THROW_EXCEPTION - memalign/free") {
            MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
            testAllocation(sentinel, allocWithMemalign);
            testFreeing(sentinel, allocWithMemalign, free);
        }
    #endif
    
    #endif
    
#endif // SLB_EXCEPTIONS_DISABLED
    
    // After tests, disarm Sentinel
    sentinel.clearTransgressions();

    
    // MARK: - ScopedMemorySentinel Tests (put into same test case to avoid weird issues on macos/release)

    SECTION("default behaviour") {
        ScopedMemorySentinel sentinel;
        // THIS WILL ASSERT (default behaviour)
        //std::vector<float>* heapObject = allocWithNew();
    }
    
    SECTION("throw on alloc") {
        bool hasThrown = false;
        {
            ScopedMemorySentinel sentinel;
            MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
            
            // NOTE: Catch's REQUIRE_THROWS may allocate memory under certain circumstances, therefore we avoid it!
            try {
                allocSink = allocWithNew();
            } catch (const std::bad_alloc& e) {
                hasThrown = true;
            }
        }
        REQUIRE(hasThrown);
    }

    
    // Set allocation quota (allocWithNew allocates float vector size 32)
    // - std::vector data: 32*sizeof(float)
    // - std::vector overhead: 24 bytes (clang stl implementation)
    int stdVectorOverhead = sizeof(std::vector<float>);
    int bytesAllocatedFor32FloatVector = 32*sizeof(float) + stdVectorOverhead;

#if defined(_MSC_VER) && defined(_DEBUG) &&_ITERATOR_DEBUG_LEVEL > 1 // MSVC Debug results in additional
    bytesAllocatedFor32FloatVector += 16;
#endif

    std::vector<float>* heapObject;
    SECTION("quota fits") {
        {
            // allocation size is just right
            ScopedMemorySentinel sentinel(bytesAllocatedFor32FloatVector);
            heapObject = allocWithNew();
        }
        delete heapObject; // clean up
        
        // this works because we're out of scope of the ScopedMemorySentinel
        heapObject = allocWithNew();
        delete heapObject; // clean up
    }
    
    SECTION("quota does barely not fit") {
        bool hasThrown = false;
        {
            // this will throw a std::bad_alloc (allocating 1 byte too many)
            ScopedMemorySentinel sentinel(bytesAllocatedFor32FloatVector-1);
            
            // NOTE: Catch's REQUIRE_THROWS may allocate memory under certain circumstances, therefore we avoid it!
            try {
                allocSink = allocWithNew();
            } catch (const std::bad_alloc& e) {
                hasThrown = true;
            }
        }
        REQUIRE(hasThrown);
        
        // delete not necessary, since we never allocated
    }
}

TEST_CASE("MemorySentinel Tests: deallocation of nullptr is not a transgression")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setAllocationQuota(0);

    SECTION("SILENT") {
        MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);

        // NOTE: Catch's macros allocate memory, so we must not use them while the sentinel is armed
        sentinel.setArmed(true);

        std::vector<float>* nullObject = nullptr;
        delete nullObject;
        const bool deleteTransgressed = sentinel.getAndClearTransgressionsOccured();

        float* nullArray = nullptr;
        delete[] nullArray;
        const bool deleteArrayTransgressed = sentinel.getAndClearTransgressionsOccured();

        free(nullptr);
        const bool freeTransgressed = sentinel.getAndClearTransgressionsOccured();

        sentinel.setArmed(false);
        
        // use catch macros AFTER unarming the sentinel, since they may allocate memory
        REQUIRE_FALSE(deleteTransgressed);
        REQUIRE_FALSE(deleteArrayTransgressed);
        REQUIRE_FALSE(freeTransgressed);
    }

#ifndef SLB_EXCEPTIONS_DISABLED
    SECTION("THROW_EXCEPTION") {
        MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);

        bool hasThrown = false;
        bool deleteTransgressed = true, deleteArrayTransgressed = true, freeTransgressed = true;

        sentinel.setArmed(true);
        try {
            std::vector<float>* nullObject = nullptr;
            delete nullObject;
            deleteTransgressed = sentinel.getAndClearTransgressionsOccured();

            float* nullArray = nullptr;
            delete[] nullArray;
            deleteArrayTransgressed = sentinel.getAndClearTransgressionsOccured();

            free(nullptr);
            freeTransgressed = sentinel.getAndClearTransgressionsOccured();
        } catch (const std::bad_alloc&) {
            hasThrown = true;
        }
        sentinel.setArmed(false);

        // use catch macros AFTER unarming the sentinel, since they may allocate memory
        REQUIRE_FALSE(hasThrown); // deallocating a nullptr must never throw
        REQUIRE_FALSE(deleteTransgressed);
        REQUIRE_FALSE(deleteArrayTransgressed);
        REQUIRE_FALSE(freeTransgressed);
    }
#endif

    sentinel.setArmed(false);
    sentinel.clearTransgressions();
}
