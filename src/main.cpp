#include "app.hpp"
#include <iostream>
#include <string>

int main() {
    std::cout << "Name: " << std::flush;

    std::string name;
    std::getline(std::cin, name);

    std::cout << App_Test::Greet(name) << '\n';

    return 0;
}
