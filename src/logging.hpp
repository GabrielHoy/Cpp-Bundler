#pragma once

#include "error.hpp"
#include <array>
#include <filesystem>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <utility>

namespace Cpp_Bundler {

    /// What to do about a condition the user may or may not consider fatal.
    enum class ErrorHandling : std::uint8_t { ERROR, WARN, IGNORE };

    inline constexpr std::array<std::string_view, 3> ERROR_HANDLING_NAMES{"error", "warn", "ignore"};

    [[nodiscard]]
    std::optional<ErrorHandling> ParseErrorHandling(std::string_view name) noexcept;

    /// The file name on its own, for log lines that an absolute path would otherwise swamp.
    [[nodiscard]]
    std::string DebugFileName(const std::filesystem::path& path);

    /// Installs the stderr logger.
    ///
    /// Diagnostics must never share a stream with the amalgamation, or `cpp-bundler … > out.cpp`
    /// would splice log lines into the generated source.
    void InitLogging(spdlog::level::level_enum level);

    void SetLogLevel(spdlog::level::level_enum level);

    /// Applies the user's chosen policy to a recoverable problem.
    ///
    /// This is the C++ counterpart of the original's `error_handling_handle!` macro: one
    /// message either aborts the run, warns, or is noted at debug level, depending on what
    /// the corresponding `--…-include` flag was set to.
    template <typename... Args>
    void HandleProblem(ErrorHandling handling, fmt::format_string<Args...> format, Args&&... args) {
        // Ignoring is the default for unresolvable includes and can therefore be hit for
        // every single include in a large tree; don't pay to format a message nobody reads.
        if (handling == ErrorHandling::IGNORE && !spdlog::should_log(spdlog::level::debug)) {
            return;
        }

        std::string message = fmt::format(format, std::forward<Args>(args)...);
        switch (handling) {
            case ErrorHandling::ERROR:
                throw Error{message};
            case ErrorHandling::WARN:
                spdlog::warn("{}", message);
                break;
            case ErrorHandling::IGNORE:
                spdlog::debug("Ignoring: {}", message);
                break;
        }
    }

} // namespace Cpp_Bundler
