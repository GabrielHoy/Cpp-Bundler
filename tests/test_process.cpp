#include "error.hpp"
#include "test_util.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

using Test_Util::CanonicalGeneric;
using Test_Util::RunBundler;
using Test_Util::TempTree;

// === Resolving ===

TEST_CASE("includes resolve against the search directories", "[process][resolve]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include \"a.hpp\"\n// hello?\n#include <b/c.hpp>\n");
    tree.Write("d1/a.hpp", "// a.hpp\n");
    tree.Write("d2/b/c.hpp", "// b/c.hpp\n");

    const std::string output = RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d1"), "--dir", tree.Path("d2")});

    REQUIRE(output == "// a.hpp\n// hello?\n// b/c.hpp\n");
}

TEST_CASE("quote-only and system-only search directories apply to their own kind", "[process][resolve]") {
    TempTree tree;
    tree.Write(
        "src/main.cpp",
        "#include \"a.hpp\"\n"
        "#include \"b.hpp\"\n"
        "#include \"c.hpp\"\n"
        "#include <a.hpp>\n"
        "#include <b.hpp>\n"
        "#include <c.hpp>\n"
    );
    tree.Write("d1/a.hpp", "// a.hpp quote\n");
    tree.Write("d2/b.hpp", "// b.hpp system\n");
    tree.Write("d3/a.hpp", "// a.hpp shared\n");
    tree.Write("d3/b.hpp", "// b.hpp shared\n");
    tree.Write("d3/c.hpp", "// c.hpp shared\n");

    const std::string output = RunBundler(
        {tree.Path("src/main.cpp"),
         "--dir-quote",
         tree.Path("d1"),
         "--dir-system",
         tree.Path("d2"),
         "-d",
         tree.Path("d3")}
    );

    // The last line is absent because <c.hpp> resolves to the same file "c.hpp" already
    // pulled in, and a header is emitted at most once.
    REQUIRE(
        output
        == "// a.hpp quote\n"
           "// b.hpp shared\n"
           "// c.hpp shared\n"
           "// a.hpp shared\n"
           "// b.hpp system\n"
    );
}

TEST_CASE("search directories are tried in the order given on the command line", "[process][resolve]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>");
    tree.Write("first/a.hpp", "// 1");
    tree.Write("second/a.hpp", "// 2");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("first"), "-d", tree.Path("second")}) == "// 1");
    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("second"), "-d", tree.Path("first")}) == "// 2");
}

TEST_CASE("the merged directory order follows the command line, not the option name", "[process][resolve]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>");
    tree.Write("shared/a.hpp", "// shared");
    tree.Write("system/a.hpp", "// system");

    // This is the whole reason --dir and --dir-system are merged by position: swapping the
    // two options on the command line has to swap which directory wins.
    REQUIRE(
        RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("shared"), "--dir-system", tree.Path("system")})
        == "// shared"
    );
    REQUIRE(
        RunBundler({tree.Path("src/main.cpp"), "--dir-system", tree.Path("system"), "-d", tree.Path("shared")})
        == "// system"
    );
}

TEST_CASE("a directory is never a valid resolution", "[process][resolve]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a>");
    tree.MakeDir("d/a");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "#include <a>");
}

TEST_CASE("a quote include resolves against its own file's directory first", "[process][resolve]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include \"a.hpp\"\n");
    tree.Write("src/a.hpp", "// next door\n");
    tree.Write("d/a.hpp", "// search dir\n");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "// next door\n");
    // A system include has no such notion, so it lands in the search directory.
    tree.Write("src/main.cpp", "#include <a.hpp>\n");
    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "// search dir\n");
}

TEST_CASE("unresolvable includes follow the policy they were given", "[process][resolve]") {
    TempTree tree;

    // Plain loop rather than SECTIONs: Catch2 re-runs the whole test case once per section,
    // which would re-drive the loop and obscure which delimiter actually failed.
    for (const std::string_view delimited : {std::string_view{"<a.hpp>"}, std::string_view{"\"a.hpp\""}}) {
        const std::string source = "#include " + std::string{delimited};
        tree.Write("src/main.cpp", source);
        INFO("include reference: " << delimited);

        // warn and ignore differ only in what reaches stderr; both leave the line alone.
        REQUIRE(RunBundler({tree.Path("src/main.cpp"), "--unresolvable-include", "ignore"}) == source);
        REQUIRE(RunBundler({tree.Path("src/main.cpp"), "--unresolvable-include", "warn"}) == source);
        REQUIRE(RunBundler({tree.Path("src/main.cpp")}) == source); // ignoring is the default
        REQUIRE_THROWS_AS(
            RunBundler({tree.Path("src/main.cpp"), "--unresolvable-include", "error"}),
            Cpp_Bundler::Error
        );
    }
}

TEST_CASE("the quote and system unresolvable policies are independent", "[process][resolve]") {
    TempTree tree;
    tree.Write("src/quote.cpp", "#include \"a.hpp\"");
    tree.Write("src/system.cpp", "#include <a.hpp>");

    REQUIRE_THROWS_AS(
        RunBundler({tree.Path("src/quote.cpp"), "--unresolvable-quote-include", "error"}),
        Cpp_Bundler::Error
    );
    REQUIRE_NOTHROW(RunBundler({tree.Path("src/system.cpp"), "--unresolvable-quote-include", "error"}));

    REQUIRE_THROWS_AS(
        RunBundler({tree.Path("src/system.cpp"), "--unresolvable-system-include", "error"}),
        Cpp_Bundler::Error
    );
    REQUIRE_NOTHROW(RunBundler({tree.Path("src/quote.cpp"), "--unresolvable-system-include", "error"}));
}

// === Inlining ===

TEST_CASE("a header is inlined at most once", "[process][inline]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>\n#include <b.hpp>\n");
    tree.Write("d/a.hpp", "#include <b.hpp>");
    tree.Write("d/b.hpp", "arst\n");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "arst\n");
}

TEST_CASE("several source files share one set of already-included headers", "[process][inline]") {
    TempTree tree;
    tree.Write("src/a.cpp", "a");
    tree.Write("src/b.cpp", "b");
    tree.Write("src/c.cpp", "c");

    REQUIRE(RunBundler({tree.Path("src/a.cpp"), tree.Path("src/b.cpp"), tree.Path("src/c.cpp")}) == "abc");
}

TEST_CASE("a source file already pulled in as a header is not repeated", "[process][inline]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>");
    tree.Write("d/a.hpp", "arst");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), tree.Path("d/a.hpp"), "-d", tree.Path("d")}) == "arst");
}

TEST_CASE("pragma once is stripped", "[process][inline]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>\n#include <b.hpp>\n");
    tree.Write("d/a.hpp", "#pragma once\n");
    tree.Write("d/b.hpp", "# \tpragma\t  once  \t\n");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}).empty());
}

TEST_CASE("unusually spaced include statements still resolve", "[process][inline]") {
    TempTree tree;
    tree.Write("src/main.cpp", "# \t include \t <a.hpp> \t ");
    tree.Write("d/a.hpp", "arst");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "arst");
}

TEST_CASE("an include with mismatched delimiters is copied through untouched", "[process][inline]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp\"\n");
    tree.Write("d/a.hpp", "arst");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "#include <a.hpp\"\n");
}

TEST_CASE("cyclic includes follow the policy they were given", "[process][inline]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>");
    tree.Write("d/a.hpp", "#include <b.hpp>");
    tree.Write("d/b.hpp", "#include <a.hpp>");

    const std::string source = tree.Path("src/main.cpp");
    const std::string dir    = tree.Path("d");

    SECTION("erroring is the default") {
        REQUIRE_THROWS_AS(RunBundler({source, "-d", dir}), Cpp_Bundler::Error);
    }
    SECTION("error") {
        REQUIRE_THROWS_AS(RunBundler({source, "-d", dir, "--cyclic-include", "error"}), Cpp_Bundler::Error);
    }
    SECTION("warn leaves the offending include in place") {
        REQUIRE(RunBundler({source, "-d", dir, "--cyclic-include", "warn"}) == "#include <a.hpp>");
    }
    SECTION("ignore leaves the offending include in place") {
        REQUIRE(RunBundler({source, "-d", dir, "--cyclic-include", "ignore"}) == "#include <a.hpp>");
    }
}

TEST_CASE("a file including itself is a cycle", "[process][inline]") {
    TempTree tree;
    tree.Write("d/a.hpp", "#include <a.hpp>");

    REQUIRE_THROWS_AS(RunBundler({tree.Path("d/a.hpp"), "-d", tree.Path("d")}), Cpp_Bundler::Error);
}

TEST_CASE("the cycle report names every file on the loop", "[process][inline]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>");
    tree.Write("d/a.hpp", "#include <b.hpp>");
    tree.Write("d/b.hpp", "#include <a.hpp>");

    try {
        (void)RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")});
        FAIL("expected a cyclic include error");
    } catch (const Cpp_Bundler::Error& error) {
        const std::string message = error.what();
        REQUIRE(message.find("Cyclic include detected") != std::string::npos);
        REQUIRE(message.find("a.hpp") != std::string::npos);
        REQUIRE(message.find("b.hpp") != std::string::npos);
    }
}

// === Line directives ===

TEST_CASE("line directives point back at the original files", "[process][lines]") {
    TempTree   tree;
    const auto source = tree.Write(
        "src/main.cpp",
        "arst\n"
        "#include <a.hpp>\n"
        "\n"
        "arst\n"
        "#include <b.hpp>\n"
        "arst\n"
    );
    tree.Write("d/a.hpp", "#include <b.hpp>\n");
    const auto header = tree.Write("d/b.hpp", "qwfp\n");

    const std::string output = RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d"), "--line-directives"});

    const std::string expected = "#line 1 \"" + CanonicalGeneric(source)
                                 + "\"\n"
                                   "arst\n"
                                   "#line 1 \""
                                 + CanonicalGeneric(header)
                                 + "\"\n"
                                   "qwfp\n"
                                   "#line 3 \""
                                 + CanonicalGeneric(source)
                                 + "\"\n"
                                   "\n"
                                   "arst\n"
                                   "#line 6 \""
                                 + CanonicalGeneric(source)
                                 + "\"\n"
                                   "arst\n";
    REQUIRE(output == expected);
}

TEST_CASE("no line directives are emitted unless asked for", "[process][lines]") {
    TempTree tree;
    tree.Write("src/main.cpp", "arst\n#include <a.hpp>\narst\n");
    tree.Write("d/a.hpp", "qwfp\n");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "arst\nqwfp\narst\n");
}

// === Filtering ===

TEST_CASE("a filter glob keeps matching headers out", "[process][filter]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>\n#include <b.hpp>\n");
    tree.Write("d/a.hpp", "// a.hpp\n");
    tree.Write("d/b.hpp", "// b.hpp\n");

    REQUIRE(
        RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d"), "--filter", "**/b.hpp"})
        == "// a.hpp\n#include <b.hpp>\n"
    );
}

TEST_CASE("a negated glob puts headers back in", "[process][filter]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>\n#include <b.hpp>\n");
    tree.Write("d/a.hpp", "// a.hpp\n");
    tree.Write("d/b.hpp", "// b.hpp\n");

    REQUIRE(
        RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d"), "-f", "**", "-f", "!**/b.hpp"})
        == "#include <a.hpp>\n// b.hpp\n"
    );
}

TEST_CASE("the last matching glob decides", "[process][filter]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a/b/c.hpp>");
    tree.Write("d/a/b/c.hpp", "arst");

    // All three globs match; only the last one counts, so the header stays out.
    REQUIRE(
        RunBundler(
            {tree.Path("src/main.cpp"), "-d", tree.Path("d"), "-f", "**/a/**", "-f", "!**/a/b/**", "-f", "**/a/b/c.hpp"}
        )
        == "#include <a/b/c.hpp>"
    );

    // Reordering the same three globs flips the outcome.
    REQUIRE(
        RunBundler(
            {tree.Path("src/main.cpp"), "-d", tree.Path("d"), "-f", "**/a/**", "-f", "**/a/b/c.hpp", "-f", "!**/a/b/**"}
        )
        == "arst"
    );
}

TEST_CASE("quote-only and system-only filters apply to their own kind", "[process][filter]") {
    TempTree tree;
    tree.Write(
        "src/main.cpp",
        "#include <a.hpp>\n"
        "#include <b.hpp>\n"
        "#include \"a.hpp\"\n"
        "#include \"b.hpp\"\n"
    );
    tree.Write("d/a.hpp", "// a.hpp\n");
    tree.Write("d/b.hpp", "// b.hpp\n");

    const std::string output = RunBundler(
        {tree.Path("src/main.cpp"),
         "-d",
         tree.Path("d"),
         "-f",
         "**",
         "--filter-quote",
         "!**/b.hpp",
         "--filter-system",
         "!**/a.hpp"}
    );

    REQUIRE(
        output
        == "// a.hpp\n"
           "#include <b.hpp>\n"
           "#include \"a.hpp\"\n"
           "// b.hpp\n"
    );
}

// === Documented limitations ===
//
// These pin behaviour the README calls out as a limitation. They are not aspirational --
// changing any of them means the documentation is now wrong, which is exactly what a
// failure here should prompt.

TEST_CASE("an include inside a disabled #if block is still inlined", "[process][limitation]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#if 0\n#include <a.hpp>\n#endif\n#include <a.hpp>\n");
    tree.Write("d/a.hpp", "int answer = 42;\n");

    // The preprocessor is never run, so the dead include wins the race to claim the header
    // and the live one is dropped as "already included". The result does not compile, which
    // is the good news: the failure is loud rather than silent.
    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "#if 0\nint answer = 42;\n#endif\n");
}

TEST_CASE("a header is inlined wherever the directive sits", "[process][limitation]") {
    TempTree tree;
    tree.Write("src/a.cpp", "namespace Outer {\n#include <h.hpp>\n}\n");
    tree.Write("src/b.cpp", "#include <h.hpp>\nvoid Use() { Helper(); }\n");
    tree.Write("d/h.hpp", "void Helper();\n");

    // Expanded inside Outer for the first user; the second user's include is then dropped,
    // so it sees no declaration at all.
    REQUIRE(
        RunBundler({tree.Path("src/a.cpp"), tree.Path("src/b.cpp"), "-d", tree.Path("d")})
        == "namespace Outer {\nvoid Helper();\n}\nvoid Use() { Helper(); }\n"
    );
}

TEST_CASE("source files are concatenated in the order given", "[process][limitation]") {
    TempTree tree;
    tree.Write("src/a.cpp", "// a\n");
    tree.Write("src/b.cpp", "// b\n");

    // Concatenation is all that happens -- no reordering, no dependency analysis. This is a
    // unity build, with every unity build's caveats about macros and internal linkage.
    REQUIRE(RunBundler({tree.Path("src/a.cpp"), tree.Path("src/b.cpp")}) == "// a\n// b\n");
    REQUIRE(RunBundler({tree.Path("src/b.cpp"), tree.Path("src/a.cpp")}) == "// b\n// a\n");
}

// === Byte fidelity ===

TEST_CASE("CRLF line endings survive the round trip", "[process][bytes]") {
    TempTree tree;
    tree.Write("src/main.cpp", "one\r\n#include <a.hpp>\r\ntwo\r\n");
    tree.Write("d/a.hpp", "mid\r\n");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "one\r\nmid\r\ntwo\r\n");
}

TEST_CASE("a file with no final newline does not gain one", "[process][bytes]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>\ntail");
    tree.Write("d/a.hpp", "head");

    // The header's missing newline is preserved too, which is exactly why concatenating
    // sources can glue two lines together -- faithful, if occasionally surprising.
    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "headtail");
}

TEST_CASE("an empty header contributes nothing", "[process][bytes]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>\nrest\n");
    tree.Write("d/a.hpp", "");

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "rest\n");
}

// === Symlinks ===

TEST_CASE("a symlinked header resolves to its target", "[process][symlink]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <b.hpp>");
    const auto target = tree.Write("d/a.hpp", "// a.hpp");

    if (!tree.TrySymlink("d/b.hpp", target)) {
        SKIP("this platform will not let the test create a symlink");
    }

    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "// a.hpp");
}

TEST_CASE("file identity looks through symlinks", "[process][symlink]") {
    TempTree tree;
    tree.Write("src/main.cpp", "#include <a.hpp>\n#include <b.hpp>\n");
    const auto target = tree.Write("d/a.hpp", "arst\n");

    if (!tree.TrySymlink("d/b.hpp", target)) {
        SKIP("this platform will not let the test create a symlink");
    }

    // Two names, one file: canonicalising before de-duplicating is what catches this.
    REQUIRE(RunBundler({tree.Path("src/main.cpp"), "-d", tree.Path("d")}) == "arst\n");
}
