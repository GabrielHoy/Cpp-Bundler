# CppBundler

A tool to bundle multi-file C++ source(s), similar to bundling JS/TS files.

---

## Requirements

| Tool | Notes |
|---|---|
| CMake | ≥ 3.25 (presets v6, `--workflow` support) |
| Ninja | Bundled with Visual Studio, or `winget install Ninja-build.Ninja` |
| vcpkg | Cloned anywhere; `VCPKG_ROOT` must point at it |
| MSVC or clang-cl | Any Visual Studio 2022+ toolset |
| sccache | Optional. Used automatically when found on `PATH` |

> **`VCPKG_ROOT` must be set.** The presets reference `$env{VCPKG_ROOT}`; if it's unset the
> toolchain path collapses to something nonsensical and the configure step fails with an
> explicit message pointing back here.
>
> On Windows, `cmake.useVsDeveloperEnvironment: "always"` (in `.vscode/settings.json`)
> causes VS to inject *its own* bundled vcpkg as `VCPKG_ROOT`, overriding yours. That is
> harmless — `builtin-baseline` in `vcpkg.json` pins port versions, so any clone resolves
> to identical dependencies.

---

## Quick start

Every preset supports a one-shot workflow that configures, builds, tests, and (for release
presets) packages:

```powershell
cmake --workflow --preset ninja-debug
```

Or drive the steps individually:

```powershell
cmake --preset ninja-debug          # configure
cmake --build --preset ninja-debug  # build
ctest --preset ninja-debug          # test
cpack --preset ninja-release        # package -> dist/
```

### Presets

| Preset | Toolchain | Purpose |
|---|---|---|
| `ninja-debug` | MSVC | Day-to-day development |
| `ninja-release` | MSVC | Optimized + LTO, packaged to `dist/` |
| `ninja-debug-clang-cl` | clang-cl | Cross-check with a second front end |
| `ninja-release-clang-cl` | clang-cl | Optimized clang-cl build |
| `ninja-asan` | MSVC | AddressSanitizer instrumented |

Each preset gets its own `build/<preset>` directory, so switching compilers never trips
CMake's "the compiler has changed" cache abort.

---

## Layout

```
src/            Application sources. Everything except main.cpp becomes <project>_core.
tests/          Catch2 test suite, linked against <project>_core.
assets/
  AppIcon.ico       Icon embedded into the executable.
  win32/
    FileMeta.rc.in  Version resource template (configured at build time).
    app.manifest    Win32 application manifest.
.github/workflows/  CI: build/test matrix plus a clang-format check.
```

### Why the core library exists

A test binary cannot link an executable — there's no linkable artifact and `main()` would
collide. So `src/*.cpp` minus `main.cpp` builds into a static library
`<project>_core` (aliased `<project>::core`); the application is `main.cpp` plus that
library, and the tests link the same library. **Put your logic in `src/`, keep `main.cpp`
as a thin entry point, and it's testable for free.**

---

## Configuration

Cache options, all settable via `-D` or a preset:

| Option | Default | Effect |
|---|---|---|
| `EXECUTABLE_NAME` | `${PROJECT_NAME}` | Output binary name, independent of the project name |
| `WARNINGS_AS_ERRORS` | `ON` | Sets `CMAKE_COMPILE_WARNING_AS_ERROR` |
| `USE_SCCACHE` | `ON` | Use sccache when present |
| `BUILD_TESTING` | `ON` when top-level | Build and register the Catch2 suite |
| `ENABLE_ASAN` | `OFF` | AddressSanitizer instrumentation |
| `ENABLE_IPO` | `ON` | LTO in optimized configurations |
| `WIN32_APP_DESCRIPTION` | *(see CMakeLists)* | `FileDescription` in the version resource |
| `WIN32_APP_COMMENTS` | *(empty)* | `Comments` in the version resource |

A one-off override without editing anything:

```powershell
cmake --build --preset ninja-release --compile-no-warning-as-error
```

### Adding a dependency

1. Add it to `dependencies` in `vcpkg.json`.
2. `find_package(<Name> CONFIG REQUIRED)` in `CMakeLists.txt`.
3. `target_link_libraries(${PROJECT_NAME}_core PUBLIC <Name>::<Target>)`.

Re-configuring installs it. Version resolution is pinned by `builtin-baseline`; bump that
SHA to move dependencies forward deliberately rather than by accident.

---

## Notes and known tradeoffs

**Globbed sources.** `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` is used deliberately, against
CMake's own recommendation. `CONFIGURE_DEPENDS` makes Ninja and Make re-glob on every build
so new files are picked up automatically; the cost is a stat sweep per build and no
guarantee on other generators. Swap in an explicit source list if that tradeoff stops being
worth it.

**sccache forces `/Z7`.** sccache cannot cache objects built with `/Zi`, because MSVC writes
the PDB out-of-band through `mspdbsrv`. When sccache is detected, debug configurations
switch to embedded debug info so caching actually happens.

**ASan disables STL container annotations.** MSVC's STL marks instrumented translation units
with `#pragma detect_mismatch`, and the linker refuses to mix them with vcpkg's
uninstrumented prebuilt packages. `_DISABLE_{STRING,VECTOR,OPTIONAL}_ANNOTATION` are defined
to match. Heap, stack and use-after-free detection are unaffected; only container-overflow
detection is lost. Build your dependencies with a custom ASan triplet to get it back.

**ASan uses MSVC, not clang-cl.** clang-cl's ASan cannot link the debug CRT, and switching
to the release CRT would mismatch vcpkg's debug-built dependencies.

**Runtime dependencies.** The Windows install rules use
`install(RUNTIME_DEPENDENCY_SET ...)` to bundle transitive DLLs from vcpkg's dynamic
triplets, excluding API sets and anything under `System32`.

---

## Formatting and linting

`.clang-format` requires **clang-format 20+** (it uses `WrapNamespaceBodyWithEmptyLines`,
`BinPackParameters: OnePerLine` and `ReflowComments: Always`). Formatting output drifts
between major versions, so CI constrains itself to one: `pipx install "clang-format~=22.1"`.
Keep that constraint in sync with the clang-format you run locally — Visual Studio's is at
`VC\Tools\Llvm\x64\bin\clang-format.exe` — or CI will disagree with your editor.

`.clang-tidy` is picked up automatically by clangd, so its checks appear as editor
diagnostics with no extra configuration. It's advisory by default — set `WarningsAsErrors`
in that file once a project is clean enough to enforce it.

`.clangd` is regenerated at configure time from `.clangd.in`, pointed at the build directory
you most recently configured. Configuring a different preset repoints your editor's index
at that preset's compilation database.

---

## License

MIT — see [LICENSE](LICENSE).
