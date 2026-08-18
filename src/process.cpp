#include "process.hpp"
#include "error.hpp"
#include "paths.hpp"
#include "scan.hpp"
#include <fstream>
#include <spdlog/spdlog.h>
#include <system_error>
#include <utility>

namespace Cpp_Bundler {

    // NOLINTNEXTLINE(readability-identifier-naming)
    namespace fs = std::filesystem;

    namespace {

        /// Reads one line *including* its trailing newline, or the final unterminated line.
        ///
        /// Lines are copied through byte for byte, so the terminator has to survive: a file
        /// with CRLF endings must come out with CRLF endings, and a file that does not end
        /// in a newline must not gain one. Streams are opened in binary mode for the same
        /// reason.
        [[nodiscard]]
        bool ReadLine(std::istream& in, std::string& line) {
            if (!std::getline(in, line, '\n')) {
                return false;
            }
            // getline swallows the delimiter without storing it. Hitting end-of-file rather
            // than a delimiter is exactly the case where there was none to restore.
            if (!in.eof()) {
                line.push_back('\n');
            }
            return true;
        }

        /// Escapes a path for the inside of a directive's `"…"`, as `#line` and the
        /// `#include` lines of list mode both need.
        ///
        /// `Paths::ToGeneric` already removes backslashes on Windows; this covers the
        /// genuinely odd names a POSIX filesystem will happily accept.
        [[nodiscard]]
        std::string EscapeForDirective(const fs::path& path) {
            const std::string text = Paths::ToGeneric(path);
            std::string       escaped;
            escaped.reserve(text.size());
            for (const char c : text) {
                if (c == '\\' || c == '"') {
                    escaped.push_back('\\');
                }
                escaped.push_back(c);
            }
            return escaped;
        }

    } // namespace

    Processor::Processor(
        std::ostream&        output,
        IncludeResolver      includeResolver,
        InliningFilter       inliningFilter,
        OutputMode           outputMode,
        bool                 lineDirectives,
        ErrorHandlingOptions handling
    )
        : out(output)
        , resolver(std::move(includeResolver))
        , filter(std::move(inliningFilter))
        , mode(outputMode)
        , errorHandling(handling) {
        // List mode copies no lines, so leaving this disengaged is what keeps the two
        // options from contradicting each other. The command line rejects the combination
        // outright; this only makes the class true to its own contract.
        if (lineDirectives && mode == OutputMode::AMALGAMATE) {
            // Deliberately not a real position, so the very first copied line disagrees
            // with it and emits a directive.
            expectedLine = LineRef{};
        }
    }

    std::string Processor::DescribeCycle(std::size_t targetIndex) const {
        std::string report = "Cyclic include detected:\n";

        // Walk the parent chain from the file being read back to the one it re-entered.
        std::size_t walk = tail;
        report += fmt::format("\t{}\n", Paths::ToUtf8(files[walk].canonicalPath));
        while (walk != targetIndex) {
            walk = files[walk].includedBy;
            report += fmt::format("\t{}\n", Paths::ToUtf8(files[walk].canonicalPath));
        }
        return report;
    }

    Processor::IncludeAction Processor::PushToStack(fs::path canonicalPath) {
        const auto [entry, inserted] = knownFiles.try_emplace(std::move(canonicalPath), files.size());

        if (inserted) {
            // The map owns the key and never relocates it, so the copy here is the only
            // one FileState needs.
            files.push_back(FileState{entry->first, tail, 0, true});
            spdlog::info("Processing {}", DebugFileName(entry->first));
            tail = entry->second;

            // This is precisely where the file's first byte would go, so writing its name
            // here is what makes the list come out in amalgamation order -- and only the
            // `inserted` branch reaches it, so each file is named exactly once.
            if (mode == OutputMode::LIST_INCLUDES) {
                OutputIncludeLine(entry->first);
            }
            return IncludeAction::INLINE;
        }

        const std::size_t index = entry->second;
        if (!files[index].inStack) {
            spdlog::debug("Skipping {}, already included", DebugFileName(entry->first));
            return IncludeAction::REMOVE;
        }

        // Still on the stack, so following it again would not terminate. Leaving the
        // `#include` in place is the only option that keeps the output well-formed.
        HandleProblem(errorHandling.cyclicInclude, "{}", DescribeCycle(index));
        return IncludeAction::LEAVE;
    }

    void Processor::OutputIncludeLine(const fs::path& path) {
        out << "#include \"" << EscapeForDirective(path) << "\"\n";
        if (!out) {
            Fail("Failed writing to output");
        }
    }

    void Processor::OutputCopiedLine(std::string_view line) {
        // List mode names files instead of copying them, and PushToStack does the naming.
        if (mode == OutputMode::LIST_INCLUDES) {
            return;
        }

        if (expectedLine.has_value()) {
            const FileState& current = files[tail];
            const LineRef    here{tail, current.lineNumber};

            // A jump happens whenever the previous output line came from somewhere else:
            // a header was just expanded, or a `#pragma once`/`#include` line was dropped.
            if (here != *expectedLine) {
                out << "#line " << here.number << " \"" << EscapeForDirective(current.canonicalPath) << "\"\n";
                *expectedLine = here;
            }
            expectedLine->number += 1;
        }

        out.write(line.data(), static_cast<std::streamsize>(line.size()));
        if (!out) {
            Fail("Failed writing to output");
        }
    }

    bool Processor::ProcessInclude(std::string_view reference, const fs::path& currentDir) {
        // The scanner only ever yields `<delimiter><at least one char><delimiter>`, but the
        // arithmetic below underflows if that ever stops being true.
        if (reference.size() < 3) {
            return true;
        }

        const bool             isSystem = reference.starts_with('<');
        const std::string_view target   = reference.substr(1, reference.size() - 2);

        std::optional<fs::path> resolved;
        if (reference.starts_with('"') && reference.ends_with('"')) {
            resolved = resolver.ResolveQuote(target, currentDir);
        } else if (isSystem && reference.ends_with('>')) {
            resolved = resolver.ResolveSystem(target);
        } else {
            // Mismatched delimiters, e.g. `#include <a"`. Not ours to interpret.
            spdlog::debug("Found weird include-like statement: {}", reference);
            return true;
        }

        if (!resolved.has_value()) {
            HandleProblem(
                isSystem ? errorHandling.unresolvableSystemInclude : errorHandling.unresolvableQuoteInclude,
                "Could not resolve {}",
                reference
            );
            return true;
        }

        if (!filter.ShouldInline(*resolved, isSystem)) {
            return true;
        }

        switch (PushToStack(std::move(*resolved))) {
            case IncludeAction::INLINE:
                ProcessRecursively();
                return false;
            case IncludeAction::REMOVE:
                return false;
            case IncludeAction::LEAVE:
                return true;
        }
        return true;
    }

    bool Processor::ProcessLine(std::istream& in, std::string& line, const fs::path& currentDir) {
        if (!ReadLine(in, line)) {
            if (in.bad()) {
                Fail("Failed to read from \"{}\"", Paths::ToUtf8(files[tail].canonicalPath));
            }
            return false;
        }

        files[tail].lineNumber += 1;

        // Dropped rather than copied: several compilers warn or error on a `#pragma once`
        // in a file that is not a header, which the amalgamation no longer is.
        if (IsPragmaOnce(line)) {
            spdlog::trace("Skipping pragma once");
            return true;
        }

        if (const std::optional<std::string_view> reference = ScanIncludeDirective(line)) {
            // The view aliases `line`, and the recursion below reads into its own buffer,
            // so it stays valid for the whole call.
            if (!ProcessInclude(*reference, currentDir)) {
                return true; // handled: the directive itself is not copied
            }
        }

        OutputCopiedLine(line);
        return true;
    }

    void Processor::ProcessRecursively() {
        // Copied, not referenced: files grows as nested includes are discovered, and any
        // reference into it would dangle the moment the vector reallocates.
        const fs::path path = files[tail].canonicalPath;

        const fs::path currentDir = path.parent_path();
        if (currentDir.empty()) {
            Fail("Processed file \"{}\" has no parent directory", Paths::ToUtf8(path));
        }

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            Fail("Failed to open file \"{}\"", Paths::ToUtf8(path));
        }

        std::string line;
        while (ProcessLine(in, line, currentDir)) {
        }

        files[tail].inStack = false;
        tail                = files[tail].includedBy;
    }

    void Processor::Process(const fs::path& sourceFile) {
        spdlog::info("Processing source file {}", DebugFileName(sourceFile));

        std::error_code ec;
        fs::path        canonicalPath = fs::canonical(sourceFile, ec);
        if (ec) {
            Fail("Failed to canonicalize source file path \"{}\": {}", Paths::ToUtf8(sourceFile), ec.message());
        }

        // Between source files the stack is empty, so a repeated source file can only come
        // back as Remove -- never as a cycle.
        if (PushToStack(std::move(canonicalPath)) == IncludeAction::INLINE) {
            ProcessRecursively();
        }
    }

} // namespace Cpp_Bundler
