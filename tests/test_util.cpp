#include "test_util.hpp"
#include "cli.hpp"
#include "filter.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "process.hpp"
#include "resolve.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace Test_Util {

    namespace fs = std::filesystem;

    namespace {

        /// Silences the bundler for the whole test binary.
        ///
        /// Tests are judged by their assertions, and spdlog's out-of-the-box logger writes
        /// to stdout at info level -- which would scribble across the Catch2 report.
        const struct QuietLogging {
            QuietLogging() {
                Cpp_Bundler::InitLogging(spdlog::level::off);
            }
        } QUIET_LOGGING;

        /// Test binaries can run several cases in one process, and CTest can run several
        /// binaries at once, so the name has to be unique on both axes.
        [[nodiscard]]
        fs::path UniqueRoot() {
            static std::atomic<unsigned> counter{0};
            const unsigned               ordinal = counter.fetch_add(1);

            std::ostringstream name;
            name << "cpp-bundler-test-"
                 << static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()) << '-'
                 << ordinal;
            return fs::temp_directory_path() / name.str();
        }

    } // namespace

    TempTree::TempTree()
        : root(UniqueRoot()) {
        fs::create_directories(root);
        // Canonical from the start: on macOS /tmp is itself a symlink, and a test comparing
        // a #line directive against Root() would otherwise never match.
        root = fs::canonical(root);
    }

    TempTree::~TempTree() {
        std::error_code ec;
        fs::remove_all(root, ec); // a destructor is no place to throw
    }

    fs::path TempTree::Write(std::string_view relative, std::string_view content) const {
        const fs::path target = root / fs::path{relative};
        fs::create_directories(target.parent_path());

        std::ofstream out(target, std::ios::binary);
        if (!out) {
            throw std::runtime_error{"failed to create test file"};
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        return target;
    }

    fs::path TempTree::MakeDir(std::string_view relative) const {
        const fs::path target = root / fs::path{relative};
        fs::create_directories(target);
        return target;
    }

    bool TempTree::TrySymlink(std::string_view link, const fs::path& target) const {
        const fs::path linkPath = root / fs::path{link};
        fs::create_directories(linkPath.parent_path());

        std::error_code ec;
        fs::create_symlink(target, linkPath, ec);
        return !ec;
    }

    std::string TempTree::Path(std::string_view relative) const {
        return Cpp_Bundler::Paths::ToUtf8(root / fs::path{relative});
    }

    std::string TempTree::RootPath() const {
        return Cpp_Bundler::Paths::ToUtf8(root);
    }

    std::string CanonicalGeneric(const fs::path& path) {
        return Cpp_Bundler::Paths::ToGeneric(fs::canonical(path));
    }

    std::string RunBundler(std::vector<std::string> arguments) {
        using namespace Cpp_Bundler;

        arguments.insert(arguments.begin(), "cpp-bundler");
        const std::optional<Options> options = ParseCommandLine(arguments);
        if (!options.has_value()) {
            throw std::runtime_error{"the arguments were served by --help/--version, not a run"};
        }

        std::ostringstream out;
        Processor          processor{
            out,
            IncludeResolver{options->quoteSearchDirs, options->systemSearchDirs},
            InliningFilter{options->quoteFilters, options->systemFilters},
            options->lineDirectives,
            options->errorHandling
        };

        for (const fs::path& sourceFile : options->sourceFiles) {
            processor.Process(sourceFile);
        }
        return out.str();
    }

} // namespace Test_Util
