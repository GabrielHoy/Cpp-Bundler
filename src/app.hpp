#pragma once

#include <string>
#include <string_view>

namespace App_Test {

    /// Builds a greeting for @p name, falling back to "World" when it is empty.
    [[nodiscard]]
    std::string Greet(std::string_view name);

} // namespace App_Test
