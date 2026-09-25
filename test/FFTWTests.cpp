//
//  ╔╦╗┌─┐┌┬┐┌─┐┬─┐┬ ┬  ╔═╗┌─┐┌┐┌┌┬┐┬┌┐┌┌─┐┬
//  ║║║├┤ ││││ │├┬┘└┬┘  ╚═╗├┤ │││ │ ││││├┤ │
//  ╩ ╩└─┘┴ ┴└─┘┴└─ ┴   ╚═╝└─┘┘└┘ ┴ ┴┘└┘└─┘┴─┘
//
//  © 2026 Lorenz Bucher - all rights reserved
//  https://github.com/Sidelobe/MemorySentinel

#include <catch2/catch.hpp>

#include "MemorySentinel.hpp"

#include <fftw3.h>

#include <vector>

// malloc/free are only intercepted on GCC / Clang -- on MSVC, FFTW's allocations are invisible
#if defined(__clang__) || defined(__GNUC__)
    #define SLB_MALLOC_INTERCEPTED 1
#endif

namespace
{

template<typename F>
static bool allocates(F&& operation)
{
    MemorySentinel& sentinel = MemorySentinel::getInstance();
    MemorySentinel::setAllocationQuota(0);
    MemorySentinel::setTransgressionBehaviour(MemorySentinel::TransgressionBehaviour::SILENT);
    sentinel.clearTransgressions();
    sentinel.setArmed(true);
    operation();
    sentinel.setArmed(false);
    return sentinel.getAndClearTransgressionsOccured();
}

static constexpr int kPow2    = 1024;
static constexpr int kNonPow2 = 480;

/** Real & complex scratch buffers, optionally misaligned by one float */
struct Buffers
{
    explicit Buffers(int n, bool misaligned = false) :
        m_real(static_cast<size_t>(n) + 2),
        m_complex(2 * (static_cast<size_t>(n) / 2 + 1) + 2),
        m_offset(misaligned ? 1 : 0) {}

    float* real()            { return m_real.data() + m_offset; }
    fftwf_complex* complex() { return reinterpret_cast<fftwf_complex*>(m_complex.data() + m_offset); }

    std::vector<float> m_real;
    std::vector<float> m_complex;
    size_t m_offset;
};

} // anonymous namespace

#ifdef SLB_MALLOC_INTERCEPTED
    #define REQUIRE_ALLOCATES(expr)      REQUIRE(allocates([&]{ expr; }))
#else
    #define REQUIRE_ALLOCATES(expr)      (void) allocates([&]{ expr; })
#endif

#define REQUIRE_NO_ALLOCATION(expr)      REQUIRE_FALSE(allocates([&]{ expr; }))


TEST_CASE("FFTW Tests: planning always allocates")
{
    Buffers b(kPow2);
    fftwf_plan plan = nullptr;

    SECTION("forward r2c, power of 2") {
        REQUIRE_ALLOCATES(plan = fftwf_plan_dft_r2c_1d(kPow2, b.real(), b.complex(), FFTW_ESTIMATE));
    }
    SECTION("forward r2c, non-power of 2") {
        Buffers c(kNonPow2);
        REQUIRE_ALLOCATES(plan = fftwf_plan_dft_r2c_1d(kNonPow2, c.real(), c.complex(), FFTW_ESTIMATE));
    }
    SECTION("forward c2c") {
        REQUIRE_ALLOCATES(plan = fftwf_plan_dft_1d(kPow2, b.complex(), b.complex(), FFTW_FORWARD, FFTW_ESTIMATE));
    }
    SECTION("inverse c2c") {
        REQUIRE_ALLOCATES(plan = fftwf_plan_dft_1d(kPow2, b.complex(), b.complex(), FFTW_BACKWARD, FFTW_ESTIMATE));
    }
    SECTION("inverse c2r") {
        REQUIRE_ALLOCATES(plan = fftwf_plan_dft_c2r_1d(kPow2, b.complex(), b.real(), FFTW_ESTIMATE));
    }
    SECTION("FFTW_UNALIGNED") {
        REQUIRE_ALLOCATES(plan = fftwf_plan_dft_r2c_1d(kPow2, b.real(), b.complex(), FFTW_ESTIMATE | FFTW_UNALIGNED));
    }

    REQUIRE(plan != nullptr);
    REQUIRE_ALLOCATES(fftwf_destroy_plan(plan)); // freeing is a transgression too
}

TEST_CASE("FFTW Tests: fftwf_malloc / fftwf_free")
{
    float* data = nullptr;
    REQUIRE_ALLOCATES(data = fftwf_alloc_real(kPow2));
    REQUIRE(data != nullptr);
    REQUIRE_ALLOCATES(fftwf_free(data));
}

TEST_CASE("FFTW Tests: forward r2c execution does not allocate")
{
    const int n = GENERATE(1024, 256, 480, 1000);
    const bool misaligned = GENERATE(false, true);
    const unsigned flags = GENERATE(FFTW_ESTIMATE,
                                    FFTW_ESTIMATE | FFTW_PRESERVE_INPUT,
                                    FFTW_ESTIMATE | FFTW_DESTROY_INPUT,
                                    FFTW_ESTIMATE | FFTW_UNALIGNED);

    Buffers b(n, misaligned);
    fftwf_plan plan = fftwf_plan_dft_r2c_1d(n, b.real(), b.complex(), flags);
    REQUIRE(plan != nullptr);

    REQUIRE_NO_ALLOCATION(fftwf_execute(plan));
    REQUIRE_NO_ALLOCATION(fftwf_execute(plan)); // repeated execution stays allocation-free

    fftwf_destroy_plan(plan);
}

TEST_CASE("FFTW Tests: c2c execution does not allocate")
{
    const int n = GENERATE(1024, 256, 480, 1000);
    const int sign = GENERATE(FFTW_FORWARD, FFTW_BACKWARD);
    const bool misaligned = GENERATE(false, true);
    const unsigned flags = GENERATE(FFTW_ESTIMATE,
                                    FFTW_ESTIMATE | FFTW_PRESERVE_INPUT,
                                    FFTW_ESTIMATE | FFTW_DESTROY_INPUT,
                                    FFTW_ESTIMATE | FFTW_UNALIGNED);

    std::vector<float> in(2 * static_cast<size_t>(n) + 2);
    std::vector<float> out(2 * static_cast<size_t>(n) + 2);
    const size_t offset = misaligned ? 1 : 0;
    fftwf_complex* cIn  = reinterpret_cast<fftwf_complex*>(in.data() + offset);
    fftwf_complex* cOut = reinterpret_cast<fftwf_complex*>(out.data() + offset);

    fftwf_plan plan = fftwf_plan_dft_1d(n, cIn, cOut, sign, flags);
    REQUIRE(plan != nullptr);

    REQUIRE_NO_ALLOCATION(fftwf_execute(plan));
    REQUIRE_NO_ALLOCATION(fftwf_execute_dft(plan, cIn, cOut)); // new-array variant

    fftwf_destroy_plan(plan);
}

TEST_CASE("FFTW Tests: inverse c2r execution")
{
    const int n = GENERATE(1024, 256, 480, 1000);
    const bool misaligned = GENERATE(false, true);

    SECTION("input may be destroyed: no allocation") {
        const unsigned flags = GENERATE(FFTW_ESTIMATE,
                                        FFTW_ESTIMATE | FFTW_DESTROY_INPUT,
                                        FFTW_ESTIMATE | FFTW_DESTROY_INPUT | FFTW_UNALIGNED);
        Buffers b(n, misaligned);
        fftwf_plan plan = fftwf_plan_dft_c2r_1d(n, b.complex(), b.real(), flags);
        REQUIRE(plan != nullptr);

        REQUIRE_NO_ALLOCATION(fftwf_execute(plan));
        REQUIRE_NO_ALLOCATION(fftwf_execute(plan));

        fftwf_destroy_plan(plan);
    }

    SECTION("FFTW_PRESERVE_INPUT: FFTW copies the input into a scratch buffer") {
        Buffers b(n, misaligned);
        fftwf_plan plan = fftwf_plan_dft_c2r_1d(n, b.complex(), b.real(), FFTW_ESTIMATE | FFTW_PRESERVE_INPUT);
        REQUIRE(plan != nullptr);

        // buffered rdft2 solvers allocate their scratch buffer at execution time -- every time
        REQUIRE_ALLOCATES(fftwf_execute(plan));
        REQUIRE_ALLOCATES(fftwf_execute(plan));

        fftwf_destroy_plan(plan);
    }
}
