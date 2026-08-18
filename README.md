# C++ Source Code Bundler

Combines C++ source files in addition to the headers they include into a single output
file: Think of a similar idea to `esbuild` or `rollup` bundlers in JavaScript/TypeScript.

```shell
cpp-bundler [options] source-files...
```

```shell
# Inline everything reachable from ./include, write one file
cpp-bundler src/main.cpp -d include -o bundle.cpp

# Keep the STL as #include lines, inline only your own headers
cpp-bundler src/main.cpp -d include -d /usr/include --filter '/usr/include/**'

# Keep line numbers debuggable, outputting #line directives in the bundled code
cpp-bundler src/main.cpp -d include --line-directives -o bundle.cpp

# Don't bundle anything: just list which files a bundle would have been built from
cpp-bundler src/main.cpp -d include --list-includes
```

### How do we decide what to inline?

Search directories are **not** implied: without at least one `-d`, nothing is inlined and
every `#include` is copied through untouched. `-d` adds a directory for both include forms;
`--dir-quote` and `--dir-system` restrict it to `#include "…"` or `#include <…>`
respectively. Directories are searched in the order they appear on the command line,
regardless of which of the three options introduced them. For `#include "…"`, the including
file's own directory is searched first, as a compiler would.

Every header is emitted **at most once**, as though it carried `#pragma once`. Identity is
decided by canonical path, so two spellings of one file (or a symlink and its target) 
count as the same header. Any `#pragma once` encountered is stripped since several
compilers warn about one in a non-header.

### CLI Options

| Option | Effect |
|---|---|
| `-o`, `--output <file>` | Write to a file instead of standard output |
| `-d`, `--dir <dir>` | Add a search directory for both include forms |
| `--dir-quote`, `--dir-system <dir>` | Add a search directory for one include 'form' only |
| `-f`, `--filter <glob>` | Exclude matching headers from inlining; `!` re-includes |
| `--filter-quote`, `--filter-system <glob>` | As `--filter`, for one include form only |
| `--line-directives` | Emit `#line` directives mapping output back to the originals |
| `-l`, `--list-includes` | List the files that *would* be bundled instead of bundling them |
| `--unresolvable-include <handling>` | `error`, `warn`, or `ignore` (default) |
| `--unresolvable-quote-include`, `--unresolvable-system-include` | As above, per include form |
| `--cyclic-include <handling>` | `error` (default), `warn`, or `ignore` |
| `-v`, `--verbose` / `-q`, `--quiet` | Repeatable; warnings and errors are the baseline |

### Listing instead of bundling

`--list-includes` (`-l`) runs the exact same walk — the same search directories, the same
filters, the same once-only rule, the same error policies — but writes one `#include` line
naming each file rather than that file's contents:

```shell
$ cpp-bundler src/main.cpp -d src --list-includes
#include "/home/you/proj/src/main.cpp"
#include "/home/you/proj/src/cli.hpp"
#include "/home/you/proj/src/filter.hpp"
#include "/home/you/proj/src/glob.hpp"
```

Paths are canonical and spelled with forward slashes, the same way `#line` directives spell
them, so every line is valid C++ on every platform. Anything that would not have reached the
bundle — a filtered header, an unresolvable include, a cycle's second visit — is absent, and
a header pulled in from two places is named once, where it first appears.

The order is the order files are **entered**, which is the order their contents *begin* in
the bundle. That is not quite the byte order of the amalgamation: a header is listed before
the headers it itself includes, whereas in the bundle those nested contents are emitted first
because the `#include` line comes before the rest of the file. Think of it as the include
tree flattened depth-first, not a topological sort of dependencies.

Because a bundle has no lines to map back, `--list-includes` cannot be combined with
`--line-directives`. Everything else, including `-o`, works as usual.

### Filtering

Globs are matched against the **full canonical path** of a resolved header, so they almost
always want a leading `**/`. Globs are evaluated in the order given and the **last match**
wins, which is what makes 'exclude-then-re-include' work:

```shell
cpp-bundler main.cpp -d . -f '**' -f '!**/mylib/**'   # inline only headers from "mylib"
```

| Pattern | Matches |
|---|---|
| `**` | every path (**special case**: see below!) |
| `**/*.hpp` | any `.hpp`, at any depth |
| `/usr/local/include/**` | everything below that directory |
| `a/**/b` | `a/b`, `a/x/b`, `a/x/y/b` |
| `src/*.hpp` | `.hpp` **directly** under `src`, not in a subdirectory |
| `[a-c]?.hpp`, `*.{hpp,hxx}` | character classes and alternation |

`*` and `?` never cross a `/`; `**` does, but only where it stands alone as a whole path
component (`a**b` is just `a*b`). A pattern of exactly `**` matches everything - read
strictly it would mean "whole components and nothing else", which no path satisfies, so it
is special-cased the way every glob library special-cases it.

On Windows, matching ignores case and accepts `\` as a separator, so patterns behave the way
the filesystem does.

### Limitations

cpp-bundler is *currently* a **text** tool and ***not a compiler.*** It resolves `#include` and strips
`#pragma once`; it does not evaluate the preprocessor, parse C++, or understand linkage.
Limitations brought on by this are described below.

**`#if` directives are not evaluated.** An `#include` inside a disabled block is inlined anyway,
and worse, that marks the header as already included so a later "live" `#include` of it is "dropped":

```cpp
#if 0
    // Inlined here as zombie code
    #include <a.hpp>
#endif

// Dropped: "already included"
#include <a.hpp>
```

The same applies to a header pulled in under two different `#if` branches: it is expanded in
the first and removed from the second. Traditional `#if`-based header guards are likewise
invisible, so every header is treated as include-once whether or not it says so - which
breaks headers designed for repeated inclusion, `<assert.h>` and X-macro headers especially.

**It inlines wherever the directive sits.** An `#include` inside a namespace or function
body is expanded there, and every later `#include` of that header is then dropped - so the
other users get nothing, while the contents sit nested where the first one put them.

Both of these produce *compile errors*, not silently wrong programs. That is the saving
grace: the failure is loud.

**Multiple `.cpp` files are a unity build.** Source files are concatenated in the order
given, which is exactly what a jumbo/unity build does - and it inherits the same failure
modes. It works when the translation units were written to tolerate it:

| Hazard | What happens |
|---|---|
| Two `main()` definitions | Redefinition error |
| `static` globals or anonymous-namespace helpers sharing a name | All anonymous namespaces in one TU are *the same* namespace, so they collide |
| `#define` without a matching `#undef` | Leaks into every following file - can silently change meaning rather than erroring |
| `using namespace` at file scope | Leaks forward; may cause ambiguity or quietly pick different overloads |
| `#pragma pack`, `#pragma warning(push)` | Leak across the file boundary |
| Static initialisation order | Previously unordered globals become ordered, which can mask or expose bugs |

The macro and `using namespace` rows are the dangerous ones, because they can change
behaviour without any diagnostic.

**It cannot resolve what it cannot see literally.** `#include MY_HEADER` (macro-built),
`#include_next`, `__has_include`, and includes split across a line continuation are all
copied through untouched.

**It is not a linker.** It concatenates text; it does not archive objects, drop unreferenced
code, or reorder anything to satisfy declaration order.

---

## Requirements

| Tool | Notes |
|---|---|
| CMake | ≥ 3.25 (presets v6, `--workflow` support) |
| Ninja | Bundled with Visual Studio, or `winget install Ninja-build.Ninja` |
| vcpkg | Cloned anywhere; `VCPKG_ROOT` must point at it |
| MSVC or clang-cl | Any Visual Studio 2022+ toolset |
| sccache | Optional. Used automatically when found on `PATH` |

> **Note:** `VCPKG_ROOT` **must be set** on your system.
---

## Quick start

Every preset supports a 'one-shot' workflow that configures, builds, tests, and - for release
presets - packages the results.

It even puts it in wrapping paper with a pretty little bow on top for you:

```powershell
cmake --workflow --preset ninja-debug
```
*(I lied about the wrapping paper. Sorry.)*

You can also of course drive the steps individually:

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
CMake's "the compiler has changed" cache abort and sinks time.


## Formatting and Linting

`.clang-format` should have **clang-format 20+** to work fully with the codebase's formatting preferences if you intend to contribute. Please ensure you conform to them for the sake of consistency.

`.clangd` is regenerated at configure time from `.clangd.in`, pointed at the build directory
you most recently configured. Configuring a different preset should point your editor's index
at that preset's compilation database for you.

---

## Attribution

This project is admittedly **heavily** inspired by the [**cpp-amalgamate**](https://github.com/Felerius/cpp-amalgamate) program, created by
David Stangl. `cpp-bundler`'s CLI interface and its observable behaviour are kept compatible with `cpp-amalgamate` for simplicity of use by people familiar with that project; however the internal implementation is new of course and contains several improvements.

## License

We use the [MIT license.](LICENSE)
