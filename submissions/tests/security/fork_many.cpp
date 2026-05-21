#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <vector>

int main() {
    int a = 0;
    int b = 0;
    std::cin >> a >> b;
    std::cout << a + b << '\n';

    std::vector<pid_t> children;

    for (int i = 0; i < 32; ++i) {
        pid_t pid = fork();

        if (pid == 0) {
            while (true) {
                sleep(1);
            }
        }

        if (pid < 0) {
            break;
        }

        children.push_back(pid);
    }

    while (true) {
        sleep(1);
    }
}
