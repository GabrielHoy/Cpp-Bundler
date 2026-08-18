#pragma once

#include <fmt/format.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace Cpp_Bundler {

    /// A fatal, user-facing failure.
    ///
    /// The message is already phrased for the command line's error output, so `main` can
    /// print it verbatim rather than wrapping it in yet another layer of explanation.
    class Error : public std::runtime_error {
      public:
        explicit Error(const std::string& message)
            : std::runtime_error(message) {
        }
    };

    /// A mistake in how the program was invoked, as opposed to a failure while running.
    ///
    /// Carries its own already-formatted usage text, so it is printed plainly instead of
    /// being routed through the logger like a runtime error.
    class UsageError : public Error {
      public:
        using Error::Error;
    };

    /// Throws an `Error` built from a compile-time-checked format string.
    template <typename... Args>
    [[noreturn]]
    void Fail(fmt::format_string<Args...> format, Args&&... args) {
        throw Error{fmt::format(format, std::forward<Args>(args)...)};
    }

} // namespace Cpp_Bundler
