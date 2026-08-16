#include "cli.hpp"
#include "error.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using Cpp_Bundler::ErrorHandling;
using Cpp_Bundler::Options;

namespace {

    [[nodiscard]]
    Options Parse(std::vector<std::string> arguments) {
        arguments.insert(arguments.begin(), "cpp-bundler");
        std::optional<Options> options = Cpp_Bundler::ParseCommandLine(arguments);
        REQUIRE(options.has_value());
        return std::move(*options);
    }

    [[nodiscard]]
    std::vector<std::string> Names(const std::vector<std::filesystem::path>& paths) {
        std::vector<std::string> names;
        names.reserve(paths.size());
        for (const std::filesystem::path& path : paths) {
            names.push_back(path.generic_string());
        }
        return names;
    }

    [[nodiscard]]
    std::vector<std::string> Patterns(const std::vector<Cpp_Bundler::InvertibleGlob>& globs) {
        std::vector<std::string> patterns;
        patterns.reserve(globs.size());
        for (const Cpp_Bundler::InvertibleGlob& entry : globs) {
            patterns.push_back((entry.inverted ? "!" : "") + entry.glob.Pattern());
        }
        return patterns;
    }

} // namespace

TEST_CASE("source files are collected in order", "[cli]") {
    const Options options = Parse({"a.cpp", "b.cpp", "c.cpp"});
    REQUIRE(Names(options.sourceFiles) == std::vector<std::string>{"a.cpp", "b.cpp", "c.cpp"});
}

TEST_CASE("search directories merge in command-line order", "[cli]") {
    // --dir feeds both lists, the specific options only their own, and the position of
    // each option on the command line -- not its name -- decides the search order.
    const Options options = Parse({"--dir-quote", "q", "-d", "shared", "--dir-system", "s", "main.cpp"});

    REQUIRE(Names(options.quoteSearchDirs) == std::vector<std::string>{"q", "shared"});
    REQUIRE(Names(options.systemSearchDirs) == std::vector<std::string>{"shared", "s"});
}

TEST_CASE("reordering the directory options reorders the search", "[cli]") {
    const Options options = Parse({"-d", "shared", "--dir-quote", "q", "main.cpp"});
    REQUIRE(Names(options.quoteSearchDirs) == std::vector<std::string>{"shared", "q"});
}

TEST_CASE("filter globs merge in command-line order", "[cli]") {
    const Options options =
        Parse({"-f", "**", "--filter-quote", "!**/q.hpp", "--filter-system", "!**/s.hpp", "main.cpp"});

    REQUIRE(Patterns(options.quoteFilters) == std::vector<std::string>{"**", "!**/q.hpp"});
    REQUIRE(Patterns(options.systemFilters) == std::vector<std::string>{"**", "!**/s.hpp"});
}

TEST_CASE("verbosity is the balance of -v and -q", "[cli]") {
    REQUIRE(Parse({"main.cpp"}).logLevel == spdlog::level::warn);
    REQUIRE(Parse({"-v", "main.cpp"}).logLevel == spdlog::level::info);
    REQUIRE(Parse({"-vv", "main.cpp"}).logLevel == spdlog::level::debug);
    REQUIRE(Parse({"-vvv", "main.cpp"}).logLevel == spdlog::level::trace);
    REQUIRE(Parse({"-vvvv", "main.cpp"}).logLevel == spdlog::level::trace);
    REQUIRE(Parse({"-q", "main.cpp"}).logLevel == spdlog::level::err);
    REQUIRE(Parse({"-qq", "main.cpp"}).logLevel == spdlog::level::off);
    REQUIRE(Parse({"--verbose", "--verbose", "main.cpp"}).logLevel == spdlog::level::debug);
}

TEST_CASE("unresolvable-include policies default and fall back correctly", "[cli]") {
    const Options bare = Parse({"main.cpp"});
    REQUIRE(bare.errorHandling.unresolvableQuoteInclude == ErrorHandling::Ignore);
    REQUIRE(bare.errorHandling.unresolvableSystemInclude == ErrorHandling::Ignore);
    REQUIRE(bare.errorHandling.cyclicInclude == ErrorHandling::Error);

    const Options shared = Parse({"--unresolvable-include", "warn", "main.cpp"});
    REQUIRE(shared.errorHandling.unresolvableQuoteInclude == ErrorHandling::Warn);
    REQUIRE(shared.errorHandling.unresolvableSystemInclude == ErrorHandling::Warn);

    const Options specific = Parse({"--unresolvable-quote-include", "error", "main.cpp"});
    REQUIRE(specific.errorHandling.unresolvableQuoteInclude == ErrorHandling::Error);
    REQUIRE(specific.errorHandling.unresolvableSystemInclude == ErrorHandling::Ignore);

    REQUIRE(Parse({"--cyclic-include", "ignore", "main.cpp"}).errorHandling.cyclicInclude == ErrorHandling::Ignore);
}

TEST_CASE("mutually exclusive options are rejected", "[cli]") {
    REQUIRE_THROWS_AS(Parse({"-v", "-q", "main.cpp"}), Cpp_Bundler::UsageError);
    REQUIRE_THROWS_AS(
        Parse({"--unresolvable-include", "warn", "--unresolvable-quote-include", "error", "main.cpp"}),
        Cpp_Bundler::UsageError
    );
    REQUIRE_THROWS_AS(
        Parse({"--unresolvable-include", "warn", "--unresolvable-system-include", "error", "main.cpp"}),
        Cpp_Bundler::UsageError
    );

    // The two specific ones are independent, so using both together is fine.
    REQUIRE_NOTHROW(
        Parse({"--unresolvable-quote-include", "warn", "--unresolvable-system-include", "error", "main.cpp"})
    );
}

TEST_CASE("bad option values are rejected with a useful message", "[cli]") {
    REQUIRE_THROWS_AS(Parse({"--cyclic-include", "maybe", "main.cpp"}), Cpp_Bundler::Error);
    REQUIRE_THROWS_AS(Parse({"-f", "[unclosed", "main.cpp"}), Cpp_Bundler::Error);
}

TEST_CASE("an invocation without source files is a usage error", "[cli]") {
    REQUIRE_THROWS_AS(Parse({}), Cpp_Bundler::UsageError);
    REQUIRE_THROWS_AS(Parse({"-d", "somewhere"}), Cpp_Bundler::UsageError);
}

TEST_CASE("options and source files may be interleaved", "[cli]") {
    // argparse consumes a positional exactly once, so this only works because argv is
    // regrouped before parsing.
    const Options options = Parse({"a.cpp", "-d", "inc", "b.cpp", "--line-directives", "c.cpp"});

    REQUIRE(Names(options.sourceFiles) == std::vector<std::string>{"a.cpp", "b.cpp", "c.cpp"});
    REQUIRE(Names(options.quoteSearchDirs) == std::vector<std::string>{"inc"});
    REQUIRE(options.lineDirectives);
}

TEST_CASE("'--' forces the rest to be source files", "[cli]") {
    const Options options = Parse({"-d", "inc", "--", "-weird-name.cpp"});
    REQUIRE(Names(options.sourceFiles) == std::vector<std::string>{"-weird-name.cpp"});
}

TEST_CASE("the --option=value form is understood", "[cli]") {
    const Options options = Parse({"--dir=inc", "--output=out.cpp", "main.cpp"});
    REQUIRE(Names(options.quoteSearchDirs) == std::vector<std::string>{"inc"});
    REQUIRE(options.outputFile.has_value());
    REQUIRE(options.outputFile->generic_string() == "out.cpp");
}

TEST_CASE("--help and --version are served without running anything", "[cli]") {
    std::ostringstream    captured;
    std::streambuf* const original = std::cout.rdbuf(captured.rdbuf());

    const std::vector<std::string> help{"cpp-bundler", "--help"};
    const std::vector<std::string> version{"cpp-bundler", "--version"};
    const bool                     helpHandled    = !Cpp_Bundler::ParseCommandLine(help).has_value();
    const bool                     versionHandled = !Cpp_Bundler::ParseCommandLine(version).has_value();

    std::cout.rdbuf(original);

    // Neither carries source files, so being served rather than rejected is the point.
    REQUIRE(helpHandled);
    REQUIRE(versionHandled);
    REQUIRE(captured.str().find("source-files") != std::string::npos);
}

TEST_CASE("every value-taking option is known to the argv regrouping", "[cli]") {
    // Guards against drift between the parser's registrations and VALUE_OPTIONS in cli.cpp.
    // The trailing option is what makes an omission detectable: if the regrouping does not
    // know an option swallows the next token, that option ends up trying to consume
    // "--line-directives" and the parse fails outright.
    const std::vector<std::pair<std::string, std::string>> optionsWithValues{
        {"-o", "out.cpp"},
        {"--output", "out.cpp"},
        {"-d", "inc"},
        {"--dir", "inc"},
        {"--dir-quote", "inc"},
        {"--dir-system", "inc"},
        {"-f", "**"},
        {"--filter", "**"},
        {"--filter-quote", "**"},
        {"--filter-system", "**"},
        {"--unresolvable-include", "warn"},
        {"--unresolvable-quote-include", "warn"},
        {"--unresolvable-system-include", "warn"},
        {"--cyclic-include", "warn"},
    };

    for (const auto& [option, value] : optionsWithValues) {
        INFO("option: " << option);
        const Options parsed = Parse({option, value, "main.cpp", "--line-directives"});
        REQUIRE(Names(parsed.sourceFiles) == std::vector<std::string>{"main.cpp"});
        REQUIRE(parsed.lineDirectives);
    }
}
