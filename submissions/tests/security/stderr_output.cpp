#include <iostream>

int main() {
    int a = 0;
    int b = 0;

    if (!(std::cin >> a >> b)) {
        return 1;
    }

    std::cerr << "CPPJUDGE_SECURITY_STDERR_MARKER" << std::endl;
    std::cout << a + b << std::endl;

    return 0;
}
