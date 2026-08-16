#pragma once

#include "glob.hpp"
#include <filesystem>
#include <string_view>
#include <vector>

namespace Cpp_Bundler {

    /// A filter glob, optionally negated by a leading `!`.
    struct InvertibleGlob {
        Glob glob;
        bool inverted{false};

        /// @throws Error when the glob is malformed.
        [[nodiscard]]
        static InvertibleGlob Parse(std::string_view spec);
    };

    /// Decides which resolved headers get inlined.
    ///
    /// Globs are evaluated last-match-wins, so a later `!…` can pull a header back in after
    /// an earlier glob excluded it. A header that nothing matches is inlined -- the default
    /// is to bundle everything that can be found.
    ///
    /// Globs run against the *canonical* path of the header, symbolic links and all, which
    /// is why a leading `**/` is usually needed: the path is absolute.
    class InliningFilter {
      public:
        InliningFilter(std::vector<InvertibleGlob> quotePatterns, std::vector<InvertibleGlob> systemPatterns);

        [[nodiscard]]
        bool ShouldInline(const std::filesystem::path& path, bool isSystemInclude) const;

      private:
        std::vector<InvertibleGlob> quoteGlobs;
        std::vector<InvertibleGlob> systemGlobs;
    };

} // namespace Cpp_Bundler
