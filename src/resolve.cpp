#include "resolve.hpp"
#include "error.hpp"
#include "paths.hpp"
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>
#include <utility>

namespace Cpp_Bundler {

    namespace fs = std::filesystem;

    namespace {

        [[nodiscard]]
        std::vector<fs::path> CanonicalizeAll(const std::vector<fs::path>& dirs) {
            std::vector<fs::path> canonical;
            canonical.reserve(dirs.size());

            for (const fs::path& dir : dirs) {
                std::error_code ec;
                fs::path        resolved = fs::canonical(dir, ec);
                if (ec) {
                    Fail("Failed to canonicalize search path \"{}\": {}", Paths::ToUtf8(dir), ec.message());
                }
                canonical.push_back(std::move(resolved));
            }
            return canonical;
        }

        [[nodiscard]]
        std::string DescribeInclude(std::string_view target, bool isQuote) {
            return isQuote ? fmt::format("\"{}\"", target) : fmt::format("<{}>", target);
        }

        [[nodiscard]]
        std::optional<fs::path>
        TryDirectory(const fs::path& dir, const fs::path& target, std::string_view description) {
            fs::path candidate = dir / target;
            spdlog::trace("Trying to resolve {} to {}", description, Paths::ToUtf8(candidate));

            std::error_code       ec;
            const fs::file_status status = fs::status(candidate, ec);
            // status() follows links, so a symlink pointing at a real header resolves,
            // while a directory that happens to share the name does not.
            if (ec || !fs::exists(status) || fs::is_directory(status)) {
                return std::nullopt;
            }

            fs::path canonical = fs::canonical(candidate, ec);
            if (ec) {
                Fail("Failed to canonicalize path to include \"{}\": {}", Paths::ToUtf8(candidate), ec.message());
            }
            return canonical;
        }

        [[nodiscard]]
        std::optional<fs::path>
        ResolveIn(std::string_view target, const fs::path* currentDir, const std::vector<fs::path>& searchDirs) {
            const std::string description = DescribeInclude(target, currentDir != nullptr);
            const fs::path    relative    = Paths::FromUtf8(target);

            if (currentDir != nullptr) {
                if (std::optional<fs::path> resolved = TryDirectory(*currentDir, relative, description)) {
                    spdlog::debug("Resolved {} to {}", description, Paths::ToUtf8(*resolved));
                    return resolved;
                }
            }

            for (const fs::path& dir : searchDirs) {
                if (std::optional<fs::path> resolved = TryDirectory(dir, relative, description)) {
                    spdlog::debug("Resolved {} to {}", description, Paths::ToUtf8(*resolved));
                    return resolved;
                }
            }

            spdlog::debug("Failed to resolve {}", description);
            return std::nullopt;
        }

    } // namespace

    IncludeResolver::IncludeResolver(const std::vector<fs::path>& quoteDirs, const std::vector<fs::path>& systemDirs)
        : quoteSearchDirs(CanonicalizeAll(quoteDirs))
        , systemSearchDirs(CanonicalizeAll(systemDirs)) {
        if (spdlog::should_log(spdlog::level::debug)) {
            for (const fs::path& dir : quoteSearchDirs) {
                spdlog::debug("Quote search dir: {}", Paths::ToUtf8(dir));
            }
            for (const fs::path& dir : systemSearchDirs) {
                spdlog::debug("System search dir: {}", Paths::ToUtf8(dir));
            }
        }
    }

    std::optional<fs::path> IncludeResolver::ResolveQuote(std::string_view target, const fs::path& currentDir) const {
        return ResolveIn(target, &currentDir, quoteSearchDirs);
    }

    std::optional<fs::path> IncludeResolver::ResolveSystem(std::string_view target) const {
        return ResolveIn(target, nullptr, systemSearchDirs);
    }

} // namespace Cpp_Bundler
