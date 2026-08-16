cmake_minimum_required(VERSION 3.25)

# vcpkg's toolchain file is injected by CMakePresets.json via $env{VCPKG_ROOT}, if that
# environment variable is unset then the path becomes invalid and throws opaque errors,
# so we surface that problem here first&foremost before that happens
if(CMAKE_TOOLCHAIN_FILE AND NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR
        "CMAKE_TOOLCHAIN_FILE points at a file that does not exist:\n"
        "    ${CMAKE_TOOLCHAIN_FILE}\n"
        "This almost always means the VCPKG_ROOT environment variable is unset.\n"
    )
endif()

project(CppBundler
    VERSION 0.1.0.0
    LANGUAGES CXX C
)

# === CMake Options ===

# Toggles that reshape the entire build tree are only ours to set when we're the top-level
# project. A parent that embeds us via add_subdirectory owns those decisions instead.
if(PROJECT_IS_TOP_LEVEL)
    # Whether to treat compiler warnings as errors or not.
    # CMAKE_COMPILE_WARNING_AS_ERROR (CMake 3.24+) is preferred over hardcoding /WX and
    # -Werror because `cmake --build ... --compile-no-warning-as-error` can override it,
    # which is invaluable when a dependency bump breaks CI.
    option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
    set(CMAKE_COMPILE_WARNING_AS_ERROR ${WARNINGS_AS_ERRORS})

    # We'll utilize sccache caches compiler invocations to speed up rebuilds,
    # if this is enabled we'll try to use it if it's present on the system.
    option(USE_SCCACHE "Use sccache for compiler invocation caching" ON)

    # Collect every executable in one directory so the test binary resolves the same
    # dependency DLLs that vcpkg copies next to the application.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

option(BUILD_TESTING "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(ENABLE_ASAN "Build with AddressSanitizer instrumentation" OFF)
option(ENABLE_IPO "Enable interprocedural optimization (LTO) in optimized configs" ON)

# Project-scoped configuration. Defined unconditionally, because our own targets read it.
set(EXECUTABLE_NAME ${PROJECT_NAME} CACHE STRING "The name of the outputted application's executable.")
set(WIN32_APP_DESCRIPTION "A tool to bundle multi-file C++ source(s), similar to bundling JS/TS files." CACHE STRING "A Win32-specific description field for the application.")
set(WIN32_APP_COMMENTS "" CACHE STRING "A Win32-specific comment field for the application.")

# === C++ Configuration ===

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(USE_SCCACHE)
    # Try to find sccache on the system and use it if it's present.
    find_program(SCCACHE_EXECUTABLE sccache)
    if(SCCACHE_EXECUTABLE)
        message(STATUS "Utilizing sccache for compiler invocation caching")
        set(CMAKE_C_COMPILER_LAUNCHER   "${SCCACHE_EXECUTABLE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_EXECUTABLE}")
        # sccache can't cache /Zi debug symbols ("PDB side-channel via mspdbsrv"),
        # so we'll force /Z7 instead (embedded PDBs)
        if(MSVC)
            set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
        endif()
    endif()
endif()

# find_package(<LIBRARY_NAME> CONFIG REQUIRED)

# === Shared compile settings ===

# An INTERFACE target is how you share flags across targets without a variable soup.
# Consumers opt in with target_link_libraries(<target> PRIVATE project_warnings); nothing
# leaks into dependencies, and it composes with generator expressions.
add_library(project_warnings INTERFACE)

if(MSVC)
    # Careful: MSVC is true for any compiler with an MSVC-like command line, clang-cl
    # included. Flags that only cl.exe needs must be gated on the compiler ID instead,
    # otherwise clang-cl reports them as unused and WARNINGS_AS_ERRORS turns that fatal.
    target_compile_options(project_warnings INTERFACE
        /W4 /permissive- /EHsc
        /utf-8          # source AND execution charset are UTF-8, not the system ACP
        /Zc:__cplusplus # without this __cplusplus reports 199711L forever
        # Conforming preprocessor (__VA_OPT__, correct __VA_ARGS__). clang-cl's is already
        # conforming and rejects the flag as unused.
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
    )
else()
    target_compile_options(project_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
endif()

if(ENABLE_ASAN)
    # Note: this path assumes MSVC's cl.exe on Windows. clang-cl rejects /fsanitize=address
    # together with the debug CRT, and forcing the release CRT instead would mismatch the
    # debug-built dependencies vcpkg's x64-windows triplet produces.
    if(MSVC)
        # ASan and MSVC's runtime checks (/RTC1, injected into CMAKE_CXX_FLAGS_DEBUG by
        # default) are mutually exclusive; shadowing the cache entry strips them.
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        target_compile_options(project_warnings INTERFACE /fsanitize=address)
        # ASan is incompatible with incremental linking.
        target_link_options(project_warnings INTERFACE /INCREMENTAL:NO)
        # MSVC's STL stamps `#pragma detect_mismatch("annotate_vector", "1")` into every
        # instrumented TU, and the linker then refuses to mix it with objects built without
        # ASan. vcpkg ships prebuilt dependencies with no instrumentation, so we opt out of
        # container annotations to match them. This forfeits container-overflow detection
        # only; heap, stack and use-after-free checking are unaffected. To get annotations
        # back, rebuild your dependencies with ASan via a custom vcpkg triplet.
        target_compile_definitions(project_warnings INTERFACE
            _DISABLE_STRING_ANNOTATION
            _DISABLE_VECTOR_ANNOTATION
            _DISABLE_OPTIONAL_ANNOTATION
        )
    else()
        target_compile_options(project_warnings INTERFACE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(project_warnings INTERFACE -fsanitize=address)
    endif()
    message(STATUS "AddressSanitizer enabled")
endif()

# === Targets ===

# Everything except main.cpp lives in a static library so the test suite has something to
# link against; an executable exposes no linkable artifact and its main() would collide.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

if(NOT CORE_SOURCES)
    # CMake rejects a library with no sources. Keep the target valid for the window between
    # deleting the template's example code and writing your own.
    set(CORE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/core_placeholder.cpp")
    file(WRITE "${CORE_SOURCES}" "// Generated by CMake: keeps the core library valid while src/ holds only main.cpp.\n")
endif()

add_library(${PROJECT_NAME}_core STATIC ${CORE_SOURCES})
add_library(${PROJECT_NAME}::core ALIAS ${PROJECT_NAME}_core)

target_include_directories(${PROJECT_NAME}_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME}_core PRIVATE project_warnings)

target_compile_definitions(${PROJECT_NAME}_core PUBLIC
    $<$<CONFIG:Debug>:IS_DEBUG=1>
    $<$<CONFIG:Release>:IS_RELEASE=1>
)

add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${EXECUTABLE_NAME})

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${PROJECT_NAME}::core
    project_warnings
)

# target_link_libraries(${PROJECT_NAME}_core PUBLIC
#   #<LIBRARIES TO LINK>
# )

if(WIN32)
    # === Windows-specific settings ===

    # Configure & add the FileMeta.rc file to the project so
    # it gets compiled into the executable and provides
    # metadata accordingly.
    set(APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.ico")
    set(APP_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(APP_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(APP_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(APP_VERSION_TWEAK ${PROJECT_VERSION_TWEAK})
    if(NOT APP_VERSION_TWEAK) # If we're not running in a CI environment, tweak defaults to 0
        set(APP_VERSION_TWEAK 0)
    endif()
    # Composed from the components rather than reusing PROJECT_VERSION directly: that
    # string already contains the tweak when project() declares four, and omits it when it
    # declares three. Building both explicitly is correct either way.
    set(APP_VERSION_STRING "${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH}.${APP_VERSION_TWEAK}")
    set(APP_PRODUCT_VERSION_STRING "${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_PATCH}")
    string(TIMESTAMP APP_COPYRIGHT_YEAR "%Y")
    set(APP_DESCRIPTION "${WIN32_APP_DESCRIPTION}")
    set(APP_COMMENTS "${WIN32_APP_COMMENTS}")

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/win32/FileMeta.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/FileMeta.rc
        @ONLY
    )

    target_sources(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/FileMeta.rc
    )

    if(MSVC)
        # CMake recognises .manifest sources and feeds them to the linker via
        # /MANIFESTINPUT, which merges them with the auto-generated manifest rather than
        # producing a duplicate RT_MANIFEST resource. See assets/win32/app.manifest for
        # what the declarations actually buy us.
        target_sources(${PROJECT_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/win32/app.manifest
        )
    endif()
endif()

if(ENABLE_IPO)
    # check_ipo_supported() runs a real compile+link probe, so this is trustworthy even for
    # the awkward combinations (clang-cl needs lld-link for ThinLTO, for instance).
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_DIAGNOSTIC)
    if(IPO_SUPPORTED)
        set_property(TARGET ${PROJECT_NAME}      PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set_property(TARGET ${PROJECT_NAME}_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(STATUS "IPO/LTO unavailable, continuing without it: ${IPO_DIAGNOSTIC}")
    endif()
endif()

# === Tests ===

if(BUILD_TESTING)
    # enable_testing() has to be invoked from the top-level directory for `ctest` to find
    # anything, so it lives here rather than in tests/CMakeLists.txt.
    enable_testing()
    add_subdirectory(tests)
endif()

# === Install & Packaging ===

include(GNUInstallDirs)

if(POLICY CMP0207)
    # Normalize backslashes to forward slashes before matching the *_EXCLUDE_REGEXES
    # below. Without this, Windows hands back mixed-separator paths like
    # "C:\WINDOWS\system32/kernel32.dll" and exclusion becomes separator-dependent.
    cmake_policy(SET CMP0207 NEW)
endif()

if(WIN32)
    # RUNTIME_DEPENDENCY_SET walks the import table and drags in the transitive DLLs that
    # vcpkg's dynamic triplets produce, so the install tree is actually runnable.
    install(TARGETS ${PROJECT_NAME}
        RUNTIME_DEPENDENCY_SET app_runtime_deps
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    set(APP_DEP_SEARCH_DIRS "")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND APP_DEP_SEARCH_DIRS
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
        )
    endif()

    install(RUNTIME_DEPENDENCY_SET app_runtime_deps
        # api-ms-*/ext-ms-* are API sets; everything under System32 is OS-provided. Neither
        # may be redistributed, and bundling them breaks on other Windows builds.
        PRE_EXCLUDE_REGEXES  "api-ms-" "ext-ms-" "^hvsifiletrust\\.dll$" "^pdmutilities\\.dll$"
        POST_EXCLUDE_REGEXES ".*[/\\\\][Ss]ystem32[/\\\\].*"
        DIRECTORIES ${APP_DEP_SEARCH_DIRS}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
else()
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(PROJECT_IS_TOP_LEVEL)
    set(CPACK_PACKAGE_NAME                "${EXECUTABLE_NAME}")
    set(CPACK_PACKAGE_VENDOR              "Tampered Reality")
    set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${WIN32_APP_DESCRIPTION}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY   "${EXECUTABLE_NAME}")
    set(CPACK_RESOURCE_FILE_LICENSE       "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_VERBATIM_VARIABLES ON)
    # ZIP/TGZ need no external tooling. Swap in NSIS/WIX once you have an installer story.
    if(WIN32)
        set(CPACK_GENERATOR ZIP)
    else()
        set(CPACK_GENERATOR TGZ)
    endif()
    include(CPack)
endif()

# We regenerate `.clangd` from `.clang.in` at configure time so it points to
# the `compile_commands.json` file in the build directory.
set(CLANGD_BUILD_DIR "${CMAKE_BINARY_DIR}")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/.clangd.in
    ${CMAKE_CURRENT_SOURCE_DIR}/.clangd
    @ONLY
)
