#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "fmt/base.h"


// Minimal test checks: a failed check prints where it failed and the test goes on. A test's main
// returns Check::result().
namespace Check {
inline uint32_t failures = 0;

inline void report(bool ok, const char* file, int line, const char* what) {
    if (!ok) {
        failures++;
        fmt::println("{}:{}: CHECK failed: {}", file, line, what);
    }
}

inline void report_near(double a, double b, double tolerance, const char* file, int line,
        const char* what) {
    if (!(std::abs(a - b) <= tolerance)) {
        failures++;
        fmt::println("{}:{}: CHECK_NEAR failed: {} ({} vs {})", file, line, what, a, b);
    }
}

inline int result() {
    if (failures > 0) {
        fmt::println("{} check(s) failed", failures);
        return 1;
    }
    return 0;
}
}  // namespace Check

#define CHECK(cond) Check::report(static_cast<bool>(cond), __FILE__, __LINE__, #cond)
#define CHECK_NEAR(a, b, tolerance)                                                                \
    Check::report_near((a), (b), (tolerance), __FILE__, __LINE__, #a ", " #b ", " #tolerance)
// Checks that `expr` throws a std::runtime_error
#define CHECK_THROWS(expr)                                                                         \
    do {                                                                                           \
        bool thrown = false;                                                                       \
        try {                                                                                      \
            static_cast<void>(expr);                                                               \
        }                                                                                          \
        catch (const std::runtime_error&) {                                                        \
            thrown = true;                                                                         \
        }                                                                                          \
        Check::report(thrown, __FILE__, __LINE__, #expr " throws");                                \
    } while (0)
