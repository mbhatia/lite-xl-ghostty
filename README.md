This is a libghostty-based terminal emulator plugin for [lite-xl](https://lite-xl.com/).
We will be using the [ghostty-fork](https://github.com/manaflow-ai/ghostty) from `cmux`'s author as I plan to be able to build some similar features for agent notifications etc.

It is inspired by [lite-xl-terminal](https://github.com/adamharrison/lite-xl-terminal).
The main issue I have with the existing lite-xl-terminal plugin is that it did not seem to support latest coding agent TUIs.

Some other references codes that were used in developing this:
- https://github.com/ghostty-org/ghostling

See [mvp.md](specs/mvp.md) for the v1 implementation spec.

## Status

This repository contains an MVP implementation in progress:

- Native module skeleton: `ghostty_lxl`, packaged as a Lite XL library dependency.
- POSIX `forkpty` process backend for macOS/Linux.
- Ghostty VT terminal/render/key/mouse/paste integration.
- Lua `TerminalView`, drawer/tab commands, pub/sub events, selection helpers, click-to-open parsing, and OSC 52 policy.
- Unit tests for the native event queue and sidecar OSC observer.

Kitty graphics, Windows/ConPTY, terminal search UI, and profile management are intentionally outside the MVP.

## Requirements

- Lite XL 2.1.x.
- Zig 0.15.2 or newer.
- CMake 3.19 or newer.
- Ninja.
- C11 compiler.
- macOS SDK or Linux build essentials.

The Lite XL plugin API header is vendored at `lib/lite-xl/resources/include/lite_xl_plugin_api.h` for source builds. You can override it with CMake's `LITE_XL_INCLUDE_DIR` cache variable if needed.

## Build

```sh
./build.sh
ctest --test-dir build --output-on-failure
```

The build uses CMake, drives Ghostty's Zig/CMake wrapper through `references/ghostty`, statically links `ghostty-vt-static`, and copies the native module into `libraries/ghostty_lxl/`.

Useful options:

```sh
./build.sh -DLXL_GHOSTTY_BUILD_TESTS=ON
./build.sh -DLITE_XL_INCLUDE_DIR=/path/to/lite-xl/resources/include
./build.sh clean
```

In environments without CMake, the portable unit-test slice can still be compiled directly with `cc`; see `execplans/mvp-implementation.md` for the exact commands used during implementation.

## Usage

Install or symlink this repository so Lite XL can load `plugins/ghostty`, then use:

- `ghostty:toggle-drawer`
- `ghostty:open-drawer`
- `ghostty:open-tab`
- `ghostty:spawn-agent`

Default keys:

- `alt+t`: toggle drawer
- `ctrl+shift+``: open terminal tab
- `ctrl+shift+v`: paste
- `ctrl+shift+w`: close terminal

Programmatic API:

```lua
local ghostty = require "plugins.ghostty"

ghostty.open_tab({
  kind = "agent",
  command = { "codex" },
  cwd = core.root_project().path,
  close_on_exit = "never"
})
```

New sessions default to the bundled Ghostty terminfo database with
`TERM=xterm-ghostty`. Override with `config.plugins.ghostty.term`,
`config.plugins.ghostty.terminfo`, or per-session `env`.

By default, terminal text follows `style.code_font`, so Lite XL font and scale
changes are inherited unless `config.plugins.ghostty.font` is set.

Events are delivered on the Lite XL main thread:

```lua
local unsubscribe = ghostty.on("cwd-changed", function(event)
  core.log("terminal cwd: %s", event.cwd)
end)
```
