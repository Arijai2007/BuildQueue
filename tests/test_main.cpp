#include "Test.hpp"
namespace test {
std::vector<Case> &cases() {
    static std::vector<Case> c;
    return c;
}
} // namespace test
int main() {
    std::cout << std::unitbuf;
    int passed = 0;
    for (const auto &c : test::cases()) {
        try {
            c.run();
            ++passed;
            std::cout << "PASS " << c.name << "\n";
        } catch (const std::exception &e) {
            std::cout << "FAIL " << c.name << ": " << e.what() << "\n";
        }
    }
    std::cout << passed << "/" << test::cases().size() << " tests passed\n";
    return passed == static_cast<int>(test::cases().size()) ? 0 : 1;
}
