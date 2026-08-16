#include "filter.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace Cpp_Bundler {

    namespace {

        void LogGlobs(std::string_view kind, const std::vector<InvertibleGlob>& globs) {
            if (!spdlog::should_log(spdlog::level::debug)) {
                return;
            }

            std::string joined;
            for (const InvertibleGlob& entry : globs) {
                if (!joined.empty()) {
                    joined += ", ";
                }
                if (entry.inverted) {
                    joined += '!';
                }
                joined += entry.glob.Pattern();
            }
            spdlog::debug("{} filter globs: [{}]", kind, joined);
        }

    } // namespace

    InvertibleGlob InvertibleGlob::Parse(std::string_view spec) {
        const bool inverted = spec.starts_with('!');
        if (inverted) {
            spec.remove_prefix(1);
        }
        return InvertibleGlob{Glob::Compile(spec), inverted};
    }

    InliningFilter::InliningFilter(
        std::vector<InvertibleGlob> quotePatterns,
        std::vector<InvertibleGlob> systemPatterns
    )
        : quoteGlobs(std::move(quotePatterns))
        , systemGlobs(std::move(systemPatterns)) {
        LogGlobs("Quote", quoteGlobs);
        LogGlobs("System", systemGlobs);
    }

    bool InliningFilter::ShouldInline(const std::filesystem::path& path, bool isSystemInclude) const {
        const std::vector<InvertibleGlob>& globs = isSystemInclude ? systemGlobs : quoteGlobs;
        if (globs.empty()) {
            return true;
        }

        const std::string candidate = Paths::ToGeneric(path);

        // Later globs override earlier ones, so walking backwards makes the first hit the
        // deciding one and lets us skip everything before it.
        for (auto it = globs.rbegin(); it != globs.rend(); ++it) {
            if (!it->glob.Matches(candidate)) {
                continue;
            }
            spdlog::debug(
                "{} {} (cause: '{}{}')",
                it->inverted ? "Inlining" : "Not inlining",
                DebugFileName(path),
                it->inverted ? "!" : "",
                it->glob.Pattern()
            );
            return it->inverted;
        }

        spdlog::debug("Inlining {} by default", DebugFileName(path));
        return true;
    }

} // namespace Cpp_Bundler
