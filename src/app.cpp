#include "app.hpp"
#include <string>
#include <string_view>

namespace App_Test {

    std::string Greet(std::string_view name) {
        // string_view is not null-terminated, so it cannot participate in operator+ directly.
        return "Hello, " + std::string{name.empty() ? std::string_view{"World"} : name} + "!";
    }

} // namespace App_Test
