int main() {
    volatile unsigned long long x = 0;
    while (true) {
        ++x;
    }
    return 0;
}
