//
//  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
//  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
//  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
//
//  © 2020 Lorenz Bucher - all rights reserved

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

static inline void handleTransgression(const char* optionalMsg, std::size_t size = 0)
{
    if (MemorySentinel::getInstance().isArmed() == false) {
        return;
    }

    MemorySentinel::getInstance().registerTransgression();
    
    switch (MemorySentinel::getTransgressionBehaviour())
    {
        case MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION: {
            throw std::bad_alloc();
        }
        case MemorySentinel::TransgressionBehaviour::LOG: {
            if (size !=0) {
                printf("[MemorySentinel]: !!Transgression detected!! %s - %zu Bytes\n", optionalMsg, size);
            } else {
                printf("[MemorySentinel]: !!Transgression detected!! %s \n", optionalMsg);
            }
            break;
        }
        case MemorySentinel::TransgressionBehaviour::SILENT: {
            break;
        }
    }
}

// Using pattern described here: https://stackoverflow.com/a/17850402/649700
static bool isHijackActive = false;

void hijack(const char* msg, std::size_t size = 0) noexcept(false)
{
    // Disabling 'hijack' while running 'trangression handler'
    isHijackActive = false;
    handleTransgression(msg, size);
    isHijackActive = true;
}

void hijack(const char* msg, std::size_t size, std::nothrow_t const&) noexcept
{
    hijack(msg, size);
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
        hijack("deallocation with free");
    }
    builtinFree(ptr);
}

#endif // ifdef GNU/Clang

// --------------------------------------------------------------------------------------------------------------------
// MARK: - new
void* operator new(std::size_t size) noexcept(false)
{
    if (isHijackActive) {
        hijack("allocation with new", size);
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
    }
    return std::malloc(size);
}

// MARK: - new nothrow
void* operator new(std::size_t size, std::nothrow_t const& nt) noexcept
{
    if (isHijackActive) {
        hijack("allocation with new (nothrow)", size, nt);
    }
    return std::malloc(size);
}

// MARK: - new[] nothrow
void* operator new[](std::size_t size, std::nothrow_t const& nt) noexcept
{
    if (isHijackActive) {
        hijack("allocation with new[] (nothrow)", size, nt);
    }
    return std::malloc(size);
}

// MARK: - delete
void operator delete(void* ptr) noexcept
{
    if (isHijackActive) {
        hijack("deallocation with delete");
    }
    std::free(ptr);
}

// MARK: - delete
void operator delete[](void* ptr) noexcept
{
    if (isHijackActive) {
        hijack("deallocation with delete[]");
    }
    std::free(ptr);
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
