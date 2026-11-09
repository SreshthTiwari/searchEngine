#include <cstdio>
#include <exception>
#include <string>

#include "test_util.h"

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    int failed_cases = 0;
    for (const auto& test : testing::registry()) {
        if (!filter.empty() && std::string(test.name).find(filter) == std::string::npos) continue;
        const int before = testing::failure_count();
        std::printf("[ RUN  ] %s\n", test.name);
        try {
            test.fn();
        } catch (const std::exception& e) {
            testing::report(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        } catch (...) {
            testing::report(__FILE__, __LINE__, "unexpected unknown exception");
        }
        ++ran;
        if (testing::failure_count() != before) {
            ++failed_cases;
            std::printf("[ FAIL ] %s\n", test.name);
        } else {
            std::printf("[  OK  ] %s\n", test.name);
        }
    }
    std::printf("%d test(s) run, %d failed\n", ran, failed_cases);
    return failed_cases == 0 ? 0 : 1;
}
