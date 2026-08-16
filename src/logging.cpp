#include "logging.hpp"
#include "paths.hpp"
#include <memory>
#include <spdlog/sinks/stdout_color_sinks.h> // despite its name, this declares the stderr sinks too

namespace Cpp_Bundler {

    std::optional<ErrorHandling> ParseErrorHandling(std::string_view name) noexcept {
        if (name == ERROR_HANDLING_NAMES[0]) {
            return ErrorHandling::Error;
        }
        if (name == ERROR_HANDLING_NAMES[1]) {
            return ErrorHandling::Warn;
        }
        if (name == ERROR_HANDLING_NAMES[2]) {
            return ErrorHandling::Ignore;
        }
        return std::nullopt;
    }

    std::string DebugFileName(const std::filesystem::path& path) {
        const std::filesystem::path name = path.filename();
        return name.empty() ? std::string{"<no file name?>"} : Paths::ToUtf8(name);
    }

    void InitLogging(spdlog::level::level_enum level) {
        auto logger = spdlog::stderr_color_mt("cpp-bundler");
        // Level and message only. A timestamp and logger name would be noise for a tool
        // that is usually run from a shell and read once.
        logger->set_pattern("%^%l%$: %v");
        logger->set_level(level);
        // Diagnostics interleave with a possibly-redirected stdout, so buffering them
        // would reorder the two streams in confusing ways.
        logger->flush_on(spdlog::level::trace);
        spdlog::set_default_logger(std::move(logger));
    }

    void SetLogLevel(spdlog::level::level_enum level) {
        spdlog::default_logger()->set_level(level);
    }

} // namespace Cpp_Bundler
