//
//  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
//  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
//  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
//
//  © 2020 Lorenz Bucher - all rights reserved

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

static decltype(auto) allocWithNew()
{
    return new std::vector<float>(32);
}
static decltype(auto) allocWithNewArray()
{
    return new float[32];
}
static decltype(auto) allocWithMalloc()
{
    return std::malloc(32*sizeof(float));
}

TEST_CASE("MemorySentinel Tests: zero allocation quota (default)")
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    
    sentinel.setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);
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
    
#ifndef SLB_EXCEPTIONS_DISABLED
    sentinel.setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
    sentinel.setArmed(true);
    REQUIRE_THROWS(allocWithNew());
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
    // clean-up not necessary, since allocation was intercepted by exception
    
    sentinel.setArmed(true);
    REQUIRE_THROWS(allocWithNewArray());
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
    // clean-up not necessary, since allocation was intercepted by exception
    
    #if (defined(__clang__) || defined(__GNUC__)) && !defined(__GLIBC__)
        sentinel.setArmed(true);
        REQUIRE_THROWS(allocWithMalloc());
        REQUIRE(sentinel.getAndClearTransgressionsOccured());
        // clean-up not necessary, since allocation was intercepted by exception
        
        sentinel.setArmed(false);
        auto m = allocWithMalloc();
        sentinel.setArmed(true);
        REQUIRE_THROWS(free(m));
        REQUIRE(sentinel.getAndClearTransgressionsOccured());
        if (m) free(m);
    #endif
#endif
    
    sentinel.setArmed(false);
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
        // this will throw a std::bad_alloc (allocating 1 byte too many)
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
