#include "cli.hpp"
#include "error.hpp"
#include "logging.hpp"
#include "paths.hpp"
#include "process.hpp"
#include "resolve.hpp"
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ostream>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>

    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    // Must follow windows.h: shellapi.h relies on its typedefs.
    #include <shellapi.h>
#endif

namespace {

    using namespace Cpp_Bundler;

#if defined(_WIN32)

    [[nodiscard]]
    std::string WideToUtf8(const wchar_t* text) {
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (bytes <= 1) {
            return {};
        }
        // The count includes the terminating null, which std::string supplies itself.
        std::string converted(static_cast<std::size_t>(bytes - 1), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text, -1, converted.data(), bytes, nullptr, nullptr);
        return converted;
    }

#endif

    /// Collects the command line as UTF-8.
    ///
    /// On Windows `argv` is encoded in the active code page, which silently mangles any
    /// path outside it. The wide command line is the only lossless source available, so it
    /// is what we read -- and everything downstream can then assume UTF-8 throughout.
    [[nodiscard]]
    std::vector<std::string> CollectArguments(int argc, char** argv) {
#if defined(_WIN32)
        int       wideCount = 0;
        wchar_t** wideArgv  = ::CommandLineToArgvW(::GetCommandLineW(), &wideCount);
        if (wideArgv != nullptr) {
            std::vector<std::string> arguments;
            arguments.reserve(static_cast<std::size_t>(wideCount));
            for (int i = 0; i < wideCount; ++i) {
                arguments.push_back(WideToUtf8(wideArgv[i]));
            }
            ::LocalFree(static_cast<HLOCAL>(static_cast<void*>(wideArgv)));
            return arguments;
        }
#endif
        return {argv, argv + argc};
    }

    void Run(const Options& options, std::ostream& out) {
        Processor processor{
            out,
            IncludeResolver{options.quoteSearchDirs, options.systemSearchDirs},
            InliningFilter{options.quoteFilters, options.systemFilters},
            options.lineDirectives,
            options.errorHandling
        };

        for (const std::filesystem::path& sourceFile : options.sourceFiles) {
            processor.Process(sourceFile);
        }

        out.flush();
        if (!out) {
            Fail("Failed writing to output");
        }
    }

} // namespace

int main(int argc, char** argv) {
    // Installed before anything can fail, so that even a parse error reaches stderr rather
    // than spdlog's default sink -- which writes to stdout, where the amalgamation goes.
    InitLogging(spdlog::level::warn);

    try {
        const std::optional<Options> options = ParseCommandLine(CollectArguments(argc, argv));
        if (!options.has_value()) {
            return 0; // --help or --version, already served
        }
        SetLogLevel(options->logLevel);

        if (options->outputFile.has_value()) {
            const std::string name = Paths::ToUtf8(*options->outputFile);
            spdlog::info("Writing to {}", name);

            std::ofstream out(*options->outputFile, std::ios::binary);
            if (!out) {
                Fail("Failed to open output file \"{}\"", name);
            }
            Run(*options, out);
        } else {
            spdlog::info("Writing to standard output");
#if defined(_WIN32)
            // Otherwise every '\n' written becomes "\r\n" and the amalgamation stops being
            // a byte-for-byte copy of its inputs.
            ::_setmode(::_fileno(stdout), _O_BINARY);
#endif
            Run(*options, std::cout);
        }
        return 0;
    } catch (const UsageError& error) {
        // Already formatted, usage text and all; routing it through the logger would only
        // bury it behind a level prefix.
        std::cerr << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        spdlog::error("{}", error.what());
        return 1;
    }
}
