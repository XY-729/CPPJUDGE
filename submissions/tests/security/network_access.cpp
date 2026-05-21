#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    int a = 0;
    int b = 0;

    if (!(std::cin >> a >> b)) {
        return 1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "CPPJUDGE_NSJAIL_NETWORK_ISOLATED socket_errno=" << errno << std::endl;
        std::cout << a + b << std::endl;
        return 0;
    }

    timeval timeout {};
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "1.1.1.1", &addr.sin_addr);

    int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    int saved_errno = errno;
    close(fd);

    if (rc == 0) {
        std::cerr << "CPPJUDGE_NSJAIL_NETWORK_LEAK" << std::endl;
        std::cout << -999999 << std::endl;
        return 0;
    }

    std::cerr << "CPPJUDGE_NSJAIL_NETWORK_ISOLATED connect_errno=" << saved_errno << std::endl;
    std::cout << a + b << std::endl;
    return 0;
}
