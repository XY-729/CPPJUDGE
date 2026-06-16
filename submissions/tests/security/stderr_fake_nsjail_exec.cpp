#include <iostream>

int main() {
    std::cerr << "Failed to execute nsjail" << std::endl;
    std::cerr << "some other debug output" << std::endl;
    int a, b;
    std::cin >> a >> b;
    std::cout << a + b << std::endl;
    return 0;
}
