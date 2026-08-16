#pragma once

#include <optional>
#include <string_view>

namespace Cpp_Bundler {

    /// Returns the `"…"` or `<…>` token of an `#include` line, delimiters included.
    ///
    /// The returned view aliases `line`, so it stays valid only as long as the caller's
    /// buffer is untouched.
    ///
    /// Mismatched delimiters (`<a"`) are returned as they were written rather than
    /// rejected here; deciding what to do with them belongs to the caller, which keeps this
    /// a pure lexer.
    ///
    /// This replaces the original's regex. The grammar is three keywords wide, so a
    /// hand-written scanner is both shorter to read and far cheaper than a regex engine
    /// invoked once per line of every file in the bundle.
    [[nodiscard]]
    std::optional<std::string_view> ScanIncludeDirective(std::string_view line) noexcept;

    /// Whether the line is a `#pragma once`, in any of its whitespace spellings.
    [[nodiscard]]
    bool IsPragmaOnce(std::string_view line) noexcept;

} // namespace Cpp_Bundler
