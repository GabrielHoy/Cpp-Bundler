#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Test_Util {

    /// A throwaway directory tree, deleted when the test that owns it finishes.
    class TempTree {
      public:
        TempTree();
        ~TempTree();

        TempTree(const TempTree&)            = delete;
        TempTree& operator=(const TempTree&) = delete;
        TempTree(TempTree&&)                 = delete;
        TempTree& operator=(TempTree&&)      = delete;

        [[nodiscard]]
        const std::filesystem::path& Root() const noexcept {
            return root;
        }

        /// Writes `content` verbatim to `relative`, creating parent directories as needed.
        /// The file is opened in binary mode so the test controls its line endings exactly.
        std::filesystem::path Write(std::string_view relative, std::string_view content) const;

        std::filesystem::path MakeDir(std::string_view relative) const;

        /// Creates `link` pointing at `target`, or returns false when the platform will not
        /// allow it -- unprivileged Windows without Developer Mode, most commonly.
        [[nodiscard]]
        bool TrySymlink(std::string_view link, const std::filesystem::path& target) const;

        [[nodiscard]]
        std::string Path(std::string_view relative) const;
        [[nodiscard]]
        std::string RootPath() const;

      private:
        std::filesystem::path root;
    };

    /// The canonical path of `path`, spelled the way `#line` directives spell it.
    [[nodiscard]]
    std::string CanonicalGeneric(const std::filesystem::path& path);

    /// Runs the whole pipeline as `main` does, but collects the output into a string.
    ///
    /// `arguments` excludes the program name. Throws whatever the real program would.
    [[nodiscard]]
    std::string RunBundler(std::vector<std::string> arguments);

} // namespace Test_Util
