#pragma once

// Minimal self-contained unit test framework (no external dependency).

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace cosmic::test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure {
    std::string message;
};

inline int& checks() {
    static int c = 0;
    return c;
}

}  // namespace cosmic::test

#define COSMIC_CAT2(a, b) a##b
#define COSMIC_CAT(a, b) COSMIC_CAT2(a, b)
#define TEST_CASE(name)                                                                              \
    static void COSMIC_CAT(test_fn_, __LINE__)();                                                    \
    static ::cosmic::test::Registrar COSMIC_CAT(test_reg_, __LINE__)(name, &COSMIC_CAT(test_fn_, __LINE__)); \
    static void COSMIC_CAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        ++::cosmic::test::checks();                                                          \
        if (!(cond)) {                                                                       \
            std::ostringstream os_;                                                          \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed";               \
            throw ::cosmic::test::Failure{os_.str()};                                        \
        }                                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                \
    do {                                                                                     \
        ++::cosmic::test::checks();                                                          \
        double va_ = (a), vb_ = (b);                                                         \
        if (std::abs(va_ - vb_) > (eps)) {                                                   \
            std::ostringstream os_;                                                          \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK_NEAR(" #a ", " #b ") " << va_ << " vs " << vb_; \
            throw ::cosmic::test::Failure{os_.str()};                                        \
        }                                                                                    \
    } while (0)
