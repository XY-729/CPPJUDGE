#include <iostream>
#include <string>

int main() {
    std::string chunk(1024 * 1024, 'x');
    while (true) {
        std::cout << chunk << '\n';
    }
    return 0;
}
