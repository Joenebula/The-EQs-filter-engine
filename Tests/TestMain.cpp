#include "TestFramework.h"

#include <chrono>
#include <cstdio>

int main()
{
    int failedTests = 0;
    const auto t0 = std::chrono::steady_clock::now();

    for (const auto& t : peqtest::registry())
    {
        const int before = peqtest::failureCount();
        std::printf ("[ RUN  ] %s\n", t.name.c_str());
        t.fn();
        const bool ok = peqtest::failureCount() == before;
        std::printf ("[ %s ] %s\n", ok ? " OK " : "FAIL", t.name.c_str());
        if (! ok)
            ++failedTests;
    }

    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("\n%zu tests, %d checks, %d failed tests, %d failed checks (%.1f s)\n",
                 peqtest::registry().size(), peqtest::checkCount(), failedTests,
                 peqtest::failureCount(), secs);
    return failedTests == 0 ? 0 : 1;
}
