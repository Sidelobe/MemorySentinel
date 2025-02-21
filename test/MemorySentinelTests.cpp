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

static void testAllocation(MemorySentinel& sentinel, std::function<void*()>&& allocFunc)
{
    sentinel.setArmed(true);
    
    volatile float* a = 0; // dummy to avoid optimization
    REQUIRE_THROWS(a = (float*) allocFunc());
    
    sentinel.setArmed(false);
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
    // freeing not necessary, since allocation was intercepted by exception
}

static void testFreeing(MemorySentinel& sentinel, std::function<void*()>&& allocFunc, std::function<void(void*)>&& freeFunc)
{
    // allocate with unarmed sentinel
    sentinel.setArmed(false);
    volatile auto m = allocFunc();
    sentinel.setArmed(true);
    
    freeFunc(m); // always noexcept
    
    // freeing took place, no exception was thrown
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
    sentinel.setArmed(false);
}

template<typename T>
static void testDelete(MemorySentinel& sentinel, T&& allocFunc)
{
    // allocate with unarmed sentinel
    sentinel.setArmed(false);
    auto m = allocFunc();
    sentinel.setArmed(true);
    
    operator delete(m);  // always noexcept
    
    // deletion took place, no exception was thrown
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
    sentinel.setArmed(false);
}

template<typename T>
static void testDeleteArray(MemorySentinel& sentinel, T&& allocFunc)
{
    // allocate with unarmed sentinel
    sentinel.setArmed(false);
    auto m = allocFunc();
    sentinel.setArmed(true);
    
    operator delete[](m);  // always noexcept
    
    // deletion took place, no exception was thrown
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
    sentinel.setArmed(false);
}

TEST_CASE("MemorySentinel Tests: zero allocation quota (default)")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    sentinel.getAndClearTransgressionsOccured();
    
    SECTION("SILENT") {
        MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);
        sentinel.setArmed(false);
        REQUIRE_FALSE(sentinel.isArmed());
        std::vector<float>* heapObject = allocWithNew();
        REQUIRE(heapObject != nullptr);
        REQUIRE_FALSE(sentinel.hasTransgressionOccured());
        sentinel.clearTransgressions();
        delete heapObject; // clean up
        
        sentinel.setArmed(true);
        REQUIRE(sentinel.isArmed());
        heapObject = allocWithNew();
        REQUIRE(heapObject != nullptr);
        REQUIRE(sentinel.getAndClearTransgressionsOccured());
        delete heapObject; // clean up
        REQUIRE(sentinel.getAndClearTransgressionsOccured());
        
        sentinel.setArmed(true);
        REQUIRE(sentinel.isArmed());
        float* heapArray = allocWithNewArray();
        REQUIRE(heapArray != nullptr);
        REQUIRE(sentinel.getAndClearTransgressionsOccured());
        delete[] heapArray; // clean up
        REQUIRE(sentinel.getAndClearTransgressionsOccured());
        sentinel.setArmed(false);
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
    #endif // (defined(__clang__) || defined(__GNUC__)) && !defined(__GLIBC__)
    
#endif // SLB_EXCEPTIONS_DISABLED
    
    // After tests, disarm Sentinel
    sentinel.clearTransgressions();
}

TEST_CASE("ScopedMemorySentinel Tests")
{
    {
        ScopedMemorySentinel sentinel;
        // THIS WILL ASSERT (default behaviour)
        //std::vector<float>* heapObject = allocWithNew();
    }
    {
        ScopedMemorySentinel sentinel;
        MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
        REQUIRE_THROWS_AS(allocWithNew(), std::bad_alloc);
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
    {
        // allocation size is just right
        ScopedMemorySentinel sentinel(bytesAllocatedFor32FloatVector);
        heapObject = allocWithNew();
    }
    delete heapObject; // clean up

    {
        // this will throw a std::bad_alloc (allocating 1 byte too many)
        ScopedMemorySentinel sentinel(bytesAllocatedFor32FloatVector-1);
        REQUIRE_THROWS_AS(allocWithNew(), std::bad_alloc);
    }
    
    // this works because we're out of scope of the ScopedMemorySentinel
    heapObject = allocWithNew();
    delete heapObject; // clean up
}
