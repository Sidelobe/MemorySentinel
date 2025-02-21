//
//  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
//  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
//  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
//
//  © 2025 Lorenz Bucher - all rights reserved
//  https://github.com/Sidelobe/MemorySentinel

#include "MemorySentinel.hpp"

#include <cstdlib>
#include <future>
#include <string>

// Note: malloc overwrite only supported on GCC / Clang
#if defined(__clang__) || defined(__GNUC__)
    #include <dlfcn.h>
    #if defined(__GLIBC__ )
        #include <malloc.h>
    #endif
#endif
static void handleTransgressionException() noexcept(false)
{
#ifdef SLB_EXCEPTIONS_DISABLED
    assert(false && "[Exceptions disabled]");
#else
    throw std::bad_alloc();
#endif
}

template<class ExceptionHandler>
static bool handleTransgression(const char* optionalMsg, std::size_t size, ExceptionHandler exceptionHandler)
{
    assert(MemorySentinel::getInstance().isArmed());
    
    int availableQuota = MemorySentinel::getRemainingAllocationQuota();
    if (availableQuota > 0 && size <= availableQuota) {
        MemorySentinel::setAllocationQuota(availableQuota - static_cast<int>(size));
        printf("[MemorySentinel]: permitted allocation in %s - %zu Bytes quota remaining\n",
               optionalMsg, static_cast<std::size_t>(MemorySentinel::getRemainingAllocationQuota()));
        return true; // this allocation was allowed
    }

    MemorySentinel::getInstance().registerTransgression();
    
    switch (MemorySentinel::getTransgressionBehaviour())
    {
        case MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION: {
            exceptionHandler();
            return false;
        }
        case MemorySentinel::TransgressionBehaviour::LOG: {
            if (size !=0) {
                printf("[MemorySentinel]: !!Transgression detected!! %s - %zu Bytes\n", optionalMsg, size);
            } else {
                printf("[MemorySentinel]: !!Transgression detected!! %s \n", optionalMsg);
            }
            return false;
        }
        case MemorySentinel::TransgressionBehaviour::SILENT: {
            return false;
        }
    }
    
    return false;
}

// Using pattern described here: https://stackoverflow.com/a/17850402/649700
static bool isHijackActive = false;

/** exception-throwing variant */
static decltype(auto) hijack(const char* msg, std::size_t size = 0) noexcept(false)
{
    // Disabling 'hijack' while running 'trangression handler'
    isHijackActive = false;
    auto retValue = handleTransgression(msg, size, handleTransgressionException);
    isHijackActive = true;
    return retValue;
}
/** no-except variant */
static decltype(auto) hijack(const char* msg, std::size_t size, std::nothrow_t const&) noexcept(true)
{
    // Disabling 'hijack' while running 'trangression handler'
    isHijackActive = false;
    auto retValue = handleTransgression(msg, size, [](){ return false; }); // dummy transgression handler simply return false in case an exception occurs
    isHijackActive = true;
    return retValue;
}



// --------------------------------------------------------------------------------------------------------------------
// MARK: - Hijack malloc/free
// TODO: This should work for GLIBC, however, symbols are unresolved in TravisCI environment - that's why we disable it for now
#if (defined(__clang__) || defined(__GNUC__)) && !defined(__GLIBC__)

static void* (*builtinMalloc)(size_t) = nullptr;
static void* (*builtinCalloc)(size_t, size_t) = nullptr;
static void* (*builtinRealloc)(void*, size_t) = nullptr;
static void (*builtinFree)(void*) = nullptr;

static void initMallocHijack()
{
#if defined(__GLIBC__ )
    extern void* __libc_malloc(size_t);
    extern void* __libc_calloc(size_t, size_t);
    extern void* __libc_realloc(void*, size_t);
    extern void __libc_free(void*);
    builtinMalloc =  __libc_malloc;
    builtinCalloc = __libc_calloc;
    builtinRealloc = __libc_realloc;
    builtinFree = __libc_free;
#else
    builtinMalloc = (void* (*)(size_t)) dlsym(RTLD_NEXT, "malloc");
    builtinCalloc = (void* (*)(size_t, size_t)) dlsym(RTLD_NEXT, "calloc");
    builtinRealloc = (void* (*)(void*, size_t)) dlsym(RTLD_NEXT, "realloc");
    builtinFree = (void (*)(void*)) dlsym(RTLD_NEXT, "free");
#endif

    if (!(builtinMalloc && builtinCalloc && builtinRealloc && builtinFree)) {
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
        exit(1);
    }
}

void* malloc(size_t size)
{
    if (builtinMalloc == nullptr) {
        initMallocHijack();
    }
    if (isHijackActive) {
        hijack("allocation with malloc", size);
    }
    return builtinMalloc(size);
}

void* calloc(size_t num, size_t size)
{
    if (builtinCalloc == nullptr) {
        initMallocHijack();
    }
    if (isHijackActive) {
        hijack("allocation with calloc", size);
    }
    return builtinCalloc(num, size);
}

void* realloc(void* ptr, size_t size)
{
    if (builtinRealloc == nullptr) {
        initMallocHijack();
    }
    if (isHijackActive) {
        hijack("allocation with realloc", size);
    }
    return builtinRealloc(ptr, size);
}

void free(void* ptr)
{
    if (builtinFree == nullptr) {
        initMallocHijack();
    }
    if (isHijackActive) {
        std::nothrow_t nt; // force non-throwing overload with tag
        hijack("deallocation with free", 0, nt);
    }
    builtinFree(ptr);
}

#else // ifdef GNU/Clang
// Define these for Microsoft Compiler and GCC without GLIB, as they're used in new/delete overrides
void* builtinMalloc(size_t size)
{
    return std::malloc(size);
}
void builtinFree(void* ptr)
{
    return std::free(ptr);
}
#endif

// --------------------------------------------------------------------------------------------------------------------
// MARK: - new
void* operator new(std::size_t size) noexcept(false)
{
    if (isHijackActive) {
        hijack("allocation with new", size);
        return builtinMalloc(size); // allocate the memory with the 'un-hijacked' malloc.
    }
    if (size == 0) { // Handle 0-byte requests by treating them as 1-byte requests
      size = 1;
    }
    return std::malloc(size);
}

// MARK: - new[]
void* operator new[](std::size_t size) noexcept(false)
{
    if (isHijackActive) {
        hijack("allocation with new[]", size);
        return builtinMalloc(size); // allocate the memory with the 'un-hijacked' malloc.
    }
    return std::malloc(size);
}

// MARK: - new noexcept
void* operator new(std::size_t size, std::nothrow_t const& nt) noexcept(true)
{
    if (isHijackActive) {
        hijack("allocation with new (nothrow)", size, nt); // will always return false
        return nullptr; // convention
    }
    return std::malloc(size);
}

// MARK: - new[] noexcept
void* operator new[](std::size_t size, std::nothrow_t const& nt) noexcept(true)
{
    if (isHijackActive) {
        hijack("allocation with new[] (nothrow)", size, nt); // will always return false
        return nullptr; // convention
    }
    return std::malloc(size);
}

// MARK: - delete -- always noexcept
void operator delete(void* ptr) noexcept(true)
{
    if (isHijackActive) {
        std::nothrow_t nt; // force non-throwing overload with tag
        hijack("deallocation with delete", 0, nt);
        builtinFree(ptr); // free the memory with the 'un-hijacked' free.
    } else {
        std::free(ptr);
    }
}

// MARK: - delete[]  -- always noexcept
void operator delete[](void* ptr) noexcept(true)
{
    if (isHijackActive) {
        std::nothrow_t nt; // force non-throwing overload with tag
        hijack("deallocation with delete[]", 0, nt);
        builtinFree(ptr); // free the memory with the 'un-hijacked' free.
    } else {
        std::free(ptr);
    }
}

// --------------------------------------------------------------------------------------------------------------------
// MARK: - MemorySentinel

// initialization (static non-const must be initialized out out line
std::atomic<MemorySentinel::TransgressionBehaviour> MemorySentinel::m_transgressionBehaviour(TransgressionBehaviour::LOG);
std::atomic<int> MemorySentinel::m_allocationQuota(0);

MemorySentinel& MemorySentinel::getInstance()
{
    thread_local MemorySentinel instance;
    return instance;
}

void MemorySentinel::setArmed(bool value)
{
    m_allocationForbidden.store(value);
    isHijackActive = value;
}

bool MemorySentinel::getAndClearTransgressionsOccured() noexcept
{
    bool result = m_transgressionOccured.load();
    clearTransgressions();
    return result;
}
