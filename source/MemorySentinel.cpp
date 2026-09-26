//
//  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
//  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
//  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
//
//  © 2025 Lorenz Bucher - all rights reserved
//  https://github.com/Sidelobe/MemorySentinel

#include "MemorySentinel.hpp"

#include <cerrno>
#include <exception>
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

#if defined(__clang__) || defined(__GNUC__)
__attribute__((noreturn))
#endif
static void handleTransgressionException() noexcept(false)
{
#ifdef SLB_EXCEPTIONS_DISABLED
    assert(false && "[Exceptions disabled]");
#else
    throw std::bad_alloc();
#endif
}

// Using pattern described here: https://stackoverflow.com/a/17850402/649700
static bool isHijackActive = false;

/**
 * While a transgression is being handled, hijacking has to be suspended: the handler itself may
 * allocate (printf, the exception object, ...), -> infinite recursion.
 * NOTE: deliberately _not_ thread_local, as this could allocate.
 */
static bool isHandlingTransgression = false;

/** Hijacking is active, and we are not already inside the transgression handler */
static inline bool shouldHijack() noexcept
{
    return isHijackActive && !isHandlingTransgression;
}

/** Suspends hijacking for as long as it exists -- RAII, so it also recovers when the handler throws */
struct TransgressionHandlerGuard
{
    TransgressionHandlerGuard()  noexcept { isHandlingTransgression = true;  }
    ~TransgressionHandlerGuard() noexcept { isHandlingTransgression = false; }
};

/** Throwing while another exception is propagating would call std::terminate() */
static inline bool isExceptionInFlight() noexcept
{
#if defined(__cpp_lib_uncaught_exceptions)
    return std::uncaught_exceptions() > 0;
#else
    return std::uncaught_exception();
#endif
}

template<class ExceptionHandler>
static bool handleTransgression(const char* optionalMsg, std::size_t size, ExceptionHandler exceptionHandler)
{
    assert(isHijackActive);
    
    // NOTE: the quota applies to allocations only -- deallocations (size == 0) are always a transgression
    int availableQuota = MemorySentinel::getRemainingAllocationQuota();
    if (size > 0 && availableQuota > 0 && size <= static_cast<std::size_t>(availableQuota)) {
        MemorySentinel::setAllocationQuota(availableQuota - static_cast<int>(size));
        printf("[MemorySentinel]: permitted allocation in %s - %zu Bytes quota remaining\n",
               optionalMsg, static_cast<std::size_t>(MemorySentinel::getRemainingAllocationQuota()));
        return true; // this allocation was allowed
    }

    MemorySentinel::getInstance().registerTransgression();
    
    switch (MemorySentinel::getTransgressionBehaviour())
    {
        case MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION: {
            // While unwinding, we can only register the transgression -- throwing would terminate
            if (!isExceptionInFlight()) {
                exceptionHandler();
            }
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

/** exception-throwing variant */
static decltype(auto) hijack(const char* msg, std::size_t size = 0) noexcept(false)
{
    TransgressionHandlerGuard guard;
    return handleTransgression(msg, size, handleTransgressionException);
}
/** no-except variant */
static decltype(auto) hijack(const char* msg, std::size_t size, std::nothrow_t const&) noexcept(true)
{
    TransgressionHandlerGuard guard;
    // dummy transgression handler simply returns false in case an exception occurs
    return handleTransgression(msg, size, [](){ return false; });
}

/** Deallocating a nullptr (free / delete / delete[]) is a no-op and must never count as a transgression */
static inline bool isNoOpDealloc(void* ptr) noexcept
{
    return ptr == nullptr;
}



// --------------------------------------------------------------------------------------------------------------------
// MARK: - Hijack malloc/free

#if (defined(__clang__) || defined(__GNUC__))

static void* (*builtinMalloc)(size_t) = nullptr;
static void* (*builtinCalloc)(size_t, size_t) = nullptr;
static void* (*builtinRealloc)(void*, size_t) = nullptr;
static void (*builtinFree)(void*) = nullptr;
static void* (*builtinMemalign)(size_t, size_t) = nullptr;         ///< memalign / aligned_alloc (same signature)
static int (*builtinPosixMemalign)(void**, size_t, size_t) = nullptr;

#if defined(__GLIBC__)
// When using GLIBC, dlsym itself may call malloc() etc, which would trigger a recursion. Therefore, we use these aliases
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t, size_t);
extern "C" void* __libc_realloc(void*, size_t);
extern "C" void __libc_free(void*);
extern "C" void* __libc_memalign(size_t, size_t);
#endif

static void initMallocHijack()
{
#if defined(__GLIBC__)
    builtinMalloc =  __libc_malloc;
    builtinCalloc = __libc_calloc;
    builtinRealloc = __libc_realloc;
    builtinFree = __libc_free;
    // NOTE: glibc has no __libc_posix_memalign / __libc_aligned_alloc -- both are built on memalign below
    builtinMemalign = __libc_memalign;
#else
    builtinMalloc = (void* (*)(size_t)) dlsym(RTLD_NEXT, "malloc");
    builtinCalloc = (void* (*)(size_t, size_t)) dlsym(RTLD_NEXT, "calloc");
    builtinRealloc = (void* (*)(void*, size_t)) dlsym(RTLD_NEXT, "realloc");
    builtinFree = (void (*)(void*)) dlsym(RTLD_NEXT, "free");
    // NOTE: these are optional -- e.g. memalign does not exist on macOS
    builtinMemalign = (void* (*)(size_t, size_t)) dlsym(RTLD_NEXT, "aligned_alloc");
    builtinPosixMemalign = (int (*)(void**, size_t, size_t)) dlsym(RTLD_NEXT, "posix_memalign");
#endif

    if (!(builtinMalloc && builtinCalloc && builtinRealloc && builtinFree)) {
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
        exit(1);
    }
}

/** An alignment is valid for posix_memalign if it is a power of two multiple of sizeof(void*) */
static inline bool isValidAlignment(size_t alignment) noexcept
{
    return alignment != 0 && (alignment % sizeof(void*)) == 0 && (alignment & (alignment - 1)) == 0;
}

/** initMallocHijack() resolves all pointers at once, so a single guard suffices for all allocators */
static inline void ensureInitialized()
{
    if (builtinMalloc == nullptr) {
        initMallocHijack();
    }
}

void* malloc(size_t size)
{
    ensureInitialized();
    if (shouldHijack()) {
        hijack("allocation with malloc", size);
    }
    return builtinMalloc(size);
}

void* calloc(size_t num, size_t size)
{
    ensureInitialized();
    if (shouldHijack()) {
        hijack("allocation with calloc", num * size);
    }
    return builtinCalloc(num, size);
}

void* realloc(void* ptr, size_t size)
{
    ensureInitialized();
    if (shouldHijack()) {
        hijack("allocation with realloc", size);
    }
    return builtinRealloc(ptr, size);
}

void free(void* ptr)
{
    ensureInitialized();
    if (isNoOpDealloc(ptr)) { return; }

    if (shouldHijack()) {
        std::nothrow_t nt; // force non-throwing overload with tag
        hijack("deallocation with free", 0, nt);
    }
    builtinFree(ptr);
}

// MARK: - Hijack aligned allocations
// NOTE: memory from these is released with free(), which is hijacked above

#if defined(__GLIBC__)
/** memalign is a GLIBC extension -- it does not exist e.g. on macOS */
extern "C" void* memalign(size_t alignment, size_t size)
{
    ensureInitialized();
    if (shouldHijack()) {
        hijack("allocation with memalign", size);
    }
    return builtinMemalign(alignment, size);
}
#endif

extern "C" void* aligned_alloc(size_t alignment, size_t size)
{
    ensureInitialized();
    if (shouldHijack()) {
        hijack("allocation with aligned_alloc", size);
    }
    return builtinMemalign(alignment, size);
}

extern "C" int posix_memalign(void** memptr, size_t alignment, size_t size)
{
    ensureInitialized();
    if (shouldHijack()) {
        hijack("allocation with posix_memalign", size);
    }
    if (builtinPosixMemalign != nullptr) {
        return builtinPosixMemalign(memptr, alignment, size);
    }

    // GLIBC exports no __libc_posix_memalign entry point, and calling the real posix_memalign here would simply
    // re-enter this hijack. We route to __libc_memalign and handle the return codes.
    if (memptr == nullptr || !isValidAlignment(alignment)) {
        return EINVAL;
    }
    void* ptr = builtinMemalign(alignment, size);
    if (ptr == nullptr) {
        return ENOMEM;
    }
    *memptr = ptr;
    return 0;
}

#else // All compilers other than GNU/Clang
// Define these for Microsoft Compiler and GCC without GLIB, as they're used in new/delete overrides
void* builtinMalloc(size_t size)
{
    return std::malloc(size);
}
void builtinFree(void* ptr)
{
    return std::free(ptr);
}
#endif // (defined(__clang__) || defined(__GNUC__))

// --------------------------------------------------------------------------------------------------------------------
// MARK: - new
void* operator new(std::size_t size) noexcept(false)
{
    if (shouldHijack()) {
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
    if (shouldHijack()) {
        hijack("allocation with new[]", size);
        return builtinMalloc(size); // allocate the memory with the 'un-hijacked' malloc.
    }
    if (size == 0) { // Handle 0-byte requests by treating them as 1-byte requests
      size = 1;
    }
    return std::malloc(size);
}

// MARK: - new noexcept
void* operator new(std::size_t size, std::nothrow_t const& nt) noexcept(true)
{
    if (shouldHijack()) {
        hijack("allocation with new (nothrow)", size, nt); // will always return false
        return nullptr; // convention
    }
    return std::malloc(size);
}

// MARK: - new[] noexcept
void* operator new[](std::size_t size, std::nothrow_t const& nt) noexcept(true)
{
    if (shouldHijack()) {
        hijack("allocation with new[] (nothrow)", size, nt); // will always return false
        return nullptr; // convention
    }
    return std::malloc(size);
}

// MARK: - delete -- always noexcept
void operator delete(void* ptr) noexcept(true)
{
    if (isNoOpDealloc(ptr)) { return; }

    if (shouldHijack()) {
        std::nothrow_t nt; // force non-throwing overload with tag
        hijack("deallocation with delete", 0, nt);
        builtinFree(ptr); // free the memory with the 'un-hijacked' free.
    } else {
        std::free(ptr);
    }
}

void operator delete(void* ptr, std::size_t size) noexcept(true)
{
    if (isNoOpDealloc(ptr)) { return; }

    if (shouldHijack()) {
        std::nothrow_t nt; // force non-throwing overload with tag
        hijack("deallocation with delete(sz)", size, nt);
        builtinFree(ptr); // free the memory with the 'un-hijacked' free.
    } else {
        std::free(ptr);
    }
}

// MARK: - delete[]  -- always noexcept
void operator delete[](void* ptr) noexcept(true)
{
    if (isNoOpDealloc(ptr)) { return; }

    if (shouldHijack()) {
        std::nothrow_t nt; // force non-throwing overload with tag
        hijack("deallocation with delete[]", 0, nt);
        builtinFree(ptr); // free the memory with the 'un-hijacked' free.
    } else {
        std::free(ptr);
    }
}

void operator delete[](void* ptr, std::size_t size) noexcept(true)
{
    if (isNoOpDealloc(ptr)) { return; }

    if (shouldHijack()) {
        std::nothrow_t nt; // force non-throwing overload with tag
        hijack("deallocation with delete[](sz)", size, nt);
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

MemorySentinel& MemorySentinel::getInstance() noexcept
{
    thread_local MemorySentinel instance;
    return instance;
}

void MemorySentinel::setArmed(bool value) noexcept
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
