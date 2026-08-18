#pragma once

#include "filter.hpp"
#include "logging.hpp"
#include "resolve.hpp"
#include <cstddef>
#include <filesystem>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Cpp_Bundler {

    struct ErrorHandlingOptions {
        ErrorHandling cyclicInclude{ErrorHandling::ERROR};
        ErrorHandling unresolvableQuoteInclude{ErrorHandling::IGNORE};
        ErrorHandling unresolvableSystemInclude{ErrorHandling::IGNORE};
    };

    /// What a `Processor` writes for each file it decides to bundle.
    enum class OutputMode : std::uint8_t {
        /// The files themselves, concatenated: the amalgamation.
        AMALGAMATE,
        /// One `#include` line naming each file instead of its contents, in the order those
        /// contents would have appeared. The walk is identical either way -- resolution,
        /// filtering and the once-only rule all still apply -- only the writing differs.
        LIST_INCLUDES,

    };

    /// Walks source files and the headers they include, writing one amalgamated stream.
    ///
    /// No preprocessor is involved: `#include` is the only directive interpreted, and every
    /// header is emitted at most once as though it carried `#pragma once`. Traditional
    /// `#if` header guards are therefore invisible here, and an `#include` sitting inside
    /// two different `#if` blocks is expanded in the first and dropped in the second.
    ///
    /// State persists across `Process` calls so that several source files given on one
    /// command line share a single set of already-included headers.
    class Processor {
      public:
        /// `lineDirectives` is ignored under `OutputMode::LIST_INCLUDES`: nothing is copied
        /// there, so there are no output lines for a `#line` directive to remap.
        Processor(
            std::ostream&        output,
            IncludeResolver      includeResolver,
            InliningFilter       inliningFilter,
            OutputMode           outputMode,
            bool                 lineDirectives,
            ErrorHandlingOptions handling
        );

        ~Processor() = default;

        Processor(const Processor&)            = delete;
        Processor& operator=(const Processor&) = delete;
        Processor(Processor&&)                 = delete;
        Processor& operator=(Processor&&)      = delete;

        /// @throws Error on I/O failure, or on a condition the user asked to treat as fatal.
        void Process(const std::filesystem::path& sourceFile);

      private:
        static constexpr std::size_t NO_FILE = std::numeric_limits<std::size_t>::max();

        enum class IncludeAction : std::uint8_t {
            INLINE, ///< expand the header here, and drop the `#include` line
            REMOVE, ///< already emitted elsewhere: drop the `#include` line
            LEAVE,  ///< keep the `#include` line exactly as written
        };

        struct FileState {
            std::filesystem::path canonicalPath;
            /// Index of the file that pulled this one in, forming a parent chain that a
            /// cycle report can walk back along.
            std::size_t           includedBy{NO_FILE};
            std::size_t           lineNumber{0};
            bool                  inStack{false};
        };

        /// A position in the *input*, used to work out when a `#line` directive is needed.
        struct LineRef {
            std::size_t fileIndex{NO_FILE};
            std::size_t number{0};

            [[nodiscard]]
            friend bool operator==(const LineRef&, const LineRef&) noexcept = default;
        };

        struct PathHash {
            [[nodiscard]]
            std::size_t operator()(const std::filesystem::path& path) const noexcept {
                return std::filesystem::hash_value(path);
            }
        };

        [[nodiscard]]
        IncludeAction PushToStack(std::filesystem::path canonicalPath);
        void          ProcessRecursively();
        void          OutputIncludeLine(const std::filesystem::path& path);
        [[nodiscard]]
        bool ProcessLine(std::istream& in, std::string& line, const std::filesystem::path& currentDir);
        [[nodiscard]]
        bool ProcessInclude(std::string_view reference, const std::filesystem::path& currentDir);
        void OutputCopiedLine(std::string_view line);
        [[nodiscard]]
        std::string DescribeCycle(std::size_t targetIndex) const;

        std::ostream&        out;
        IncludeResolver      resolver;
        InliningFilter       filter;
        OutputMode           mode;
        ErrorHandlingOptions errorHandling;

        /// Every file seen so far. Indices are stable; references are not, because this
        /// grows while a file is being processed -- always re-index, never cache a pointer
        /// across anything that might push.
        std::vector<FileState>                                           files;
        std::unordered_map<std::filesystem::path, std::size_t, PathHash> knownFiles;
        /// Index of the file currently being read, or `NO_FILE` between source files.
        std::size_t                                                      tail{NO_FILE};
        /// Engaged only when `--line-directives` is on. Holds where the next copied line
        /// would land if no directive were emitted; a mismatch is what triggers one.
        std::optional<LineRef>                                           expectedLine;
    };

} // namespace Cpp_Bundler
