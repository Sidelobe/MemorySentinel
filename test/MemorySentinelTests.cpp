//
//  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
//  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
//  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
//
//  © 2025 Lorenz Bucher - all rights reserved
//  https://github.com/Sidelobe/MemorySentinel

#include <catch2/catch.hpp>

#include "MemorySentinel.hpp"

#include <atomic>
#include <cerrno>
#include <thread>
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

// Turn off clang optimizations for these functions.
// NOTE: for GCC, we use -fno-allocation-dce in CMakeLists.txt
#if defined(__clang__)
#pragma clang optimize off
#endif

static decltype(auto) allocWithNew()        { return new std::vector<float>(32); }
static decltype(auto) allocWithNewArray()   { return new float[32]; }
static decltype(auto) allocWithNewFloat()   { return new float(1.f); } // trivial type: safe to sized-delete
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
static void testFreeing(MemorySentinel& sentinel, T& allocFunc, U freeFunc)
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
        
        // sized deallocation overloads
        testDetection(sentinel, allocWithNewFloat, [](auto* p) { operator delete(p, sizeof(*p)); });
        testDetection(sentinel, allocWithNewArray, [](auto* p) { operator delete[](p, 32 * sizeof(*p)); });
        
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
        
        // sized deallocation overloads
        testFreeing(sentinel, allocWithNewFloat, [](auto* p) { operator delete(p, sizeof(*p)); });
        testFreeing(sentinel, allocWithNewArray, [](auto* p) { operator delete[](p, 32 * sizeof(*p)); });
        
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
    
    #endif
    
#endif // SLB_EXCEPTIONS_DISABLED
    
    // After tests, disarm Sentinel
    sentinel.clearTransgressions();
}

TEST_CASE("ScopedMemorySentinel Tests")
{
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

        operator delete(nullptr, sizeof(float));
        const bool sizedDeleteTransgressed = sentinel.getAndClearTransgressionsOccured();

        operator delete[](nullptr, sizeof(float));
        const bool sizedDeleteArrayTransgressed = sentinel.getAndClearTransgressionsOccured();

        free(nullptr);
        const bool freeTransgressed = sentinel.getAndClearTransgressionsOccured();

        sentinel.setArmed(false);
        
        // use catch macros AFTER unarming the sentinel, since they may allocate memory
        REQUIRE_FALSE(deleteTransgressed);
        REQUIRE_FALSE(deleteArrayTransgressed);
        REQUIRE_FALSE(sizedDeleteTransgressed);
        REQUIRE_FALSE(sizedDeleteArrayTransgressed);
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


TEST_CASE("MemorySentinel Tests: LOG behaviour")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setAllocationQuota(0);
    MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::LOG);
    sentinel.clearTransgressions();

    auto deleteObject = [](auto* p) { delete p; };
    auto deleteArray  = [](auto* p) { delete[] p; };

    testDetection(sentinel, allocWithNew,      deleteObject);
    testDetection(sentinel, allocWithNewArray, deleteArray);

    // sized deallocation overloads
    testDetection(sentinel, allocWithNewFloat, [](auto* p) { operator delete(p, sizeof(*p)); });
    testDetection(sentinel, allocWithNewArray, [](auto* p) { operator delete[](p, 32 * sizeof(*p)); });

#if defined(__clang__) || defined(__GNUC__)
    auto freeMemory = [](auto* p) { std::free(p); };
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

    // NOTE: Catch's macros may allocate memory, therefore we only use them after disarming
    sentinel.setArmed(true);

    // the nothrow variants return nullptr while armed
    void* nothrowObject = allocWithNewNoExcept();
    const bool nothrowObjectDetected = sentinel.getAndClearTransgressionsOccured();
    void* nothrowArray = allocWithNewArrayNoExcept();
    const bool nothrowArrayDetected = sentinel.getAndClearTransgressionsOccured();

    // deallocating a nullptr remains a no-op
    std::vector<float>* nullObject = nullptr;
    delete nullObject;
    const bool nullDeleteDetected = sentinel.getAndClearTransgressionsOccured();
    float* nullArray = nullptr;
    delete[] nullArray;
    const bool nullDeleteArrayDetected = sentinel.getAndClearTransgressionsOccured();
    free(nullptr);
    const bool nullFreeDetected = sentinel.getAndClearTransgressionsOccured();

    sentinel.setArmed(false);

    REQUIRE(nothrowObject == nullptr);
    REQUIRE(nothrowObjectDetected);
    REQUIRE(nothrowArray == nullptr);
    REQUIRE(nothrowArrayDetected);
    REQUIRE_FALSE(nullDeleteDetected);
    REQUIRE_FALSE(nullDeleteArrayDetected);
    REQUIRE_FALSE(nullFreeDetected);
}

TEST_CASE("MemorySentinel Tests: nothrow new returns nullptr while armed")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setAllocationQuota(0);
    MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);
    sentinel.clearTransgressions();

    sentinel.setArmed(true);
    void* p1 = allocWithNewNoExcept();
    const bool objectDetected = sentinel.getAndClearTransgressionsOccured();
    void* p2 = allocWithNewArrayNoExcept();
    const bool arrayDetected = sentinel.getAndClearTransgressionsOccured();
    sentinel.setArmed(false);

    REQUIRE(p1 == nullptr);
    REQUIRE(objectDetected);
    REQUIRE(p2 == nullptr);
    REQUIRE(arrayDetected);
}

TEST_CASE("MemorySentinel Tests: allocation quota")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);
    sentinel.clearTransgressions();

    constexpr int quota = 1024;
    constexpr int arraySize = 32 * static_cast<int>(sizeof(float));

    SECTION("allocations within the quota are permitted and consume it") {
        MemorySentinel::setAllocationQuota(quota);
        sentinel.setArmed(true);
        float* heapArray = allocWithNewArray();
        const int remaining = MemorySentinel::getRemainingAllocationQuota();
        const bool detected = sentinel.getAndClearTransgressionsOccured();
        sentinel.setArmed(false);

        REQUIRE(heapArray != nullptr);
        REQUIRE_FALSE(detected);
        REQUIRE(remaining == quota - arraySize);
        delete[] heapArray; // clean up
    }

    SECTION("deallocations are a transgression, even with quota left") {
        sentinel.setArmed(false);
        float* heapArray = allocWithNewArray();
        float* heapFloat = allocWithNewFloat();

        MemorySentinel::setAllocationQuota(quota);
        sentinel.setArmed(true);
        delete[] heapArray;
        const bool detected = sentinel.getAndClearTransgressionsOccured();
        // the sized overloads pass the actual size, which must not be mistaken for an allocation
        operator delete(heapFloat, sizeof(float));
        const bool sizedDetected = sentinel.getAndClearTransgressionsOccured();
        const int remaining = MemorySentinel::getRemainingAllocationQuota();
        sentinel.setArmed(false);

        REQUIRE(detected);
        REQUIRE(sizedDetected);
        REQUIRE(remaining == quota); // deallocations never consume quota
    }

#if defined(__clang__) || defined(__GNUC__)
    SECTION("calloc consumes the total size, not the size of a single element") {
        MemorySentinel::setAllocationQuota(quota);
        sentinel.setArmed(true);
        void* m = allocWithCalloc(); // calloc(32, sizeof(float))
        const int remaining = MemorySentinel::getRemainingAllocationQuota();
        const bool detected = sentinel.getAndClearTransgressionsOccured();
        sentinel.setArmed(false);

        REQUIRE(m != nullptr);
        REQUIRE_FALSE(detected);
        REQUIRE(remaining == quota - arraySize);
        std::free(m); // clean up
    }
#endif

    MemorySentinel::setAllocationQuota(0);
}

#ifndef SLB_EXCEPTIONS_DISABLED
TEST_CASE("MemorySentinel Tests: detection continues after a thrown transgression")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setAllocationQuota(0);
    MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
    sentinel.clearTransgressions();

    constexpr int numAttempts = 3;
    int numThrows = 0;

    sentinel.setArmed(true);
    for (int i = 0; i < numAttempts; ++i) {
        try {
            allocSink = allocWithNew();
        } catch (const std::bad_alloc&) {
            ++numThrows;
        }
    }
    const bool detected = sentinel.getAndClearTransgressionsOccured();
    sentinel.setArmed(false);

    // the sentinel must remain active after having thrown -- every attempt is intercepted
    REQUIRE(numThrows == numAttempts);
    REQUIRE(detected);
}
#endif

#if defined(__clang__) || defined(__GNUC__)
TEST_CASE("MemorySentinel Tests: a failing allocation is detected as well")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setAllocationQuota(0);
    MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);
    sentinel.clearTransgressions();

    void* p = nullptr;
    sentinel.setArmed(true);
    const int result = posix_memalign(&p, 24, 64); // 24 is not a power of two -> EINVAL
    const bool detected = sentinel.getAndClearTransgressionsOccured();
    sentinel.setArmed(false);

    REQUIRE(result == EINVAL);
    REQUIRE(p == nullptr);
    REQUIRE(detected);
}
#endif

TEST_CASE("MemorySentinel Tests: hijacking is process-wide, detection is per thread")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setAllocationQuota(0);
    MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);
    sentinel.clearTransgressions();

    std::atomic<bool> go { false };
    std::atomic<bool> done { false };
    bool detectedOnWorker = false;
    void* allocatedOnWorker = nullptr;

    // NOTE: the thread is created while disarmed -- starting a thread allocates
    std::thread worker([&] {
        while (!go.load()) { std::this_thread::yield(); }
        MemorySentinel& workerSentinel = MemorySentinel::getInstance();
        workerSentinel.clearTransgressions();
        allocatedOnWorker = allocWithMalloc();
        detectedOnWorker = workerSentinel.getAndClearTransgressionsOccured();
        done.store(true);
    });

    sentinel.setArmed(true);
    go.store(true);
    while (!done.load()) { std::this_thread::yield(); }
    const bool detectedOnMain = sentinel.getAndClearTransgressionsOccured();
    sentinel.setArmed(false);
    worker.join();
    std::free(allocatedOnWorker);

#if defined(__clang__) || defined(__GNUC__)
    // arming is process-wide, so the worker's allocation is intercepted ...
    REQUIRE(detectedOnWorker);
#endif
    // ... but the transgression is registered in the sentinel of the allocating thread
    REQUIRE_FALSE(detectedOnMain);
}

#ifndef SLB_EXCEPTIONS_DISABLED
namespace
{
/** Allocates in its destructor, i.e. while the stack is being unwound */
struct AllocatesWhenDestroyed
{
    ~AllocatesWhenDestroyed() { allocSink = allocWithNew(); }
};
} // anonymous namespace

TEST_CASE("MemorySentinel Tests: allocating while unwinding is registered, but does not throw")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setAllocationQuota(0);
    MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
    sentinel.clearTransgressions();

    bool hasThrown = false;
    sentinel.setArmed(true);
    try {
        AllocatesWhenDestroyed dtorAllocates; // its allocation happens during unwinding
        allocSink = allocWithNew();           // this one throws
    } catch (const std::bad_alloc&) {
        hasThrown = true;
    }
    const bool detected = sentinel.getAndClearTransgressionsOccured();
    sentinel.setArmed(false);

    // a second exception during unwinding would call std::terminate -- reaching this point proves it did not
    REQUIRE(hasThrown);
    REQUIRE(detected);
}
#endif

TEST_CASE("MemorySentinel Tests: zero-size allocation")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setAllocationQuota(0);
    MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);
    sentinel.clearTransgressions();

    // a 0-byte request must still yield a valid pointer -- new / new[] must never return nullptr
    void* zeroBytes = operator new(0);
    REQUIRE(zeroBytes != nullptr);
    operator delete(zeroBytes);

    void* zeroBytesArray = operator new[](0);
    REQUIRE(zeroBytesArray != nullptr);
    operator delete[](zeroBytesArray);

    sentinel.setArmed(true);
    void* zeroBytesArmed = operator new(0, std::nothrow);
    const bool detected = sentinel.getAndClearTransgressionsOccured();
    sentinel.setArmed(false);

    REQUIRE(zeroBytesArmed == nullptr);
    REQUIRE(detected);
}

TEST_CASE("MemorySentinel Tests: allocations are untouched while not armed")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setAllocationQuota(0);
    MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);
    sentinel.clearTransgressions();
    sentinel.setArmed(false);

    // the nothrow variants behave like the regular ones while not armed
    void* nothrowObject = operator new(32, std::nothrow);
    void* nothrowArray  = operator new[](32, std::nothrow);
    void* object        = operator new(32);
    void* array         = operator new[](32);

    REQUIRE(nothrowObject != nullptr);
    REQUIRE(nothrowArray != nullptr);
    REQUIRE(object != nullptr);
    REQUIRE(array != nullptr);
    REQUIRE_FALSE(sentinel.hasTransgressionOccured());

    operator delete(nothrowObject);
    operator delete[](nothrowArray);
    operator delete(object);
    operator delete[](array);

    REQUIRE_FALSE(sentinel.hasTransgressionOccured());
}
