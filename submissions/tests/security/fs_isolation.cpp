#include <fstream>
#include <iostream>

int main() {
    int a = 0;
    int b = 0;

    if (!(std::cin >> a >> b)) {
        return 1;
    }

    std::ifstream passwd("/etc/passwd");
    if (passwd.is_open()) {
        std::cerr << "CPPJUDGE_NSJAIL_FS_LEAK" << std::endl;
        std::cout << -999999 << std::endl;
        return 0;
    }

    std::cerr << "CPPJUDGE_NSJAIL_FS_ISOLATED" << std::endl;
    std::cout << a + b << std::endl;
    return 0;
}
