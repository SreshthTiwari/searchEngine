#pragma once

#include <cmath>
#include <cstdio>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& failure_count() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

template <class T>
std::string show(const T& value) {
    if constexpr (requires(std::ostream& os, const T& v) { os << v; }) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else {
        return "<unprintable>";
    }
}

inline void report(const char* file, int line, const std::string& message) {
    std::fprintf(stderr, "  FAILED %s:%d: %s\n", file, line, message.c_str());
    ++failure_count();
}

}

#define TEST(name)                                                   \
    static void test_##name();                                       \
    static ::testing::Registrar registrar_##name(#name, &test_##name); \
    static void test_##name()

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) ::testing::report(__FILE__, __LINE__, #cond);   \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        const auto& check_va = (a);                                              \
        const auto& check_vb = (b);                                              \
        if (!(check_va == check_vb)) {                                           \
            ::testing::report(__FILE__, __LINE__,                                \
                              std::string(#a " == " #b " (") +                   \
                                  ::testing::show(check_va) + " vs " +           \
                                  ::testing::show(check_vb) + ")");              \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                    \
    do {                                                                         \
        const double check_va = (a);                                             \
        const double check_vb = (b);                                             \
        if (!(std::fabs(check_va - check_vb) <= (eps))) {                        \
            ::testing::report(__FILE__, __LINE__,                                \
                              std::string(#a " ~= " #b " (") +                   \
                                  std::to_string(check_va) + " vs " +            \
                                  std::to_string(check_vb) + ")");               \
        }                                                                        \
    } while (0)

#define CHECK_THROWS(stmt, type)                                                 \
    do {                                                                         \
        bool check_thrown = false;                                               \
        try {                                                                    \
            stmt;                                                                \
        } catch (const type&) {                                                  \
            check_thrown = true;                                                 \
        } catch (...) {                                                          \
        }                                                                        \
        if (!check_thrown) {                                                     \
            ::testing::report(__FILE__, __LINE__, "expected " #type " from " #stmt); \
        }                                                                        \
    } while (0)
