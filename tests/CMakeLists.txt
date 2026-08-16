# Catch2 arrives through the "tests" feature in vcpkg.json, which CMakePresets.json
# activates via VCPKG_MANIFEST_FEATURES.
find_package(Catch2 3 CONFIG REQUIRED)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(${PROJECT_NAME}_tests ${TEST_SOURCES})

target_link_libraries(${PROJECT_NAME}_tests PRIVATE
    ${PROJECT_NAME}::core
    # Catch2WithMain supplies main(); use Catch2::Catch2 alone if you need your own.
    Catch2::Catch2WithMain
    project_warnings
)

# Catch2's config package appends its `extras` directory to CMAKE_MODULE_PATH, which is
# what makes this include() resolve.
# The bundler recurses once per level of #include nesting; see the comment on the same
# option in the top-level CMakeLists.txt.
if(WIN32 AND MSVC)
    target_link_options(${PROJECT_NAME}_tests PRIVATE /STACK:8388608)
endif()

include(Catch)

# Registers every TEST_CASE as an individual CTest test by interrogating the built binary,
# so `ctest -R <name>` and parallel execution work without hand-maintaining a list.
catch_discover_tests(${PROJECT_NAME}_tests)
