#pragma once

// Minimal dependency-free test harness so the DSP tests build anywhere
// (Windows/MSVC, Linux, macOS) without fetching a test framework.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace peqtest
{

struct TestCase
{
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar
{
    Registrar (const char* name, std::function<void()> fn) { registry().push_back ({ name, std::move (fn) }); }
};

inline int& failureCount()   { static int n = 0; return n; }
inline int& checkCount()     { static int n = 0; return n; }

inline void reportFailure (const char* file, int line, const std::string& msg)
{
    ++failureCount();
    std::printf ("    FAIL %s:%d  %s\n", file, line, msg.c_str());
}

} // namespace peqtest

#define PEQ_CONCAT_INNER(a, b) a##b
#define PEQ_CONCAT(a, b) PEQ_CONCAT_INNER (a, b)

#define TEST_CASE(name)                                                            \
    static void PEQ_CONCAT (peqTestFn_, __LINE__)();                               \
    static peqtest::Registrar PEQ_CONCAT (peqTestReg_, __LINE__) (name, &PEQ_CONCAT (peqTestFn_, __LINE__)); \
    static void PEQ_CONCAT (peqTestFn_, __LINE__)()

#define CHECK(cond)                                                                \
    do {                                                                           \
        ++peqtest::checkCount();                                                   \
        if (! (cond)) peqtest::reportFailure (__FILE__, __LINE__, #cond);          \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                          \
    do {                                                                           \
        ++peqtest::checkCount();                                                   \
        const double a_ = (double) (actual), e_ = (double) (expected), t_ = (double) (tol); \
        if (! (std::abs (a_ - e_) <= t_)) {                                        \
            char buf_[256];                                                        \
            std::snprintf (buf_, sizeof (buf_), "%s = %.6f, expected %.6f +/- %.6f", #actual, a_, e_, t_); \
            peqtest::reportFailure (__FILE__, __LINE__, buf_);                     \
        }                                                                          \
    } while (0)
