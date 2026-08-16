#include "scan.hpp"
#include <cstddef>

namespace Cpp_Bundler {

    namespace {

        [[nodiscard]]
        constexpr bool IsSpace(char c) noexcept {
            return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
        }

        [[nodiscard]]
        constexpr std::size_t SkipSpace(std::string_view text, std::size_t pos) noexcept {
            while (pos < text.size() && IsSpace(text[pos])) {
                ++pos;
            }
            return pos;
        }

        /// Consumes `word` at `pos`, advancing it only on success.
        [[nodiscard]]
        constexpr bool Consume(std::string_view text, std::size_t& pos, std::string_view word) noexcept {
            if (text.substr(pos, word.size()) != word) {
                return false;
            }
            pos += word.size();
            return true;
        }

    } // namespace

    std::optional<std::string_view> ScanIncludeDirective(std::string_view line) noexcept {
        std::size_t pos = SkipSpace(line, 0);
        if (pos >= line.size() || line[pos] != '#') {
            return std::nullopt;
        }

        pos = SkipSpace(line, pos + 1);
        if (!Consume(line, pos, "include")) {
            return std::nullopt;
        }

        // No separator is required here, so `#include<a>` is valid. That also means
        // `#includex<a>` is rejected: the delimiter check below sees the 'x' and bails.
        pos = SkipSpace(line, pos);
        if (pos >= line.size() || (line[pos] != '"' && line[pos] != '<')) {
            return std::nullopt;
        }

        const std::size_t referenceStart = pos;
        ++pos;

        const std::size_t bodyStart = pos;
        while (pos < line.size() && line[pos] != '>' && line[pos] != '"') {
            ++pos;
        }
        if (pos == bodyStart || pos >= line.size()) {
            return std::nullopt; // empty reference, or no closing delimiter
        }
        ++pos; // the closing '>' or '"'

        const std::size_t referenceEnd = pos;
        if (SkipSpace(line, pos) != line.size()) {
            return std::nullopt; // trailing content that is not whitespace
        }

        return line.substr(referenceStart, referenceEnd - referenceStart);
    }

    bool IsPragmaOnce(std::string_view line) noexcept {
        std::size_t pos = SkipSpace(line, 0);
        if (pos >= line.size() || line[pos] != '#') {
            return false;
        }

        pos = SkipSpace(line, pos + 1);
        if (!Consume(line, pos, "pragma")) {
            return false;
        }

        // Unlike the delimiter after `include`, `once` is a bare word, so at least one
        // space has to separate the two or `#pragmaonce` would qualify.
        const std::size_t afterKeyword = pos;
        pos                            = SkipSpace(line, pos);
        if (pos == afterKeyword) {
            return false;
        }

        return Consume(line, pos, "once") && SkipSpace(line, pos) == line.size();
    }

} // namespace Cpp_Bundler
