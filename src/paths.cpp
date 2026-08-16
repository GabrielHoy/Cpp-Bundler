#include "paths.hpp"

namespace Cpp_Bundler::Paths {

    namespace {

        /// `char8_t` and `char` share a representation, so this is a reinterpretation
        /// rather than a conversion -- which is the point: no code page is consulted.
        [[nodiscard]]
        std::string FromU8(std::u8string_view text) {
            return std::string{reinterpret_cast<const char*>(text.data()), text.size()};
        }

    } // namespace

    std::filesystem::path FromUtf8(std::string_view text) {
        return std::filesystem::path{std::u8string_view{reinterpret_cast<const char8_t*>(text.data()), text.size()}};
    }

    std::string ToUtf8(const std::filesystem::path& path) {
        return FromU8(path.u8string());
    }

    std::string ToGeneric(const std::filesystem::path& path) {
        std::string text = FromU8(path.generic_u8string());

        // `std::filesystem::canonical` can hand back an extended-length path on Windows.
        // That prefix is an artefact of the Win32 API's 260-character limit, not something
        // anyone wants to write a glob against or read in a #line directive.
        constexpr std::string_view EXTENDED_LENGTH_PREFIX = "//?/";
        if (text.starts_with(EXTENDED_LENGTH_PREFIX)) {
            text.erase(0, EXTENDED_LENGTH_PREFIX.size());
        }
        return text;
    }

} // namespace Cpp_Bundler::Paths
