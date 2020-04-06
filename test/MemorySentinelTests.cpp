//
//  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
//  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
//  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
//
//  © 2020 Lorenz Bucher - all rights reserved

#include <catch2/catch.hpp>

#include "MemorySentinel.hpp"

#include <vector>

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

TEST_CASE("MemorySentinel Tests")
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
    
    sentinel.setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
    sentinel.setArmed(true);
    REQUIRE(sentinel.isArmed());
    REQUIRE_THROWS(allocWithNew());
    REQUIRE(heapObject != nullptr);
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
    // clean-up not necessary, since allocation was intercepted by exception

    sentinel.setArmed(true);
    REQUIRE(sentinel.isArmed());
    REQUIRE_THROWS(allocWithNewArray());
    REQUIRE(heapObject != nullptr);
    REQUIRE(sentinel.getAndClearTransgressionsOccured());
    // clean-up not necessary, since allocation was intercepted by exception
    
//    sentinel.setArmed(true);
//    REQUIRE(sentinel.isArmed());
//    REQUIRE_THROWS(allocWithMalloc());
//    REQUIRE(heapObject != nullptr);
//    REQUIRE(sentinel.getAndClearTransgressionsOccured());
//    // clean-up not necessary, since allocation was intercepted by exception
    
    // After tests, disarm Sentinel
    sentinel.setArmed(false);
    sentinel.clearTransgressions();
}

TEST_CASE("ScopedMemorySentinel Tests")
{
    {
        ScopedMemorySentinel sentinel;
        // THIS WILL ASSERT
        //std::vector<float>* heapObject = allocWithNew();
    }
}
