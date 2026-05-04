# lite-xl-ghostty MVP Implementation Plan

Last updated: 2026-05-02

## Objective

Implement `specs/mvp.md` as a source-buildable Lite XL plugin backed by Ghostty's `libghostty-vt`, with durable progress, logical commits, and focused tests along the way.

## Checklist

- [x] Read MVP spec and initial repository state.
- [x] Create durable implementation plan.
- [ ] Add repository scaffolding:
  - [x] `manifest.json`
  - [x] `build.sh`
  - [x] `CMakeLists.txt`
  - [x] `src/` native module skeleton
  - [x] `plugins/ghostty/` Lua plugin skeleton
  - [x] focused unit test harness
- [ ] Implement native support modules:
  - [x] event queue
  - [x] UTF-8 helpers
  - [x] streaming OSC observer with OSC 9 and OSC 52 parsing
  - [x] POSIX PTY spawn/resize/cleanup
  - [x] terminal object lifecycle and synchronization
  - [x] Lua binding glue
- [ ] Implement native terminal behavior:
  - [x] `forkpty` process startup from argv or explicit shell string
  - [x] reader thread draining PTY into Ghostty VT
  - [x] Ghostty terminal/render-state creation
  - [x] render snapshot rows/spans
  - [x] title/cwd/bell/exit events
  - [x] input text, key, mouse, focus, paste, scroll APIs
  - [x] child cleanup and reader thread shutdown
- [ ] Implement Lua plugin behavior:
  - [x] config defaults
  - [x] pub/sub events API
  - [x] drawer and tab commands
  - [x] `TerminalView` lifecycle, resize, update, draw
  - [x] keyboard fallback routing and paste policy
  - [x] mouse routing, scrollback, focus reporting
  - [x] OSC 52 ask/allow/deny policy
  - [x] click-to-open URL/path/OSC8 workflow
  - [x] visible-grid selection and copy
  - [x] agent terminal constructor/command
- [x] Build and distribution:
  - [x] CMake drives Ghostty `zig build lib-vt`
  - [x] native module links Ghostty VT statically
  - [x] `build.sh` places native library next to `plugins/ghostty/init.lua`
  - [x] README documents build, install, usage, and limitations
- [ ] Tests and verification:
  - [x] C unit tests for event queue
  - [x] C unit tests for OSC parser fragmentation/overflow/OSC9/OSC52
  - [x] C unit tests for UTF-8 helpers
  - [ ] C unit tests for command option validation where practical
  - [x] Lua unit tests for events, click-to-open parsing, selection helpers, key fallbacks
  - [x] native build succeeds
  - [x] unit tests pass
  - [ ] Lite XL load smoke test if feasible
  - [x] completion audit against `specs/mvp.md`
- [x] Make logical commits as milestones are completed.

## Key Decisions And Assumptions

- The repository is greenfield except for `README.md`, `specs/mvp.md`, and reference checkouts.
- Implementation will keep Ghostty VT as the only parser; any pure-Lua/native fallback code is policy, parsing support, or UI glue.
- Unit tests will start with modules that do not require Lite XL or a full Ghostty link, then expand as build details settle.
- Subagents are being used for independent reference analysis: Ghostty VT C APIs, Lite XL plugin patterns, and Ghostty/CMake build integration.
- Build integration will use Ghostty's CMake wrapper and `ghostty-vt-static`; the plugin module output remains `plugins/ghostty/libghostty_lxl.{lib,so}`.
- Lite XL native entrypoint should be `luaopen_lite_xl_libghostty_lxl(lua_State *L, void *XL)`, with a standalone Lua entrypoint useful for tests.

## Surprises And Observations

- `execplans/mvp-implementation.md` did not exist initially.
- `references/ghostty`, `references/ghostling`, and `references/lite-xl-terminal` are present in the workspace.
- The MVP is broad; initial commits should establish buildable/testable slices rather than a single large integration.
- `cmake`, `ninja`, and `zig@0.15` were installed via Homebrew after the initial environment lacked the required build tools.
- `/Applications` did not expose `lite_xl_plugin_api.h` in a simple file search, so the Lite XL 2.1.7 plugin API header was vendored.
- The default Zig remains `0.16.0`, but `references/ghostty` requires Zig `0.15.2`; use `PATH=/opt/homebrew/opt/zig@0.15/bin:$PATH` for builds.

## Verification Log

- 2026-05-02: `cmake -S . -B build-support -G Ninja -DLXL_GHOSTTY_BUILD_NATIVE=OFF -DLXL_GHOSTTY_BUILD_TESTS=ON` could not run because `cmake` is not installed.
- 2026-05-02: `cc -std=c11 -Wall -Wextra -Werror -pthread -Isrc src/event_queue.c src/osc_observer.c src/utf8.c tests/test_event_queue.c -o /tmp/lxl_test_event_queue` passed.
- 2026-05-02: `/tmp/lxl_test_event_queue` passed.
- 2026-05-02: `cc -std=c11 -Wall -Wextra -Werror -pthread -Isrc src/event_queue.c src/osc_observer.c src/utf8.c tests/test_osc_observer.c -o /tmp/lxl_test_osc_observer` passed.
- 2026-05-02: `/tmp/lxl_test_osc_observer` passed.
- 2026-05-02: `cc -std=c11 -Wall -Wextra -Werror -pthread -Ireferences/ghostty/include -Isrc -c src/pty_unix.c -o /tmp/lxl_pty_unix.o` passed.
- 2026-05-02: `cc -std=c11 -Wall -Wextra -Werror -pthread -Ireferences/ghostty/include -Isrc -c src/terminal.c -o /tmp/lxl_terminal.o` passed.
- 2026-05-02: Lua binding compile not verified because `lite_xl_plugin_api.h` and local Lua headers were not found under the repo, `/Applications`, Homebrew include paths, or system include paths.
- 2026-05-02: Re-ran event queue and OSC observer direct `cc` builds/tests after native skeleton changes; both passed.
- 2026-05-02: Added Lua plugin skeleton and `manifest.json`. Lua syntax/runtime verification is blocked because no `lua`, `luac`, or `luajit` executable is installed in the environment.
- 2026-05-02: Re-ran strict direct C compiles for `terminal.c` and `pty_unix.c`, plus event queue and OSC observer tests; all passed.
- 2026-05-02: Implemented native render snapshot row/span extraction and native key encoder path for basic key events. Strict direct compile for `terminal.c` still passes; `lxl_ghostty.c` remains uncompiled due missing Lua/Lite XL headers.
- 2026-05-02: Re-ran event queue and OSC observer tests; both passed.
- 2026-05-02: Implemented native mouse encoder path, OSC 8 hyperlink lookup via Ghostty grid refs, and Lua OSC 52 ask/allow/deny policy. Strict direct compile for `terminal.c` and support tests still pass.
- 2026-05-02: Vendored Lite XL 2.1.7 `lite_xl_plugin_api.h` from upstream so source builds do not require discovering the header in `/Applications`.
- 2026-05-02: `cc -std=c11 -Wall -Wextra -Werror -Wno-error=ignored-attributes -pthread -Ilib/lite-xl/resources/include -Ireferences/ghostty/include -Isrc -c src/lxl_ghostty.c -o /tmp/lxl_ghostty.o` passed with one warning from the vendored Lite XL header.
- 2026-05-02: Re-ran strict direct compiles for `terminal.c`, `pty_unix.c`, event queue test, and OSC observer test; both tests passed.
- 2026-05-02: `zig build -Demit-lib-vt` initially failed due sandboxed Zig cache permissions; rerun with approval reached the actual build error: installed Zig `0.16.0` does not satisfy Ghostty's required `0.15.2`.
- 2026-05-02: `cc -std=c11 -Wall -Wextra -Werror -pthread -Isrc src/utf8.c tests/test_utf8.c -o /tmp/lxl_test_utf8` passed.
- 2026-05-02: `/tmp/lxl_test_utf8` passed.
- 2026-05-02: Final direct verification rerun passed: `lxl_ghostty.c` object compile, `terminal.c` object compile, `pty_unix.c` object compile, event queue test, OSC observer test, and UTF-8 test. `lxl_ghostty.c` still emits the known vendored-header `-Wignored-attributes` warning.
- 2026-05-02: Installed Homebrew `cmake` 4.3.2, `ninja` 1.13.2, and `zig@0.15` 0.15.2.
- 2026-05-02: `env PATH=/opt/homebrew/opt/zig@0.15/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin ./build.sh` succeeded and produced `plugins/ghostty/libghostty_lxl.dylib`.
- 2026-05-02: Verified artifacts: `plugins/ghostty/libghostty_lxl.dylib` is a Mach-O arm64 bundle; `references/ghostty/zig-out/lib/libghostty-vt.a` is present.
- 2026-05-02: Initial CTest run failed because `build.sh` only built `ghostty_lxl`, leaving test executables unbuilt. `build.sh` was changed to build the default target set.
- 2026-05-02: CTest then exposed Release-mode test bugs caused by `assert(...)` calls with side effects under `NDEBUG`; tests were changed to always-on `CHECK(...)`.
- 2026-05-02: `cmake --build build` succeeded.
- 2026-05-02: `ctest --test-dir build --output-on-failure` passed: 3/3 tests.
- 2026-05-02: `otool -L plugins/ghostty/libghostty_lxl.dylib` shows only `/usr/lib/libSystem.B.dylib`, consistent with static Ghostty linkage.
- 2026-05-02: `nm -gU plugins/ghostty/libghostty_lxl.dylib` shows `_luaopen_lite_xl_libghostty_lxl`.
- 2026-05-02: Attempted `/Applications/Lite XL.app/Contents/MacOS/lite-xl --version` with approval; it did not expose a useful headless mode and exited without output.
- 2026-05-02: Re-ran the standard documented workflow after updating `build.sh`: `env PATH=/opt/homebrew/opt/zig@0.15/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin ./build.sh` succeeded, followed by `ctest --test-dir build --output-on-failure` passing 3/3 tests.
- 2026-05-02: Installed Homebrew `lua` 5.5.0 and added `tests/test_lua_helpers.lua` for events, key fallbacks, selection extraction, and click-to-open parsing/resolution.
- 2026-05-02: Registered the Lua helper test with CTest when a Lua interpreter is available.
- 2026-05-02: `env PATH=/opt/homebrew/opt/zig@0.15/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin ./build.sh && ctest --test-dir build --output-on-failure` passed: 4/4 tests.
- 2026-05-02: Added `tests/test_dlopen.c` to verify that the produced native module loads with `dlopen` and exports `luaopen_lite_xl_libghostty_lxl`.
- 2026-05-02: `env PATH=/opt/homebrew/opt/zig@0.15/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin ./build.sh && ctest --test-dir build --output-on-failure` passed: 5/5 tests.

## Completion Audit

Objective restated as concrete deliverables:

- Implement the MVP plugin layout, native Ghostty VT module, Lua TerminalView, events, input, mouse, paste, click-to-open, selection, build metadata, tests, durable progress, and logical commits.
- Verify source builds and runtime behavior where feasible.

Prompt-to-artifact checklist:

- `specs/mvp.md` implemented as repo layout: present `manifest.json`, `build.sh`, `CMakeLists.txt`, `src/*`, and `plugins/ghostty/*`.
- Durable state in `execplans/mvp-implementation.md`: present and updated throughout.
- Use subagents: completed independent reference-analysis agents for Ghostty C APIs, Lite XL patterns, and build integration.
- Logical commits: `7554600`, `bc69542`, `01c1adc`, `4bfa29b`, `129f06e`, `b3c4fa4`, `c233868`, `b00a97d`, `4d21ba6`, `f1ff182`.
- Unit tests: present `tests/test_event_queue.c`, `tests/test_osc_observer.c`, `tests/test_utf8.c`, `tests/test_lua_helpers.lua`, and `tests/test_dlopen.c`; all pass under CTest after `./build.sh`.
- Native object compile evidence: `lxl_ghostty.c`, `terminal.c`, and `pty_unix.c` compile as objects against vendored Lite XL and Ghostty headers.
- Full native build evidence: achieved with `env PATH=/opt/homebrew/opt/zig@0.15/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin ./build.sh`.
- Lite XL runtime smoke test: weakly verified only; Lite XL did not expose a useful headless command path from this shell.

Missing or weakly verified requirements:

- Lite XL load smoke test and headless UI testing remain weakly verified because Lite XL did not expose a noninteractive command path from this shell.
- Lua modules could not be syntax-checked with `lua`/`luac` because no Lua interpreter is installed.
- Some MVP behaviors are first-pass implementations and need runtime validation under Lite XL: alternate-screen TUIs, Codex/Claude TUIs, exact mouse reporting behavior, OSC 52 prompt ergonomics, selection across scrollback, and file open cursor placement.

## Commit Plan

1. Scaffolding and test harness.
2. Native support modules: event queue, UTF-8, OSC observer, tests.
3. PTY and terminal lifecycle with Lua binding stubs.
4. Ghostty render/input integration.
5. Lua TerminalView, commands, config, events.
6. Click-to-open, selection, paste/OSC52 policy.
7. Docs, verification, and polish.
