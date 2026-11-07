# AGENTS.md

Rules for any change to vcd-writer, a C++20 library that writes Value Change Dump files (IEEE 1364-2005, §18.2).

## Code

- Standard C++20 only. No platform-dependent code in the library. Compiler-specific flags live in CMake only.
- The standard library instead of third-party dependencies (GoogleTest is used by the tests only).
- Character checks are ASCII and locale-independent (no `std::tolower` / `std::isgraph`):
  VCD identifiers are printable ASCII, and the result must not depend on the locale set by the host application.

### Naming

- C++ only: everything is in `namespace vcd`, so the names have no `VCD` prefix
  (`vcd::Writer`, `vcd::Variable`, `vcd::Header`; the pimpl struct is `Impl`).
- Types are UpperCamelCase.
- Enumerators are lowercase everywhere.
- Files are named `vcd_<purpose>.h` / `vcd_<purpose>.cpp`.

### Public interface (`include/`)

- The public header contains the API only: the writer's state is hidden behind pimpl.
- Minimal includes: only what the API declarations themselves need.
- Every API entity is documented with Doxygen (`@brief`, `@param`, `@return`, `@throws`).
- Every enumeration ends with `_count_`: the number of values (the first invalid one).
  Name tables and range checks are sized by it and the tests use it too: no magic numbers,
  so changing an enumeration needs no changes in the code or the tests.
  It is lowercase because `_COUNT_` is a reserved identifier.

### Source layout (`src/`)

- Code is split by meaning: each class in its own files; headers expose the minimum data needed.
- General-purpose helpers live in `vcd_utils` only.
- No method longer than one line inside a class definition: longer ones are defined outside the class.
- A type with only public members is a `struct` (exceptions included).
- No redundant includes and no redundant method definitions (e.g. forwarders "method → the same `Impl` method").
- No parameters or settings that do not affect the result.

### Behavior

- The output conforms to IEEE 1364-2005 §18.2: in particular `$dumpvars`, `$dumpall`, `$dumpon`, `$dumpoff`
  always end with `$end`, and `$scope` / `$upscope` are balanced.
- The output is byte-identical on every platform: the file is opened in binary mode (LF),
  the output order is deterministic (never depends on hash table iteration).
- All input is validated before any state change: a failed call leaves no trace.
- Destructors never throw.
- The `change()` hot path does not allocate memory in a steady state and does not copy `shared_ptr`.
  Changes that may affect performance come with `bench/vcd_bench.cpp` results before and after.

## Tests

- Every bug fix comes with a regression test.
- Every test writes its own file (`test_filename()`); tests do not depend on each other.
- Reference output `compare.vcd`: `main_exec` must produce a byte-identical `dump.vcd` on every OS.
- Tests pass with no compiler warnings and under ASan + UBSan (`-DVCDWRITER_SANITIZE=ON`).
- Before a PR, check locally with at least MSVC and clang/libc++ (or GCC ≥ 13); the PR description states
  what was checked locally and what is covered by CI only.

## Build

- Both builds are supported: CMake and Makefile.
- Warnings are enabled (`/W4` for MSVC, `-Wall -Wextra -Wpedantic` for the others) and there must be none.
- CMake options: `VCDWRITER_BUILD_MAIN`, `VCDWRITER_BUILD_TESTS`, `VCDWRITER_BUILD_BENCH`, `VCDWRITER_SANITIZE`.

## CI (GitHub Actions)

- Runners and compilers are `*-latest`: latest is not older than required (GCC ≥ 13, Clang/libc++ ≥ 17, MSVC ≥ 19.29).
- Ready-made actions instead of hand-written scripts.
- Minimal token permissions, granted at the level that needs them, and only the ones needed.
- Triggers are written explicitly, so they are clear from the text, or with a comment.
- A tag and a GitHub Release with the Release binaries (header, static and shared libraries for Linux,
  Windows, macOS) are made only for a push to `master`, after all tests passed and the tag was created.

## Git and PRs

- Changes go through pull requests; merging requires the owner's review and approval.
- One PR, one topic (build/dependencies, fixes, performance, refactoring, docs). Dependent PRs are stacked.
- Commit messages and PR titles follow Conventional Commits (`fix:`, `feat:`, `perf:`, `refactor:`, `ci:`, `docs:`):
  github-tag-action picks the next version from them (`feat:` is minor, `BREAKING CHANGE` is major, otherwise patch).
- No force-push: merge instead of rebase.
- The repository allows squash and rebase merges only. After the base PR is squash-merged, a stacked PR conflicts:
  merge `master` into its branch keeping the branch side (`git checkout --ours`) and push.
- A PR description has: purpose, what changed, breaking changes (⚠️), tests, verification (locally / CI only);
  benchmark results for performance changes.
