#include <cstring>
#include <unistd.h>
#include <vector>

int main() {
    std::vector<char*> blocks;
    while (true) {
        char* block = new char[1024 * 1024];
        std::memset(block, 1, 1024 * 1024);
        blocks.push_back(block);
        usleep(1000);
    }
    return 0;
}
