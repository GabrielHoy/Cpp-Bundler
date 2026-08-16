#pragma once

#include "filter.hpp"
#include "process.hpp"
#include <filesystem>
#include <optional>
#include <spdlog/common.h>
#include <string>
#include <vector>

namespace Cpp_Bundler {

    /// Everything the command line hands to the rest of the program.
    ///
    /// The `--dir` family and the `--filter` family arrive here already merged in the order
    /// they appeared on the command line. That order is not cosmetic: it decides which
    /// search directory wins a lookup, and which filter glob has the final say over a
    /// header. Keeping the merge in the parser means nothing downstream has to care that
    /// the options were ever spelled three different ways.
    struct Options {
        std::vector<std::filesystem::path>   sourceFiles;
        std::optional<std::filesystem::path> outputFile;
        std::vector<std::filesystem::path>   quoteSearchDirs;
        std::vector<std::filesystem::path>   systemSearchDirs;
        std::vector<InvertibleGlob>          quoteFilters;
        std::vector<InvertibleGlob>          systemFilters;
        ErrorHandlingOptions                 errorHandling;
        bool                                 lineDirectives{false};
        spdlog::level::level_enum            logLevel{spdlog::level::warn};
    };

    /// Parses the command line, `arguments[0]` being the program name.
    ///
    /// Returns nullopt when the invocation was already fully served (`--help`,
    /// `--version`), in which case the caller should exit successfully.
    ///
    /// @throws UsageError when the command line is wrong, Error when a value in it is.
    [[nodiscard]]
    std::optional<Options> ParseCommandLine(const std::vector<std::string>& arguments);

} // namespace Cpp_Bundler
