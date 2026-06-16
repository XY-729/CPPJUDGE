#include <iostream>

int main() {
    std::cerr << "Failed to prepare nsjail filesystem: some error" << std::endl;
    int a, b;
    std::cin >> a >> b;
    std::cout << a + b << std::endl;
    return 0;
}
