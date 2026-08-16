#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Cpp_Bundler {

    /// A compiled glob, matched against a whole path rather than one path component.
    ///
    /// Supported syntax:
    ///   - `?`      one character, never a separator
    ///   - `*`      any run of characters, never crossing a separator
    ///   - `**`     any run of whole path components; recursive only where it forms a
    ///              complete component (`**/x`, `a/**/b`, `a/**`), and a plain `*` anywhere
    ///              else (`a**b`), which is what every other glob implementation does
    ///   - `[a-z]`  a character class; `[!…]` and `[^…]` negate it
    ///   - `{a,b}`  alternation, which may nest
    ///   - `\c`     a literal `c` (POSIX only -- on Windows `\` is a path separator)
    ///
    /// Candidates are compared by `Matches`, which expects the forward-slash spelling that
    /// `Paths::ToGeneric` produces. On Windows, comparison ignores case, matching how the
    /// filesystem itself decides whether two names refer to the same file.
    ///
    /// Note that a lone `**` matches every path. Read strictly it would mean "any number of
    /// complete components and nothing else", which no path satisfies; every glob library
    /// carves out the same exception, and `--filter '**'` (exclude everything, then allow
    /// things back in with `!`) depends on it.
    class Glob {
      public:
        /// @throws Error when the pattern is malformed.
        [[nodiscard]]
        static Glob Compile(std::string_view pattern);

        [[nodiscard]]
        bool Matches(std::string_view path) const;

        [[nodiscard]]
        const std::string& Pattern() const noexcept {
            return pattern;
        }

      private:
        enum class Kind : std::uint8_t {
            LITERAL,          ///< one specific character
            ANY_CHAR,         ///< `?`
            ANY_RUN,          ///< `*`
            RECURSIVE_PREFIX, ///< leading `**/`; matches nothing, or any prefix ending in a separator
            RECURSIVE_SUFFIX, ///< trailing `/**`; matches a separator followed by anything at all
            RECURSIVE_MIDDLE, ///< interior `/**/`; matches `/` or `/…/`
            CHAR_CLASS,
            ALTERNATION,
        };

        struct Token {
            // Spelled out rather than left as an aggregate: `Token{Kind::AnyRun}` would
            // otherwise trip -Wmissing-field-initializers for the two vectors it does not
            // touch, and the build treats warnings as errors.
            Token() = default;

            explicit Token(Kind tokenKind) noexcept
                : kind(tokenKind) {
            }

            Token(Kind tokenKind, char tokenLiteral) noexcept
                : kind(tokenKind)
                , literal(tokenLiteral) {
            }

            Kind                               kind{Kind::LITERAL};
            char                               literal{};
            bool                               negated{}; ///< CharClass
            std::vector<std::pair<char, char>> ranges;    ///< CharClass, inclusive on both ends
            std::vector<std::vector<Token>>    branches;  ///< Alternation
        };

        class Parser;
        struct Continuation;

        [[nodiscard]]
        static bool MatchFrom(
            const std::vector<Token>& tokens,
            std::size_t               tokenIndex,
            std::string_view          path,
            std::size_t               pathIndex,
            const Continuation*       continuation
        );
        /// Matches the rest at `from`, or at any later position that begins a path component.
        [[nodiscard]]
        static bool MatchAtBoundaries(
            const std::vector<Token>& tokens,
            std::size_t               next,
            std::string_view          path,
            std::size_t               from,
            const Continuation*       continuation
        );
        /// Matches the rest at `from`, or at any later position at all, separators included.
        [[nodiscard]]
        static bool MatchAnywhere(
            const std::vector<Token>& tokens,
            std::size_t               next,
            std::string_view          path,
            std::size_t               from,
            const Continuation*       continuation
        );
        /// Matches the rest at `from`, or at any later position within the same component.
        [[nodiscard]]
        static bool MatchWithinComponent(
            const std::vector<Token>& tokens,
            std::size_t               next,
            std::string_view          path,
            std::size_t               from,
            const Continuation*       continuation
        );
        [[nodiscard]]
        static bool ClassMatches(const Token& token, char c) noexcept;

        Glob() = default;

        std::string        pattern;
        std::vector<Token> compiledTokens;
        /// Set for the degenerate `**` pattern described above.
        bool               matchesEverything{false};
    };

} // namespace Cpp_Bundler
