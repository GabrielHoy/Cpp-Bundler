#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace Cpp_Bundler {

    /// Turns the text inside an `#include` into a path on disk.
    ///
    /// Everything it returns is canonical, links resolved. That is what lets the processor
    /// recognise two spellings of the same header -- or a symlink and its target -- as one
    /// file, and it is the path the `--filter` globs are matched against.
    class IncludeResolver {
      public:
        /// Search directories are canonicalised up front, so a mistyped `-d` is reported
        /// once instead of quietly failing to resolve anything for the rest of the run.
        ///
        /// @throws Error when a search directory cannot be canonicalised.
        IncludeResolver(
            const std::vector<std::filesystem::path>& quoteDirs,
            const std::vector<std::filesystem::path>& systemDirs
        );

        /// `currentDir` is searched before the `-d`/`--dir-quote` directories, the way a
        /// compiler treats `#include "…"`. It is expected to already be canonical.
        [[nodiscard]]
        std::optional<std::filesystem::path>
        ResolveQuote(std::string_view target, const std::filesystem::path& currentDir) const;

        [[nodiscard]]
        std::optional<std::filesystem::path> ResolveSystem(std::string_view target) const;

      private:
        std::vector<std::filesystem::path> quoteSearchDirs;
        std::vector<std::filesystem::path> systemSearchDirs;
    };

} // namespace Cpp_Bundler
