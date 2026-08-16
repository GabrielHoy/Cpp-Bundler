#include "scan.hpp"
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string_view>

using Cpp_Bundler::IsPragmaOnce;
using Cpp_Bundler::ScanIncludeDirective;

TEST_CASE("include directives in their ordinary spellings", "[scan]") {
    REQUIRE(ScanIncludeDirective("#include <a.hpp>\n") == "<a.hpp>");
    REQUIRE(ScanIncludeDirective("#include \"a.hpp\"\n") == "\"a.hpp\"");
    REQUIRE(ScanIncludeDirective("#include<a.hpp>") == "<a.hpp>");
    REQUIRE(ScanIncludeDirective("#include <b/c.hpp>") == "<b/c.hpp>");
}

TEST_CASE("whitespace is permitted wherever the preprocessor permits it", "[scan]") {
    REQUIRE(ScanIncludeDirective("   #  include   <a.hpp>   \n") == "<a.hpp>");
    REQUIRE(ScanIncludeDirective("# \t include \t <a.hpp> \t ") == "<a.hpp>");
    // A CRLF file must scan the same as an LF one.
    REQUIRE(ScanIncludeDirective("#include <a.hpp>\r\n") == "<a.hpp>");
}

TEST_CASE("non-includes are left alone", "[scan]") {
    REQUIRE_FALSE(ScanIncludeDirective("int main() {}\n").has_value());
    REQUIRE_FALSE(ScanIncludeDirective("// #include <a.hpp>\n").has_value());
    REQUIRE_FALSE(ScanIncludeDirective("#includex <a.hpp>\n").has_value());
    REQUIRE_FALSE(ScanIncludeDirective("#include\n").has_value());
    REQUIRE_FALSE(ScanIncludeDirective("#include <>\n").has_value());
    REQUIRE_FALSE(ScanIncludeDirective("#include <a.hpp\n").has_value());
    // Anything after the closing delimiter disqualifies the line.
    REQUIRE_FALSE(ScanIncludeDirective("#include <a.hpp> // why\n").has_value());
}

TEST_CASE("mismatched delimiters are reported, not swallowed", "[scan]") {
    // The scanner is deliberately permissive here so the caller can decide; the processor
    // leaves such a line exactly as written rather than guessing what was meant.
    REQUIRE(ScanIncludeDirective("#include <a.hpp\"\n") == "<a.hpp\"");
    REQUIRE(ScanIncludeDirective("#include \"a.hpp>\n") == "\"a.hpp>");
}

TEST_CASE("pragma once in its whitespace variants", "[scan]") {
    REQUIRE(IsPragmaOnce("#pragma once\n"));
    REQUIRE(IsPragmaOnce("# \tpragma\t  once  \t\n"));
    REQUIRE(IsPragmaOnce("   #pragma once"));
    REQUIRE(IsPragmaOnce("#pragma once\r\n"));
}

TEST_CASE("things that only look like pragma once", "[scan]") {
    // `once` is a bare word, so unlike the delimiter after `include` it needs a separator.
    REQUIRE_FALSE(IsPragmaOnce("#pragmaonce\n"));
    REQUIRE_FALSE(IsPragmaOnce("#pragma once twice\n"));
    REQUIRE_FALSE(IsPragmaOnce("#pragma pack(1)\n"));
    REQUIRE_FALSE(IsPragmaOnce("// #pragma once\n"));
}
