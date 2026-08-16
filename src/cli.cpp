#include "cli.hpp"
#include "error.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include <algorithm>
#include <argparse/argparse.hpp>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string_view>
#include <utility>

#ifndef CPP_BUNDLER_NAME
    #define CPP_BUNDLER_NAME "cpp-bundler"
#endif
#ifndef CPP_BUNDLER_VERSION
    #define CPP_BUNDLER_VERSION "0.0.0"
#endif

namespace Cpp_Bundler {

    namespace {

        // Wrapped by hand: argparse lays out the option table itself but prints the
        // description and epilog exactly as given.
        constexpr std::string_view ABOUT =
            "Recursively combines C++ source files and the headers they include into a single output file.\n"
            "Each header is emitted at most once, as if it carried #pragma once, and which includes get\n"
            "inlined can be controlled precisely with the --filter family of options.\n"
            "\n"
            "No search directories are used by default: without at least one -d, nothing is inlined.";

        constexpr std::string_view EPILOG =
            "Filter globs match the full, canonical path of a header, so they usually need a leading '**/'.\n"
            "They are evaluated in the order given with the last match winning, and a glob prefixed with '!'\n"
            "puts matching headers back in. With no globs at all, everything resolvable is inlined.\n"
            "\n"
            "Only #include is interpreted -- #if header guards are invisible to this tool, so an include\n"
            "sitting inside two different #if blocks is expanded in the first and dropped in the second.";

        /// Options that consume a following value.
        ///
        /// Used only to split argv (see `SplitCommandLine`); the parser itself is
        /// the authority on what exists. `test_cli.cpp` walks every option listed here to
        /// make sure the two never drift apart.
        constexpr std::string_view VALUE_OPTIONS[]{
            "-o",
            "--output",
            "-d",
            "--dir",
            "--dir-quote",
            "--dir-system",
            "-f",
            "--filter",
            "--filter-quote",
            "--filter-system",
            "--unresolvable-include",
            "--unresolvable-quote-include",
            "--unresolvable-system-include",
            "--cyclic-include",
        };

        struct SplitArguments {
            /// The program name followed by the options, in the order they were written.
            std::vector<std::string> options;
            /// The source files, in the order they were written.
            std::vector<std::string> positionals;
        };

        /// Separates the options from the source files before argparse sees either.
        ///
        /// Two of argparse's limitations make this necessary. It consumes a positional
        /// exactly once, so `a.cpp -d dir b.cpp` would strand `b.cpp`; and it decides what
        /// is positional purely by the leading character, so a file named `-odd.cpp` is
        /// unreachable even after `--`. Taking the source files into our own hands fixes
        /// both, and preserves the relative order of the options themselves, which is what
        /// the --dir/--filter merging is built on.
        [[nodiscard]]
        SplitArguments SplitCommandLine(const std::vector<std::string>& arguments) {
            SplitArguments split;
            split.options.reserve(arguments.size());

            if (!arguments.empty()) {
                split.options.push_back(arguments.front()); // argparse expects the program name first
            }

            const auto takesValue = [](std::string_view token) {
                return std::ranges::find(VALUE_OPTIONS, token) != std::ranges::end(VALUE_OPTIONS);
            };

            bool optionsEnded = false;
            for (std::size_t i = 1; i < arguments.size(); ++i) {
                const std::string& token = arguments[i];

                if (optionsEnded) {
                    split.positionals.push_back(token);
                    continue;
                }
                if (token == "--") {
                    optionsEnded = true;
                    continue;
                }
                // A lone "-" is a filename by convention, not an option.
                if (token.size() < 2 || token.front() != '-') {
                    split.positionals.push_back(token);
                    continue;
                }

                split.options.push_back(token);
                // `--opt=value` arrives as a single token; every other form needs the next one.
                if (takesValue(token) && i + 1 < arguments.size()) {
                    split.options.push_back(arguments[++i]);
                }
            }

            return split;
        }

        [[nodiscard]]
        spdlog::level::level_enum LevelForVerbosity(int verbosity) noexcept {
            if (verbosity <= -2) {
                return spdlog::level::off;
            }
            switch (verbosity) {
                case -1:
                    return spdlog::level::err;
                case 0:
                    return spdlog::level::warn;
                case 1:
                    return spdlog::level::info;
                case 2:
                    return spdlog::level::debug;
                default:
                    return spdlog::level::trace;
            }
        }

        [[nodiscard]]
        ErrorHandling RequireErrorHandling(const std::string& value, std::string_view option) {
            if (const std::optional<ErrorHandling> parsed = ParseErrorHandling(value)) {
                return *parsed;
            }
            Fail("Invalid value \"{}\" for {}: expected one of error, warn, ignore", value, option);
        }

        [[nodiscard]]
        bool Contains(const std::vector<std::string>& arguments, std::string_view flag) {
            return std::ranges::find(arguments, flag) != arguments.end();
        }

    } // namespace

    std::optional<Options> ParseCommandLine(const std::vector<std::string>& arguments) {
        Options                      options;
        int                          verbose = 0;
        int                          quiet   = 0;
        std::optional<ErrorHandling> unresolvable;
        std::optional<ErrorHandling> unresolvableQuote;
        std::optional<ErrorHandling> unresolvableSystem;

        // Only --help is auto-generated: argparse's default --version also claims -v, which
        // this tool spends on verbosity.
        argparse::ArgumentParser program(
            CPP_BUNDLER_NAME,
            CPP_BUNDLER_VERSION,
            argparse::default_arguments::help,
            /*exit_on_default_arguments=*/false
        );
        program.add_description(std::string{ABOUT});
        program.add_epilog(std::string{EPILOG});
        program.set_usage_max_line_width(100);

        program.add_argument("--version").flag().help("Print version information and exit");

        // Registered for the help text only: the source files are collected by
        // SplitCommandLine and never reach argparse, so this consumes nothing. It has to
        // be declared as taking zero-or-more, because argparse enforces a positional's
        // minimum even when nothing was ever routed to it. The real "at least one" check
        // happens below, where it can produce a message worth reading.
        program.add_argument("files")
            .nargs(argparse::nargs_pattern::any)
            .metavar("source-files")
            .help("Source files to process (at least one)");

        program.add_argument("-o", "--output")
            .metavar("file")
            .help("Write the combined source to a file instead of standard output")
            .action([&options](const std::string& value) { options.outputFile = Paths::FromUtf8(value); });

        // The three --dir options append to the same two lists, so the merged order simply
        // falls out of the order argparse invokes these actions in.
        program.add_argument("-d", "--dir")
            .append()
            .metavar("dir")
            .help("Add a search directory for both quote and system includes")
            .action([&options](const std::string& value) {
            std::filesystem::path dir = Paths::FromUtf8(value);
            options.quoteSearchDirs.push_back(dir);
            options.systemSearchDirs.push_back(std::move(dir));
        });

        program.add_argument("--dir-quote")
            .append()
            .metavar("dir")
            .help("Add a search directory used only for quote includes")
            .action([&options](const std::string& value) {
            options.quoteSearchDirs.push_back(Paths::FromUtf8(value));
        });

        program.add_argument("--dir-system")
            .append()
            .metavar("dir")
            .help("Add a search directory used only for system includes")
            .action([&options](const std::string& value) {
            options.systemSearchDirs.push_back(Paths::FromUtf8(value));
        });

        program.add_argument("-f", "--filter")
            .append()
            .metavar("glob")
            .help("Exclude matching headers from being inlined; prefix with '!' to include them again")
            .action([&options](const std::string& value) {
            InvertibleGlob glob = InvertibleGlob::Parse(value);
            options.quoteFilters.push_back(glob);
            options.systemFilters.push_back(std::move(glob));
        });

        program.add_argument("--filter-quote")
            .append()
            .metavar("glob")
            .help("Like --filter, but only for quote includes")
            .action([&options](const std::string& value) {
            options.quoteFilters.push_back(InvertibleGlob::Parse(value));
        });

        program.add_argument("--filter-system")
            .append()
            .metavar("glob")
            .help("Like --filter, but only for system includes")
            .action([&options](const std::string& value) {
            options.systemFilters.push_back(InvertibleGlob::Parse(value));
        });

        program.add_argument("--unresolvable-include")
            .metavar("handling")
            .help("How to treat an include that cannot be resolved: error, warn, or ignore (default: ignore)")
            .action([&unresolvable](const std::string& value) {
            unresolvable = RequireErrorHandling(value, "--unresolvable-include");
        });

        program.add_argument("--unresolvable-quote-include")
            .metavar("handling")
            .help("Like --unresolvable-include, but only for quote includes")
            .action([&unresolvableQuote](const std::string& value) {
            unresolvableQuote = RequireErrorHandling(value, "--unresolvable-quote-include");
        });

        program.add_argument("--unresolvable-system-include")
            .metavar("handling")
            .help("Like --unresolvable-include, but only for system includes")
            .action([&unresolvableSystem](const std::string& value) {
            unresolvableSystem = RequireErrorHandling(value, "--unresolvable-system-include");
        });

        program.add_argument("--cyclic-include")
            .metavar("handling")
            .help("How to treat a cyclic include: error, warn, or ignore (default: error)")
            .action([&options](const std::string& value) {
            options.errorHandling.cyclicInclude = RequireErrorHandling(value, "--cyclic-include");
        });

        program.add_argument("--line-directives")
            .flag()
            .help("Emit #line directives so compilers and debuggers can map lines back to their original files")
            .action([&options](const std::string&) { options.lineDirectives = true; });

        program.add_argument("-v", "--verbose")
            .flag()
            .append()
            .help("Show more: -v adds info, -vv debug, -vvv trace (warnings and errors are always shown)")
            .action([&verbose](const std::string&) { ++verbose; });

        program.add_argument("-q", "--quiet")
            .flag()
            .append()
            .help("Show less: -q leaves only errors, -qq nothing at all")
            .action([&quiet](const std::string&) { ++quiet; });

        // Checked ahead of parsing, because a bare `--help` has no source files and the
        // parser would reject it before ever running the help action.
        if (Contains(arguments, "-h") || Contains(arguments, "--help")) {
            std::cout << program;
            return std::nullopt;
        }
        if (Contains(arguments, "--version")) {
            std::cout << CPP_BUNDLER_VERSION << '\n';
            return std::nullopt;
        }
        if (arguments.size() <= 1) {
            throw UsageError{program.help().str()};
        }

        const SplitArguments split = SplitCommandLine(arguments);
        try {
            program.parse_args(split.options);
        } catch (const Error&) {
            throw; // our own diagnostics are already phrased for the user
        } catch (const std::exception& error) {
            throw UsageError{fmt::format("{}\n\n{}\n", error.what(), program.usage())};
        }

        options.sourceFiles.reserve(split.positionals.size());
        for (const std::string& sourceFile : split.positionals) {
            options.sourceFiles.push_back(Paths::FromUtf8(sourceFile));
        }

        if (options.sourceFiles.empty()) {
            throw UsageError{fmt::format("No source files given.\n\n{}\n", program.usage())};
        }
        if (verbose > 0 && quiet > 0) {
            throw UsageError{"--verbose and --quiet cannot be combined."};
        }
        if (unresolvable.has_value() && (unresolvableQuote.has_value() || unresolvableSystem.has_value())) {
            throw UsageError{"--unresolvable-include cannot be combined with its --…-quote-include or "
                             "--…-system-include variants."};
        }

        // The shared option wins where it was given; otherwise the specific one applies,
        // and failing that includes that cannot be resolved are silently left alone.
        options.errorHandling.unresolvableQuoteInclude =
            unresolvable.value_or(unresolvableQuote.value_or(ErrorHandling::Ignore));
        options.errorHandling.unresolvableSystemInclude =
            unresolvable.value_or(unresolvableSystem.value_or(ErrorHandling::Ignore));
        options.logLevel = LevelForVerbosity(verbose - quiet);

        return options;
    }

} // namespace Cpp_Bundler
