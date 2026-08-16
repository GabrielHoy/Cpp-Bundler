#include "app.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("greet interpolates the provided name", "[App_Test]") {
    REQUIRE(App_Test::Greet("Tampered") == "Hello, Tampered!");
}

TEST_CASE("greet falls back to World when given an empty name", "[App_Test]") {
    REQUIRE(App_Test::Greet("") == "Hello, World!");
}
