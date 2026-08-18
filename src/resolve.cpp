#include "resolve.hpp"
#include "error.hpp"
#include "paths.hpp"
#include <spdlog/spdlog.h>
#include <string>
#include <system_error>
#include <utility>

namespace Cpp_Bundler {

    namespace Fs = std::filesystem;

    namespace {

        [[nodiscard]]
        std::vector<Fs::path> CanonicalizeAll(const std::vector<Fs::path>& dirs) {
            std::vector<Fs::path> canonical;
            canonical.reserve(dirs.size());

            for (const Fs::path& dir : dirs) {
                std::error_code ec;
                Fs::path        resolved = Fs::canonical(dir, ec);
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
        std::optional<Fs::path>
        TryDirectory(const Fs::path& dir, const Fs::path& target, std::string_view description) {
            Fs::path candidate = dir / target;
            spdlog::trace("Trying to resolve {} to {}", description, Paths::ToUtf8(candidate));

            std::error_code       ec;
            const Fs::file_status status = Fs::status(candidate, ec);
            // status() follows links, so a symlink pointing at a real header resolves,
            // while a directory that happens to share the name does not.
            if (ec || !Fs::exists(status) || Fs::is_directory(status)) {
                return std::nullopt;
            }

            Fs::path canonical = Fs::canonical(candidate, ec);
            if (ec) {
                Fail("Failed to canonicalize path to include \"{}\": {}", Paths::ToUtf8(candidate), ec.message());
            }
            return canonical;
        }

        [[nodiscard]]
        std::optional<Fs::path>
        ResolveIn(std::string_view target, const Fs::path* currentDir, const std::vector<Fs::path>& searchDirs) {
            const std::string description = DescribeInclude(target, currentDir != nullptr);
            const Fs::path    relative    = Paths::FromUtf8(target);

            if (currentDir != nullptr) {
                if (std::optional<Fs::path> resolved = TryDirectory(*currentDir, relative, description)) {
                    spdlog::debug("Resolved {} to {}", description, Paths::ToUtf8(*resolved));
                    return resolved;
                }
            }

            for (const Fs::path& dir : searchDirs) {
                if (std::optional<Fs::path> resolved = TryDirectory(dir, relative, description)) {
                    spdlog::debug("Resolved {} to {}", description, Paths::ToUtf8(*resolved));
                    return resolved;
                }
            }

            spdlog::debug("Failed to resolve {}", description);
            return std::nullopt;
        }

    } // namespace

    IncludeResolver::IncludeResolver(const std::vector<Fs::path>& quoteDirs, const std::vector<Fs::path>& systemDirs)
        : quoteSearchDirs(CanonicalizeAll(quoteDirs))
        , systemSearchDirs(CanonicalizeAll(systemDirs)) {
        if (spdlog::should_log(spdlog::level::debug)) {
            for (const Fs::path& dir : quoteSearchDirs) {
                spdlog::debug("Quote search dir: {}", Paths::ToUtf8(dir));
            }
            for (const Fs::path& dir : systemSearchDirs) {
                spdlog::debug("System search dir: {}", Paths::ToUtf8(dir));
            }
        }
    }

    std::optional<Fs::path> IncludeResolver::ResolveQuote(std::string_view target, const Fs::path& currentDir) const {
        return ResolveIn(target, &currentDir, quoteSearchDirs);
    }

    std::optional<Fs::path> IncludeResolver::ResolveSystem(std::string_view target) const {
        return ResolveIn(target, nullptr, systemSearchDirs);
    }

} // namespace Cpp_Bundler
