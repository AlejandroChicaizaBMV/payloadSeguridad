#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "Hello World!" << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(5));

    return 0;
}