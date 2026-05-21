#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <vector>

int main() {
    int a = 0;
    int b = 0;

    if (!(std::cin >> a >> b)) {
        return 2;
    }

    std::cout << a + b << '\n';

    std::vector<int> fds;
    while (true) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) {
            return 1;
        }
        fds.push_back(fd);
    }
}
