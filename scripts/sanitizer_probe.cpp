// A runtime probe, independent of BuildQueue, for diagnosing host sanitizer support.
#include <atomic>
#include <iostream>
#include <thread>
int main() {
    std::atomic<int> counter{0};
    std::thread worker([&] { ++counter; });
    worker.join();
    std::cout << "Sanitizer runtime probe: " << counter << "\n";
}
