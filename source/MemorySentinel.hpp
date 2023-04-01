//
//  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
//  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
//  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
//
//  © 2020 Lorenz Bucher - all rights reserved

#pragma once

#include <atomic>
#include <cassert>

// Macro to detect if exceptions are disabled (works on GCC, Clang and MSVC)
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if !__has_feature(cxx_exceptions) && !defined(__cpp_exceptions) && !defined(__EXCEPTIONS) && !defined(_CPPUNWIND)
  #define SLB_EXCEPTIONS_DISABLED
#endif

/**
 * Singleton that hijacks all calls on new, new[], delete and delete[] as well as malloc/free.
 * This is useful to detect whether memory has been allocated in unit tests.
 */
class MemorySentinel {
public:
    enum class TransgressionBehaviour
    {
        LOG,
        THROW_EXCEPTION,
        SILENT,
    };

    /** Returns a MemorySentinel for the current thread. */
    static MemorySentinel& getInstance();
    
    void setArmed(bool value);
    bool isArmed() const { return m_allocationForbidden.load(); }

    static void setTransgressionBehaviour(TransgressionBehaviour b) noexcept { m_transgressionBehaviour.store(b); }
    static TransgressionBehaviour getTransgressionBehaviour() noexcept { return m_transgressionBehaviour.load(); }

    static void setAllocationQuota(int numBytes) noexcept { m_allocationQuota.store(numBytes); }
    static int getRemainingAllocationQuota() noexcept { return m_allocationQuota.load(); }

    void registerTransgression() { m_transgressionOccured.store(true); }
    void clearTransgressions() { m_transgressionOccured.exchange(false); }
    bool hasTransgressionOccured() { return m_transgressionOccured.load(); }
    
    /** NOTE: this clear the transgression upon call */
    bool getAndClearTransgressionsOccured() noexcept
    {
        bool result = m_transgressionOccured.load();
        clearTransgressions();
        return result;
    }

private:
    MemorySentinel() = default; // Singleton = private ctor
    
    static std::atomic<TransgressionBehaviour> m_transgressionBehaviour;
    static std::atomic<int> m_allocationQuota; // allocation Quota in bytes
    
    std::atomic<bool> m_allocationForbidden { false };
    std::atomic<bool> m_transgressionOccured { false };
};


class ScopedMemorySentinel
{
public:
    ScopedMemorySentinel(int allocationQuotaBytes = 0)
    {
        MemorySentinel::setAllocationQuota(allocationQuotaBytes);
        if (allocationQuotaBytes > 0) {
            MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION);
        } else {
            MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::LOG);
        }
        auto& sentinel = MemorySentinel::getInstance();
        sentinel.clearTransgressions();
        sentinel.setArmed(true);
    }

    ~ScopedMemorySentinel()
    {
        auto& sentinel = MemorySentinel::getInstance();
        sentinel.setArmed(false);
        if (sentinel.getAndClearTransgressionsOccured() &&
            sentinel.getTransgressionBehaviour() != MemorySentinel::TransgressionBehaviour::THROW_EXCEPTION) {
            
            assert(false && "MemorySentinel was triggered!");
        }
    }
};
