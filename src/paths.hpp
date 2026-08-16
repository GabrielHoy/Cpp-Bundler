#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Cpp_Bundler::Paths {

    /// Interprets `text` as UTF-8 when building a path.
    ///
    /// `std::filesystem::path{std::string}` runs narrow input through the active code page
    /// on Windows, which silently mangles anything outside it. Everything reaching this
    /// program -- argv, and the text inside `#include` statements -- is UTF-8, so we say so.
    [[nodiscard]]
    std::filesystem::path FromUtf8(std::string_view text);

    /// The path's native spelling, as UTF-8 bytes. Use this for messages shown to a human.
    [[nodiscard]]
    std::string ToUtf8(const std::filesystem::path& path);

    /// UTF-8 with forward slashes and no Windows extended-length (`\\?\`) prefix.
    ///
    /// This is the form globs match against -- so that `**/x.hpp` works the same on every
    /// platform -- and the form embedded in `#line` directives, where a backslash would
    /// otherwise be read as the start of an escape sequence.
    [[nodiscard]]
    std::string ToGeneric(const std::filesystem::path& path);

} // namespace Cpp_Bundler::Paths
