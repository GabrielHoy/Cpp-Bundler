#include "error.hpp"
#include "glob.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string_view>

using Cpp_Bundler::Glob;

namespace {

    [[nodiscard]]
    bool Matches(std::string_view pattern, std::string_view path) {
        return Glob::Compile(pattern).Matches(path);
    }

} // namespace

TEST_CASE("literals match the whole path and nothing else", "[glob]") {
    REQUIRE(Matches("a/b.hpp", "a/b.hpp"));
    REQUIRE_FALSE(Matches("a/b.hpp", "a/b.hpp.bak"));
    REQUIRE_FALSE(Matches("a/b.hpp", "x/a/b.hpp"));
    REQUIRE_FALSE(Matches("a/b.hpp", "a/b"));
}

TEST_CASE("'*' stays inside one path component", "[glob]") {
    REQUIRE(Matches("*.hpp", "a.hpp"));
    REQUIRE(Matches("*.hpp", ".hpp"));
    REQUIRE_FALSE(Matches("*.hpp", "x/a.hpp"));

    REQUIRE(Matches("a/*.hpp", "a/b.hpp"));
    REQUIRE_FALSE(Matches("a/*.hpp", "a/b/c.hpp"));
}

TEST_CASE("'?' is exactly one non-separator character", "[glob]") {
    REQUIRE(Matches("a?.hpp", "ab.hpp"));
    REQUIRE_FALSE(Matches("a?.hpp", "a.hpp"));
    REQUIRE_FALSE(Matches("a?.hpp", "abc.hpp"));
    REQUIRE_FALSE(Matches("a?b", "a/b"));
}

TEST_CASE("a lone '**' matches every path", "[glob]") {
    // The special case the --filter '**' idiom depends on: exclude everything, then use
    // '!' globs to allow specific headers back in.
    REQUIRE(Matches("**", "/usr/include/x.hpp"));
    REQUIRE(Matches("**", "a"));
    REQUIRE(Matches("**", "a/b/c"));
    REQUIRE(Matches("**/", "/usr/include/x.hpp"));
}

TEST_CASE("'**/' matches any number of leading components, including none", "[glob]") {
    REQUIRE(Matches("**/b.hpp", "b.hpp"));
    REQUIRE(Matches("**/b.hpp", "/b.hpp"));
    REQUIRE(Matches("**/b.hpp", "/a/b.hpp"));
    REQUIRE(Matches("**/b.hpp", "/x/y/z/b.hpp"));
    REQUIRE_FALSE(Matches("**/b.hpp", "/x/y/c.hpp"));
    REQUIRE_FALSE(Matches("**/b.hpp", "/x/y/b.hpp.bak"));
}

TEST_CASE("'/**' matches everything below a directory", "[glob]") {
    REQUIRE(Matches("/usr/**", "/usr/a.hpp"));
    REQUIRE(Matches("/usr/**", "/usr/a/b/c.hpp"));
    // The separator is part of the token, so the directory itself is not below itself.
    REQUIRE(Matches("/usr/**", "/usr/"));
    REQUIRE_FALSE(Matches("/usr/**", "/usr"));
    REQUIRE_FALSE(Matches("/usr/**", "/usrx/a.hpp"));
}

TEST_CASE("'/**/' spans zero or more interior components", "[glob]") {
    REQUIRE(Matches("a/**/b", "a/b"));
    REQUIRE(Matches("a/**/b", "a/x/b"));
    REQUIRE(Matches("a/**/b", "a/x/y/b"));
    REQUIRE_FALSE(Matches("a/**/b", "ab"));
    REQUIRE_FALSE(Matches("a/**/b", "a/bc"));

    // The shape the --filter tests lean on.
    REQUIRE(Matches("**/a/**", "/tmp/x/a/b/c.hpp"));
    REQUIRE_FALSE(Matches("**/a/**", "/tmp/x/ab/c.hpp"));
}

TEST_CASE("'**' that is not a whole component degrades to '*'", "[glob]") {
    REQUIRE(Matches("a**b", "axxb"));
    REQUIRE(Matches("a**b", "ab"));
    REQUIRE_FALSE(Matches("a**b", "a/x/b"));
}

TEST_CASE("adjacent recursive tokens collapse", "[glob]") {
    REQUIRE(Matches("**/**", "a/b/c"));
    REQUIRE(Matches("**/**", "a"));
}

TEST_CASE("character classes", "[glob]") {
    REQUIRE(Matches("[abc].hpp", "a.hpp"));
    REQUIRE(Matches("[abc].hpp", "c.hpp"));
    REQUIRE_FALSE(Matches("[abc].hpp", "d.hpp"));

    REQUIRE(Matches("[a-c]*.hpp", "b1.hpp"));
    REQUIRE_FALSE(Matches("[a-c]*.hpp", "d1.hpp"));

    REQUIRE(Matches("[!a].hpp", "b.hpp"));
    REQUIRE_FALSE(Matches("[!a].hpp", "a.hpp"));
    REQUIRE(Matches("[^a].hpp", "b.hpp"));

    // A class never swallows a separator, even a negated one.
    REQUIRE_FALSE(Matches("a[!x]b", "a/b"));
}

TEST_CASE("alternation", "[glob]") {
    REQUIRE(Matches("**/*.{hpp,hxx}", "x/a.hpp"));
    REQUIRE(Matches("**/*.{hpp,hxx}", "x/a.hxx"));
    REQUIRE_FALSE(Matches("**/*.{hpp,hxx}", "x/a.cpp"));

    REQUIRE(Matches("{a,{b,c}}.hpp", "c.hpp"));
    REQUIRE_FALSE(Matches("{a,{b,c}}.hpp", "d.hpp"));

    // An empty branch is legal and means "nothing here".
    REQUIRE(Matches("a{,b}c", "ac"));
    REQUIRE(Matches("a{,b}c", "abc"));
}

TEST_CASE("malformed patterns are rejected rather than silently misbehaving", "[glob]") {
    REQUIRE_THROWS_AS(Glob::Compile(""), Cpp_Bundler::Error);
    REQUIRE_THROWS_AS(Glob::Compile("[abc"), Cpp_Bundler::Error);
    REQUIRE_THROWS_AS(Glob::Compile("{a,b"), Cpp_Bundler::Error);
    REQUIRE_THROWS_AS(Glob::Compile("[z-a]"), Cpp_Bundler::Error);
}

TEST_CASE("the pattern is kept verbatim for diagnostics", "[glob]") {
    REQUIRE(Glob::Compile("**/*.hpp").Pattern() == "**/*.hpp");
}

#if defined(_WIN32)

TEST_CASE("matching follows the platform's view of case and separators", "[glob]") {
    // Windows compares file names case-insensitively, so a glob that did not would reject
    // paths naming the very file it was written for.
    REQUIRE(Matches("**/Foo.HPP", "c:/x/foo.hpp"));
    REQUIRE(Matches("[a-c].hpp", "B.hpp"));

    // A pattern written the way a Windows user would type it still matches the
    // forward-slash spelling the resolver produces.
    REQUIRE(Matches("**\\b.hpp", "c:/x/b.hpp"));
}

#else

TEST_CASE("matching is case-sensitive and '\\' escapes", "[glob]") {
    REQUIRE_FALSE(Matches("**/Foo.HPP", "/x/foo.hpp"));
    REQUIRE(Matches("a\\*b", "a*b"));
    REQUIRE_FALSE(Matches("a\\*b", "axb"));
}

#endif
