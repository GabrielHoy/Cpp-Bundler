#include "glob.hpp"
#include "error.hpp"

namespace Cpp_Bundler {

    namespace {

        constexpr char SEPARATOR = '/';

#if defined(_WIN32)
        // Windows spells separators with a backslash and compares names without regard to
        // case, so patterns follow suit: `\` is a separator rather than an escape, and
        // matching folds case. Escaping is simply unavailable there, as it is in every
        // other Windows glob implementation.
        constexpr bool BACKSLASH_ESCAPES = false;
        constexpr bool CASE_INSENSITIVE  = true;
#else
        constexpr bool BACKSLASH_ESCAPES = true;
        constexpr bool CASE_INSENSITIVE  = false;
#endif

        /// Guards against a pattern whose nesting would recurse the parser into the ground.
        constexpr int MAX_ALTERNATION_DEPTH = 16;

        [[nodiscard]]
        constexpr bool IsPatternSeparator(char c) noexcept {
            return c == SEPARATOR || (!BACKSLASH_ESCAPES && c == '\\');
        }

        /// ASCII-only, deliberately. Case folding the rest of Unicode would need a table
        /// and full decoding; multi-byte UTF-8 sequences still compare exactly, byte for
        /// byte, so non-ASCII names keep working -- just case-sensitively.
        [[nodiscard]]
        constexpr char FoldCase(char c) noexcept {
            if constexpr (CASE_INSENSITIVE) {
                return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            } else {
                return c;
            }
        }

        [[nodiscard]]
        constexpr char SwapCase(char c) noexcept {
            if (c >= 'a' && c <= 'z') {
                return static_cast<char>(c - 'a' + 'A');
            }
            if (c >= 'A' && c <= 'Z') {
                return static_cast<char>(c - 'A' + 'a');
            }
            return c;
        }

        [[nodiscard]]
        constexpr bool InRange(char c, char low, char high) noexcept {
            const auto value = static_cast<unsigned char>(c);
            return value >= static_cast<unsigned char>(low) && value <= static_cast<unsigned char>(high);
        }

    } // namespace

    // === Matching ===

    /// What is left to match once the current token list runs out.
    ///
    /// Alternation needs this: after a branch matches, whatever followed the `{…}` still
    /// has to match, and those tokens live in a different vector. Chaining stack-allocated
    /// frames keeps the whole matcher allocation-free.
    struct Glob::Continuation {
        const std::vector<Token>* tokens;
        std::size_t               index;
        const Continuation*       next;
    };

    bool Glob::ClassMatches(const Token& token, char c) noexcept {
        bool inSet = false;
        for (const auto& [low, high] : token.ranges) {
            if (InRange(c, low, high) || (CASE_INSENSITIVE && InRange(SwapCase(c), low, high))) {
                inSet = true;
                break;
            }
        }
        return inSet != token.negated;
    }

    bool Glob::MatchAtBoundaries(
        const std::vector<Token>& tokens,
        std::size_t               next,
        std::string_view          path,
        std::size_t               from,
        const Continuation*       continuation
    ) {
        if (MatchFrom(tokens, next, path, from, continuation)) {
            return true;
        }
        for (std::size_t end = from; end < path.size(); ++end) {
            if (path[end] == SEPARATOR && MatchFrom(tokens, next, path, end + 1, continuation)) {
                return true;
            }
        }
        return false;
    }

    bool Glob::MatchAnywhere(
        const std::vector<Token>& tokens,
        std::size_t               next,
        std::string_view          path,
        std::size_t               from,
        const Continuation*       continuation
    ) {
        for (std::size_t end = from; end <= path.size(); ++end) {
            if (MatchFrom(tokens, next, path, end, continuation)) {
                return true;
            }
        }
        return false;
    }

    bool Glob::MatchWithinComponent(
        const std::vector<Token>& tokens,
        std::size_t               next,
        std::string_view          path,
        std::size_t               from,
        const Continuation*       continuation
    ) {
        // Grows one character at a time and stops at a separator, which is what keeps `*`
        // inside a single path component.
        for (std::size_t end = from;; ++end) {
            if (MatchFrom(tokens, next, path, end, continuation)) {
                return true;
            }
            if (end == path.size() || path[end] == SEPARATOR) {
                return false;
            }
        }
    }

    bool Glob::MatchFrom(
        const std::vector<Token>& tokens,
        std::size_t               tokenIndex,
        std::string_view          path,
        std::size_t               pathIndex,
        const Continuation*       continuation
    ) {
        if (tokenIndex == tokens.size()) {
            if (continuation == nullptr) {
                return pathIndex == path.size();
            }
            return MatchFrom(*continuation->tokens, continuation->index, path, pathIndex, continuation->next);
        }

        const Token&      token = tokens[tokenIndex];
        const std::size_t next  = tokenIndex + 1;

        switch (token.kind) {
            case Kind::LITERAL:
                return pathIndex < path.size() && FoldCase(path[pathIndex]) == FoldCase(token.literal)
                       && MatchFrom(tokens, next, path, pathIndex + 1, continuation);

            case Kind::ANY_CHAR:
                return pathIndex < path.size() && path[pathIndex] != SEPARATOR
                       && MatchFrom(tokens, next, path, pathIndex + 1, continuation);

            case Kind::CHAR_CLASS:
                return pathIndex < path.size() && path[pathIndex] != SEPARATOR && ClassMatches(token, path[pathIndex])
                       && MatchFrom(tokens, next, path, pathIndex + 1, continuation);

            case Kind::ANY_RUN:
                return MatchWithinComponent(tokens, next, path, pathIndex, continuation);

            case Kind::RECURSIVE_PREFIX:
                // Either nothing at all, or any prefix that ends on a component boundary.
                return MatchAtBoundaries(tokens, next, path, pathIndex, continuation);

            case Kind::RECURSIVE_SUFFIX:
                // The separator this token absorbed has to be there; everything past it is
                // unconstrained, separators included.
                return pathIndex < path.size() && path[pathIndex] == SEPARATOR
                       && MatchAnywhere(tokens, next, path, pathIndex + 1, continuation);

            case Kind::RECURSIVE_MIDDLE:
                // Starting the search one past the separator makes the zero-component case
                // (`a/**/b` matching `a/b`) the first thing tried.
                return pathIndex < path.size() && path[pathIndex] == SEPARATOR
                       && MatchAtBoundaries(tokens, next, path, pathIndex + 1, continuation);

            case Kind::ALTERNATION: {
                const Continuation frame{&tokens, next, continuation};
                for (const std::vector<Token>& branch : token.branches) {
                    if (MatchFrom(branch, 0, path, pathIndex, &frame)) {
                        return true;
                    }
                }
                return false;
            }
        }

        return false;
    }

    bool Glob::Matches(std::string_view path) const {
        return matchesEverything || MatchFrom(compiledTokens, 0, path, 0, nullptr);
    }

    // === Parsing ===

    class Glob::Parser {
      public:
        explicit Parser(std::string_view source) noexcept
            : pattern(source) {
        }

        [[nodiscard]]
        std::vector<Token> ParseSequence(int depth, bool insideAlternation);

      private:
        void ParseStar(std::vector<Token>& sequence);
        [[nodiscard]]
        Token ParseCharClass();
        [[nodiscard]]
        Token ParseAlternation(int depth);

        [[nodiscard]]
        static bool IsRecursive(Kind kind) noexcept {
            return kind == Kind::RECURSIVE_PREFIX || kind == Kind::RECURSIVE_SUFFIX || kind == Kind::RECURSIVE_MIDDLE;
        }

        std::string_view pattern;
        std::size_t      pos{0};
    };

    // `sequence` throughout the parser, never `tokens`: Parser is nested inside Glob, so a
    // local called `tokens` would hide Glob's own member and the shadow warnings would
    // (rightly) reject it.
    std::vector<Glob::Token> Glob::Parser::ParseSequence(int depth, bool insideAlternation) {
        std::vector<Token> sequence;

        while (pos < pattern.size()) {
            const char c = pattern[pos];
            if (insideAlternation && (c == ',' || c == '}')) {
                break;
            }

            switch (c) {
                case '?':
                    ++pos;
                    sequence.push_back(Token{Kind::ANY_CHAR});
                    break;

                case '*':
                    ParseStar(sequence);
                    break;

                case '[':
                    sequence.push_back(ParseCharClass());
                    break;

                case '{':
                    sequence.push_back(ParseAlternation(depth));
                    break;

                case '\\':
                    ++pos;
                    if constexpr (BACKSLASH_ESCAPES) {
                        if (pos >= pattern.size()) {
                            Fail("Glob pattern \"{}\" ends with a dangling '\\'", pattern);
                        }
                        sequence.push_back(Token{Kind::LITERAL, pattern[pos]});
                        ++pos;
                    } else {
                        // A separator, normalised so that a pattern written with backslashes
                        // still matches the forward-slash candidates we build.
                        sequence.push_back(Token{Kind::LITERAL, SEPARATOR});
                    }
                    break;

                default:
                    ++pos;
                    sequence.push_back(Token{Kind::LITERAL, c});
                    break;
            }
        }

        return sequence;
    }

    void Glob::Parser::ParseStar(std::vector<Token>& sequence) {
        const std::size_t start = pos;
        ++pos;

        if (pos >= pattern.size() || pattern[pos] != '*') {
            sequence.push_back(Token{Kind::ANY_RUN});
            return;
        }
        ++pos;

        const bool atStart         = start == 0;
        const bool afterSeparator  = !atStart && IsPatternSeparator(pattern[start - 1]);
        const bool atEnd           = pos == pattern.size();
        const bool beforeSeparator = !atEnd && IsPatternSeparator(pattern[pos]);

        // `**` only means "any number of path components" where it stands alone as one.
        if ((!atStart && !afterSeparator) || (!atEnd && !beforeSeparator)) {
            sequence.push_back(Token{Kind::ANY_RUN});
            return;
        }

        if (atStart) {
            sequence.push_back(Token{Kind::RECURSIVE_PREFIX});
            if (beforeSeparator) {
                ++pos; // the separator belongs to this token
            }
            return;
        }

        // A neighbouring recursive token has already swallowed the separator, so fold into
        // it rather than mangling it: `**/**` is just `**`.
        if (!sequence.empty() && IsRecursive(sequence.back().kind)) {
            if (beforeSeparator) {
                ++pos;
            }
            return;
        }

        // The two remaining forms absorb the separator in front of them, so discard the
        // literal token already emitted for it.
        if (!sequence.empty()) {
            sequence.pop_back();
        }

        if (atEnd) {
            sequence.push_back(Token{Kind::RECURSIVE_SUFFIX});
            return;
        }
        sequence.push_back(Token{Kind::RECURSIVE_MIDDLE});
        ++pos; // the trailing separator
    }

    Glob::Token Glob::Parser::ParseCharClass() {
        const std::size_t open = pos;
        ++pos; // '['

        Token token{Kind::CHAR_CLASS};
        if (pos < pattern.size() && (pattern[pos] == '!' || pattern[pos] == '^')) {
            token.negated = true;
            ++pos;
        }

        bool first = true;
        while (pos < pattern.size()) {
            char low = pattern[pos];
            // A `]` in the leading position is a literal -- the only way to get one into a
            // class at all.
            if (low == ']' && !first) {
                ++pos;
                return token;
            }
            first = false;
            ++pos;

            if constexpr (BACKSLASH_ESCAPES) {
                if (low == '\\') {
                    if (pos >= pattern.size()) {
                        break;
                    }
                    low = pattern[pos];
                    ++pos;
                }
            }

            char high = low;
            // `a-z` is a range, but a `-` immediately before the closing bracket is a literal.
            if (pos + 1 < pattern.size() && pattern[pos] == '-' && pattern[pos + 1] != ']') {
                ++pos; // '-'
                high = pattern[pos];
                ++pos;

                if constexpr (BACKSLASH_ESCAPES) {
                    if (high == '\\' && pos < pattern.size()) {
                        high = pattern[pos];
                        ++pos;
                    }
                }

                if (static_cast<unsigned char>(high) < static_cast<unsigned char>(low)) {
                    Fail("Glob pattern \"{}\" has a reversed character range '{}-{}'", pattern, low, high);
                }
            }

            token.ranges.emplace_back(low, high);
        }

        Fail("Glob pattern \"{}\" has a character class opened at offset {} that is never closed", pattern, open);
    }

    Glob::Token Glob::Parser::ParseAlternation(int depth) {
        if (depth >= MAX_ALTERNATION_DEPTH) {
            Fail("Glob pattern \"{}\" nests alternations more than {} deep", pattern, MAX_ALTERNATION_DEPTH);
        }

        const std::size_t open = pos;
        ++pos; // '{'

        Token token{Kind::ALTERNATION};
        for (;;) {
            token.branches.push_back(ParseSequence(depth + 1, true));

            if (pos >= pattern.size()) {
                Fail("Glob pattern \"{}\" has an alternation opened at offset {} that is never closed", pattern, open);
            }
            const char c = pattern[pos];
            ++pos;
            if (c == '}') {
                return token;
            }
            // Otherwise it was a ',' and the next branch follows.
        }
    }

    Glob Glob::Compile(std::string_view pattern) {
        if (pattern.empty()) {
            Fail("Glob pattern is empty");
        }

        Glob glob;
        glob.pattern.assign(pattern);

        Parser parser{pattern};
        glob.compiledTokens = parser.ParseSequence(0, false);
        glob.matchesEverything =
            glob.compiledTokens.size() == 1 && glob.compiledTokens.front().kind == Kind::RECURSIVE_PREFIX;
        return glob;
    }

} // namespace Cpp_Bundler
