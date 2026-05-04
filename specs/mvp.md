# lite-xl-ghostty MVP Spec

Status: draft for review.
Target: Lite XL 2.1.x (`mod-version:3`), macOS and Linux.
Native engine: `libghostty-vt` from the Ghostty fork under `references/ghostty`.

## 1. Purpose

`lite-xl-ghostty` is a Lite XL terminal plugin backed by Ghostty's embeddable VT engine. The goal is not to build another hand-rolled terminal parser. The goal is to make Lite XL's integrated terminal good enough for modern coding-agent TUIs such as Codex CLI, Claude Code, opencode, aider, and similar tools.

The MVP should deliver a reliable text terminal with correct VT behavior, rich keyboard and mouse handling, useful terminal events, and click-to-open workflows. It should explicitly avoid renderer-heavy features such as Kitty graphics until the core terminal is stable.

## 2. Decisions

These decisions are fixed for MVP unless this document is revised:

- Platforms: macOS and Linux only.
- PTY model: POSIX `forkpty`; Windows/ConPTY is out of MVP scope.
- VT engine: use `libghostty-vt`; do not implement VT parsing locally.
- Native threading: one C-side reader thread per terminal drains the PTY.
- Scrollback: use `libghostty-vt` line cap via `max_scrollback`; default `10000` lines.
- Lua event API: pub/sub, e.g. `ghostty.on(event_name, callback)`.
- OSC 9 and OSC 52: implement a C-side sidecar OSC observer because public `libghostty-vt` does not expose them as simple terminal effects.
- Keyboard: hybrid model. Use Ghostty's key encoder when Lite XL exposes sufficient event data, and Lua fallback tables where it does not.
- Lite XL shortcut escape hatch: terminal captures most keys by default; an inversion modifier routes shortcuts back to Lite XL.
- Agent command model: argv table is canonical. Shell strings are opt-in.
- Relative file links: resolve against OSC 7 cwd first, then Lite XL project root.
- Kitty graphics: disabled in MVP.
- Selection: Lua-side grid selection in MVP; Ghostty selection API is v1.1+.

## 3. Goals

- Provide a functional integrated terminal view in Lite XL.
- Support drawer and tab terminal views.
- Support multiple terminal instances through Lite XL's normal split/tab model.
- Support a constructor suitable for dedicated agent terminals with command, cwd, env, and close-on-exit policy.
- Render from `GhosttyRenderState` rather than maintaining a parallel Lua terminal buffer.
- Handle common terminal effects: title changes, bell, cwd changes, focus reporting, size queries, device attribute responses, and pty write-backs.
- Support modern input: cursor application mode, bracketed paste, Kitty keyboard protocol when event data allows it, and Ghostty mouse encoding.
- Support clickable OSC 8 hyperlinks, auto-detected URLs, and file paths with optional `:line[:col]`.
- Expose terminal lifecycle and semantic events to other Lite XL plugins.
- Ship as an `lpm` plugin with source build support and eventually prebuilt native libraries.

## 4. Non-Goals

- Windows support.
- Kitty graphics rendering or inline images.
- tmux-style splits inside one terminal.
- Full terminal search UI.
- Ghostty-native selection integration.
- A full profile manager or agent launcher UI.
- Replacing `lite-xl-terminal`; this plugin should coexist under the id `ghostty`.
- Terminal shell integration injection by default. MVP consumes OSC 7/133 when emitted, but does not mutate shell startup unless the user opts in later.

## 5. Reference Findings

### 5.1 Ghostty / libghostty-vt

The relevant C API is under `references/ghostty/include/ghostty/vt`.

`libghostty-vt` provides:

- `GhosttyTerminal`: VT parser, screen state, scrollback, modes, colors, title, cwd, Kitty keyboard state, mouse modes, and Kitty graphics storage.
- `GhosttyRenderState`: frame-oriented render snapshot with row/cell iterators, dirty tracking, graphemes, resolved foreground/background colors, cursor state, and palette.
- `GhosttyKeyEncoder`: key events to terminal byte sequences, including application cursor mode, modifier handling, xterm modifyOtherKeys, and Kitty keyboard protocol flags.
- `GhosttyMouseEncoder`: mouse events to X10, UTF-8, SGR, URxvt, and SGR-pixels encodings.
- `ghostty_paste_is_safe` and `ghostty_paste_encode`: paste safety and bracketed paste encoding.
- Terminal effects callbacks for pty responses, bell, title changed, enquiry, XTVERSION, size report, color scheme query, and device attributes.

Important API facts:

- `GHOSTTY_TERMINAL_DATA_TITLE` exposes OSC 0/2 title state.
- `GHOSTTY_TERMINAL_DATA_PWD` exposes OSC 7 working directory state.
- OSC 8 hyperlinks are exposed through cell metadata and `ghostty_grid_ref_hyperlink_uri`.
- OSC 133 prompt/input/output semantics are exposed through row and cell metadata.
- Public `libghostty-vt` does not expose OSC 9 notifications or OSC 52 clipboard writes as direct `GhosttyTerminal` effect callbacks, so MVP needs a sidecar OSC observer if these are in scope.
- `GhosttyTerminalOptions.max_scrollback` is a line cap, not a memory cap. MVP uses a line cap.

### 5.2 ghostling

`references/ghostling/main.c` is the closest native integration template. It demonstrates:

- `forkpty` process creation.
- Feeding PTY bytes into `ghostty_terminal_vt_write`.
- Updating and iterating `GhosttyRenderState`.
- Using Ghostty key and mouse encoders.
- Responding to terminal effect callbacks.

The MVP should borrow this shape, replacing raylib window/input/rendering with Lite XL's Lua view and renderer APIs.

### 5.3 lite-xl-terminal

`references/lite-xl-terminal` is the Lite XL plugin template:

- `plugins/terminal/init.lua` defines `TerminalView = View:extend()`.
- Lua owns view layout, keymap, commands, selection, copy/paste, scrolling, status integration, and drawing.
- Native code is loaded with `require "plugins.terminal.libterminal"`.
- The C entrypoint uses `lite_xl_plugin_api.h`.

The MVP should follow this plugin layout but replace the custom VT parser with Ghostty.

## 6. Architecture

```text
Lite XL process
  plugins/ghostty/init.lua
    TerminalView : View
    commands, keymap, settings
    drawing, selection, click-to-open, event dispatch
    paste and clipboard policy UI
      |
      | Lua native calls
      v
  plugins/ghostty/libghostty_lxl.{lib,so}
    Lua bindings via lite_xl_plugin_api.h
    PTY/process ownership
    reader thread
    GhosttyTerminal
    GhosttyRenderState
    GhosttyKeyEncoder
    GhosttyMouseEncoder
    sidecar OSC observer
    native event queue
      |
      | static link
      v
  libghostty-vt.a
```

The C module owns all terminal state that must remain synchronized with PTY bytes. Lua owns all Lite XL UI and policy.

## 7. Repository Layout

```text
lite-xl-ghostty/
  README.md
  specs/
    mvp.md
  manifest.json
  build.sh
  CMakeLists.txt
  src/
    lxl_ghostty.c
    lxl_ghostty.h
    pty_unix.c
    pty_unix.h
    terminal.c
    terminal.h
    event_queue.c
    event_queue.h
    osc_observer.c
    osc_observer.h
    utf8.c
    utf8.h
  plugins/
    ghostty/
      init.lua
      click_to_open.lua
      events.lua
      keymap.lua
      selection.lua
      config.lua
```

This split keeps `lxl_ghostty.c` focused on Lua binding glue and avoids one giant C file. The implementation may start with fewer files, but these ownership boundaries should remain.

## 8. Build And Distribution

### 8.1 Development Build

Required tools:

- Zig 0.15.x.
- CMake 3.19+.
- Ninja.
- C11 compiler.
- macOS SDK or Linux build essentials.

`build.sh` should:

1. Configure CMake into `build/`.
2. Build `libghostty-vt.a` through Ghostty's Zig build.
3. Build `libghostty_lxl`.
4. Place the native library next to `plugins/ghostty/init.lua`.

### 8.2 CMake

CMake should:

- Drive `zig build lib-vt` against `references/ghostty`.
- Link `libghostty-vt.a` statically into `libghostty_lxl`.
- Include `references/ghostty/include`.
- Include Lite XL's plugin API header, either vendored or discovered.
- Link `pthread`.
- Link `util` on Linux for `forkpty`.
- Avoid raylib, SDL, or any separate GUI dependency.

### 8.3 Prebuilt Releases

MVP should be source-buildable first. The spec should keep the release path compatible with `lpm` prebuilts:

- macOS universal or separate `aarch64` and `x86_64` artifacts.
- Linux `x86_64` and `aarch64` artifacts.
- Native library filename: `libghostty_lxl.lib` on macOS, `libghostty_lxl.so` on Linux.
- Lua module path: `plugins.ghostty.libghostty_lxl`.
- Plugin id: `ghostty`.

## 9. Native Terminal Object

The native terminal userdata should represent one running terminal.

### 9.1 Fields

Conceptual C fields:

```c
typedef struct {
  pthread_mutex_t mu;
  pthread_t reader_thread;
  atomic_bool stopping;
  atomic_bool dirty;
  atomic_bool child_alive;

  int pty_fd;
  pid_t child_pid;
  int exit_status;

  uint16_t cols;
  uint16_t rows;
  uint32_t cell_width_px;
  uint32_t cell_height_px;

  GhosttyTerminal terminal;
  GhosttyRenderState render_state;
  GhosttyRenderStateRowIterator row_iter;
  GhosttyRenderStateRowCells row_cells;
  GhosttyKeyEncoder key_encoder;
  GhosttyKeyEvent key_event;
  GhosttyMouseEncoder mouse_encoder;
  GhosttyMouseEvent mouse_event;

  LxlGhosttyEventQueue events;
  LxlGhosttyOscObserver osc_observer;

  char *last_title;
  char *last_pwd;
} LxlGhosttyTerminal;
```

The exact struct can differ, but these responsibilities should be represented.

### 9.2 Constructor Options

Lua:

```lua
local term = native.new({
  cols = 80,
  rows = 24,
  cell_width = 8,
  cell_height = 16,
  max_scrollback = 10000,
  command = { "zsh", "-l" },
  cwd = "/path/to/project",
  env = { TERM = "xterm-256color" },
  shell = false,
  name = "Terminal"
})
```

Rules:

- `command` table is canonical and maps directly to `execvp`.
- `command` string is only valid when `shell = true`; it runs through the user's shell as `shell -lc command`.
- If `command` is omitted, use `$SHELL`, then passwd shell, then `/bin/sh`.
- `cwd` defaults to Lite XL project root, then process cwd.
- `TERM` defaults to `xterm-256color`.
- The plugin may later support `xterm-ghostty`, but should not require terminfo installation for MVP.

### 9.3 Lifecycle

Native methods:

- `term:close()`: stop reader thread, close PTY, terminate child if needed, free Ghostty objects.
- `term:exited() -> exited, code, signal`: report child state.
- `term:pid() -> pid`.
- `term:is_dirty() -> boolean`.

Close behavior is a Lua policy:

- `close_on_exit = "clean_exit"` for normal shell terminals.
- `close_on_exit = "never"` for agent terminals.
- `close_on_exit = "always"` available for one-shot command terminals.

## 10. PTY And Threading

### 10.1 Spawn

Use `forkpty`:

1. Initialize `winsize` from `rows` and `cols`.
2. `forkpty(&pty_fd, NULL, NULL, &winsize)`.
3. Child:
   - `chdir(cwd)` if provided.
   - Apply environment overrides.
   - `execvp`.
   - On failure, print an error to stderr and `_exit(127)`.
4. Parent:
   - Set `pty_fd` non-blocking.
   - Start reader thread.

### 10.2 Reader Thread

The reader thread:

- Polls the PTY fd.
- Reads available bytes into a fixed buffer, e.g. 16 KiB.
- Feeds the same bytes to the sidecar OSC observer.
- Locks `terminal->mu`.
- Calls `ghostty_terminal_vt_write`.
- Unlocks.
- Sets `dirty = true`.

The sidecar OSC observer should run before `ghostty_terminal_vt_write` so policies like OSC 52 can see the raw command. It must not mutate terminal state. It only pushes events.

### 10.3 Locking

The Ghostty terminal and render state are protected by `terminal->mu`.

Must hold mutex for:

- `ghostty_terminal_vt_write`.
- `ghostty_render_state_update`.
- `ghostty_terminal_resize`.
- `ghostty_terminal_scroll_viewport`.
- `ghostty_terminal_get` for borrowed strings such as title and pwd.
- `ghostty_terminal_grid_ref` and immediate use of grid refs.
- `ghostty_key_encoder_setopt_from_terminal`.
- `ghostty_mouse_encoder_setopt_from_terminal`.

Should avoid holding mutex while:

- Calling Lua.
- Opening files.
- Setting clipboard.
- Showing notifications.
- Running shell commands.

### 10.4 Event Queue

Callbacks and the reader thread push events into a native queue. Lua drains events during `TerminalView:update`.

Queue policy:

- Bell events may be coalesced.
- Title/cwd events should keep the latest value if many arrive in one frame.
- OSC 9 notifications should be rate-limited on the Lua side.
- OSC 52 clipboard writes must not be dropped silently unless policy denies them.
- Exit event should be delivered exactly once.

## 11. Ghostty Integration

### 11.1 Terminal Creation

Create:

- `GhosttyTerminal`.
- `GhosttyRenderState`.
- Reusable row iterator.
- Reusable row cells iterator.
- `GhosttyKeyEncoder`.
- Reusable `GhosttyKeyEvent`.
- `GhosttyMouseEncoder`.
- Reusable `GhosttyMouseEvent`.

Set terminal options:

- `GHOSTTY_TERMINAL_OPT_USERDATA`.
- `GHOSTTY_TERMINAL_OPT_WRITE_PTY`.
- `GHOSTTY_TERMINAL_OPT_BELL`.
- `GHOSTTY_TERMINAL_OPT_TITLE_CHANGED`.
- `GHOSTTY_TERMINAL_OPT_ENQUIRY`.
- `GHOSTTY_TERMINAL_OPT_XTVERSION`.
- `GHOSTTY_TERMINAL_OPT_SIZE`.
- `GHOSTTY_TERMINAL_OPT_COLOR_SCHEME`.
- `GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES`.
- `GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND`.
- `GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND`.
- `GHOSTTY_TERMINAL_OPT_COLOR_CURSOR`.
- `GHOSTTY_TERMINAL_OPT_COLOR_PALETTE`.
- `GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_STORAGE_LIMIT = 0`.

### 11.2 Terminal Effects

`WRITE_PTY`:

- Write response bytes back to PTY immediately.
- Must be non-blocking safe.
- If write returns `EAGAIN`, buffering is acceptable but MVP may retry once and drop with debug log if still blocked.

`BELL`:

- Push `bell` event.
- Lua decides status flash / audible behavior.

`TITLE_CHANGED`:

- Query `GHOSTTY_TERMINAL_DATA_TITLE` under mutex.
- Copy string into event queue.
- Lua updates view title and emits `title-changed`.

`SIZE`:

- Return pixel and cell size from stored terminal dimensions.

`DEVICE_ATTRIBUTES`:

- Return Ghostty-compatible attributes based on `references/ghostling`.

`XTVERSION`:

- Return `lite-xl-ghostty <version>`.

`COLOR_SCHEME`:

- Return dark/light based on Lite XL style background luminance.

### 11.3 Cwd Detection

OSC 7 updates are reflected in `GHOSTTY_TERMINAL_DATA_PWD`.

Because there is no dedicated cwd-changed callback in the public API, Lua or C should detect changes after VT writes/render updates:

- Store `last_pwd`.
- On dirty update, query `GHOSTTY_TERMINAL_DATA_PWD`.
- If changed and non-empty, push or emit `cwd-changed`.

## 12. Sidecar OSC Observer

The observer watches the PTY byte stream for OSC sequences. It must be streaming and tolerant of fragmented reads.

MVP commands:

- OSC 9 desktop notification.
- OSC 52 clipboard read/write.

Nice-to-have observed commands:

- OSC 7 cwd, only as a fallback event source. Ghostty remains canonical for cwd state.
- OSC 133 prompt markers, only for event synthesis if row semantics are insufficient.

### 12.1 Parser Requirements

The observer should recognize:

- `ESC ] ... BEL`.
- `ESC ] ... ESC \`.
- UTF-8 payload bytes without assuming null termination.
- Sequences split across reads.
- A maximum OSC buffer size, default 1 MiB, configurable lower for safety.

On overflow:

- Drop the sequence.
- Push a debug event if debug logging is enabled.
- Continue parsing subsequent bytes.

### 12.2 OSC 9

Support common forms:

- `OSC 9;message ST`.
- `OSC 9;title;body ST` if encountered.

Lua behavior:

- Emit `notification` event.
- Show Lite XL status/toast notification by default.
- Rate-limit repeated identical notifications.
- Do not invoke platform desktop notification in MVP unless Lite XL already exposes a stable cross-platform API.

### 12.3 OSC 52

Support clipboard writes:

- Parse clipboard target.
- Decode base64 content.
- Enforce maximum decoded size, default 1 MiB.
- Push `clipboard-write-request` event to Lua.

Policy:

- `osc52 = "ask"` by default.
- Prompt/notify on first write per terminal session.
- Once accepted, allow future writes in that session.
- `osc52 = "deny"` denies all writes.
- `osc52 = "allow"` permits without prompt.

Clipboard reads:

- Deny by default in MVP, or respond empty.
- Do not expose system clipboard contents to terminal applications unless a future explicit policy is added.

Security:

- Never execute OSC payloads.
- Never trust OSC 52 base64 size before decoding.
- Never let OSC observer call Lua from reader thread.

## 13. Lua Plugin

### 13.1 Modules

`plugins/ghostty/init.lua` exports:

```lua
local ghostty = {
  TerminalView = TerminalView,
  new_terminal = function(options) ... end,
  open_tab = function(options) ... end,
  open_drawer = function(options) ... end,
  on = events.on,
  off = events.off,
  emit = events.emit
}
return ghostty
```

Internal modules:

- `events.lua`: pub/sub implementation.
- `config.lua`: defaults and settings spec.
- `keymap.lua`: terminal command/key bindings.
- `selection.lua`: Lua-side selection.
- `click_to_open.lua`: link detection and activation.

### 13.2 TerminalView

`TerminalView` owns:

- Native terminal object.
- View size and computed terminal cols/rows.
- Selection state.
- Scrollbar state.
- Mouse hover/click state.
- Last render rows.
- Title, cwd, exited state.
- Agent flag and close-on-exit policy.

Required methods:

- `new(options)`.
- `get_name()`.
- `supports_text_input()`.
- `update()`.
- `draw()`.
- `on_text_input(text)`.
- `on_mouse_pressed(button, x, y, clicks)`.
- `on_mouse_moved(x, y, dx, dy)`.
- `on_mouse_released(button, x, y)`.
- `on_mouse_wheel(y)`.
- `on_key_pressed(key, scancode, repeated, modifiers)` if Lite XL exposes it.
- `close()`.

### 13.3 Commands

MVP commands:

- `ghostty:toggle-drawer`.
- `ghostty:open-drawer`.
- `ghostty:open-tab`.
- `ghostty:close-terminal`.
- `ghostty:clear`.
- `ghostty:paste`.
- `ghostty:copy-selection`.
- `ghostty:scroll-up`.
- `ghostty:scroll-down`.
- `ghostty:spawn-command`.
- `ghostty:spawn-agent`.

Default keys:

- `alt+t`: toggle drawer.
- `ctrl+shift+``: open terminal tab.
- `ctrl+shift+v`: paste into terminal.
- `ctrl+shift+w`: close terminal view.

Terminal-focused shortcuts should mirror `lite-xl-terminal` where practical.

## 14. Rendering

### 14.1 Native Render Snapshot

Native API should expose a render snapshot optimized for Lua.

Avoid returning one Lua table per cell. Return rows as spans:

```lua
{
  cols = 120,
  rows = 32,
  cursor = { x = 3, y = 10, visible = true, style = "block", blinking = false },
  scrollbar = { total = 5000, offset = 4968, len = 32 },
  background = { 18, 18, 18 },
  foreground = { 220, 220, 220 },
  rows_data = {
    {
      dirty = true,
      semantic = "none",
      spans = {
        { x = 0, text = "hello", fg = {...}, bg = nil, bold = false, italic = false, underline = false, inverse = false },
      }
    }
  }
}
```

The exact representation can be more compact for performance, but it must preserve:

- UTF-8 text produced from Ghostty grapheme codepoints.
- Wide cell spacing.
- Foreground/background colors.
- Bold, italic, underline, strikethrough if exposed.
- Inverse.
- Dirty rows.
- Cursor position and style.
- Row semantic state.
- Whether a row contains hyperlinks.

### 14.2 Lua Drawing

Lua draws:

- Background fill.
- Per-span background rectangles.
- Per-span text with `renderer.draw_text`.
- Cursor overlay.
- Selection overlay.
- Scrollbar.
- Link hover underline/cursor if active.

Font:

- Use `config.plugins.ghostty.font`, default `style.code_font`.
- Terminal assumes monospace. If the configured font is not monospace, behavior is unsupported.

Cell dimensions:

- `cell_width = renderer.get_text_width(font, "M")` or equivalent.
- `cell_height = font:get_height()`.
- Resize terminal when view size changes enough to alter cols/rows.

### 14.3 Dirty Tracking

MVP can redraw the full terminal view when native dirty is set. The native render API should still expose dirty rows so later optimization is straightforward.

After Lua draws a frame, call native `term:clear_dirty()` or equivalent to reset Ghostty render dirty state.

## 15. Keyboard Input

### 15.1 Hybrid Model

Input routes:

- Printable text from Lite XL `on_text_input(text)` goes to `term:input_text(text)` unless it is part of a handled shortcut.
- Special keys use `term:send_key(event)` when sufficient metadata is available.
- Missing event data falls back to Lua escape tables.

Before each encoded key:

1. Lock terminal mutex.
2. Call `ghostty_key_encoder_setopt_from_terminal`.
3. Reapply configured `macos_option_as_alt`.
4. Populate reusable `GhosttyKeyEvent`.
5. Encode.
6. Unlock.
7. Write encoded bytes to PTY.

### 15.2 Modifier Policy

Defaults:

- Terminal captures normal terminal shortcuts.
- Lite XL shortcuts use an inversion modifier, default `shift` with Ctrl on non-macOS where practical.
- macOS Option-as-Meta default: `true`.

Config:

```lua
config.plugins.ghostty.inversion_key = "shift"
config.plugins.ghostty.mac_option_as_meta = true -- true | "left" | "right" | false
```

### 15.3 Fallback Tables

Fallbacks must include:

- Enter / Return.
- Backspace.
- Delete.
- Tab / Shift-Tab.
- Escape.
- Arrows.
- Home / End.
- Page Up / Page Down.
- Insert.
- F1-F12 at minimum.
- Ctrl-letter control bytes.

Fallbacks must consult terminal state where exposed:

- Cursor key application mode.
- Bracketed paste mode.
- Keypad application mode if implemented.

## 16. Mouse Input

Use `GhosttyMouseEncoder` for application mouse reporting.

On mouse event:

1. Convert pixel x/y to terminal col/row.
2. If click-to-open activation modifier is active, try link/path activation before sending to PTY.
3. If selection mode is active, update selection rather than sending.
4. If terminal mouse tracking is active, encode through Ghostty and write PTY bytes.
5. Otherwise, use mouse wheel for viewport scrollback and clicks for selection/focus.

Defaults:

- Click-to-open activation: Cmd-click on macOS, Ctrl-click on Linux.
- Plain click focuses terminal or sends to TUI if mouse tracking is active.

Mouse support:

- Press/release left/middle/right.
- Drag/motion when tracking asks for it.
- Wheel up/down.
- SGR coordinates through Ghostty.
- Pixel coordinate reporting where Ghostty can encode it and Lite XL provides enough data.

## 17. Paste

Paste flow:

1. Lua reads clipboard.
2. Native checks bracketed paste mode with `ghostty_terminal_mode_get(GHOSTTY_MODE_BRACKETED_PASTE)`.
3. Use `ghostty_paste_is_safe`.
4. Warn only when paste is unsafe and bracketed paste is not active.
5. Use `ghostty_paste_encode`.
6. Write encoded paste to PTY.

Unsafe means at least:

- Multiline paste.
- Bracketed paste terminator injection.
- Control characters that Ghostty strips or normalizes.

The warning should be bypassable per paste operation and configurable.

## 18. Selection And Copy

MVP uses Lua-side grid selection.

Supported:

- Drag selection.
- Double-click word selection.
- Triple-click line selection if feasible.
- Copy selected text to system clipboard.
- Primary selection on Linux only if Lite XL exposes it.
- Preserve line wraps enough for normal terminal copy.

Known limitations:

- Grapheme and wide-character selection may be imperfect in MVP.
- Selection across deep scrollback depends on the render snapshot API and may initially be limited to visible viewport plus native text extraction helpers.

Future:

- Replace or augment with `libghostty-vt` selection API after basic terminal behavior is stable.

## 19. Click-To-Open

Activation default:

- macOS: Cmd-click.
- Linux: Ctrl-click.

Targets:

- OSC 8 hyperlinks, using Ghostty grid refs.
- Auto-detected `http://` and `https://` URLs.
- `file://` URLs.
- Absolute file paths.
- Relative file paths.
- `path:line`.
- `path:line:col`.

Resolution:

1. OSC 8 hyperlink URI wins.
2. Auto-detected URL.
3. Absolute file path.
4. Relative file path resolved against OSC 7 cwd.
5. Relative file path resolved against Lite XL project root.

Open behavior:

- File paths open in Lite XL.
- `line` and `col` move cursor if Lite XL API supports it.
- URLs open with Lite XL/system browser facilities.
- Unknown schemes do nothing unless explicitly allowed.

Parsing boundaries:

- Do not include trailing punctuation such as `.`, `,`, `)`, `]`, `}` unless balanced.
- Support quoted paths with spaces later; MVP may only auto-detect unquoted paths.
- Avoid scanning across OSC 133 prompt boundaries when possible.

Git SHA click support is not MVP.

## 20. Events

### 20.1 API

```lua
local ghostty = require "plugins.ghostty"

local unsubscribe = ghostty.on("cwd-changed", function(event)
  -- event.view, event.terminal, event.cwd
end)

unsubscribe()
```

`ghostty.off(event_name, fn)` may also be supported.

Handlers must be called on the Lite XL main thread, never from C reader thread.

### 20.2 Event Names

MVP events:

- `terminal-created`
- `terminal-closed`
- `terminal-exited`
- `title-changed`
- `cwd-changed`
- `bell`
- `notification`
- `clipboard-write-request`
- `clipboard-write-denied`
- `clipboard-write-accepted`
- `prompt-start`
- `prompt-end`
- `command-start`
- `command-end`
- `link-opened`

Prompt/command events are best-effort in MVP:

- Use OSC 133 row/cell semantics where possible.
- If precise exit status from OSC 133 is unavailable through public APIs, emit semantic boundary events without exit code.
- Do not invent exit codes.

### 20.3 Event Payloads

All event payloads include:

```lua
{
  view = view,
  terminal = native_terminal,
  kind = "cwd-changed",
  time = system.get_time()
}
```

Specific fields:

- `title-changed`: `title`.
- `cwd-changed`: `cwd`, `previous_cwd`.
- `bell`: `count`.
- `notification`: `title`, `body`, `raw`.
- `clipboard-write-request`: `clipboard`, `text`, `bytes`.
- `terminal-exited`: `code`, `signal`, `clean`.
- `link-opened`: `target`, `target_type`, `line`, `col`.

## 21. Agent Terminals

MVP exposes a constructor, not a full UI.

```lua
ghostty.open_tab({
  kind = "agent",
  command = { "codex" },
  cwd = core.root_project().path,
  env = {
    TERM = "xterm-256color"
  },
  close_on_exit = "never",
  title = "Codex"
})
```

Agent terminal behavior:

- Default `close_on_exit = "never"`.
- Prefer project root cwd.
- Emit cwd/title/notification/bell events like normal terminals.
- Allow startup command injection only through explicit constructor options.
- Do not auto-run agents without a user command or config entry.

One command palette entry should exist:

- `ghostty:spawn-agent`

It may initially prompt for a command string and run it with `shell = true`, but documented programmatic API should prefer argv.

## 22. Configuration

Suggested defaults:

```lua
config.plugins.ghostty = {
  term = "xterm-256color",
  shell = os.getenv("SHELL") or "/bin/sh",
  drawer_height = 300,
  font = style.code_font,
  max_scrollback = 10000,
  close_on_exit = "clean_exit",
  agent_close_on_exit = "never",
  inversion_key = "shift",
  mac_option_as_meta = true,
  click_modifier = PLATFORM == "Mac OS X" and "cmd" or "ctrl",
  osc52 = "ask",
  osc52_max_bytes = 1024 * 1024,
  osc_max_bytes = 1024 * 1024,
  paste_warning = true,
  kitty_graphics = false,
  minimum_contrast_ratio = 3,
  debug = false
}
```

Theme:

- Default background/foreground comes from Lite XL style.
- 256-color palette can default to Ghostty's built-in palette.
- Allow user override of foreground, background, cursor, and palette.

## 23. Native Lua API

Native methods should be small and policy-free.

Creation:

- `native.new(options) -> terminal`

Lifecycle:

- `terminal:close()`
- `terminal:exited() -> exited, code, signal`
- `terminal:pid() -> pid`

Sizing:

- `terminal:resize(cols, rows, cell_width_px, cell_height_px)`

Input:

- `terminal:write(bytes)`
- `terminal:send_key(event_table) -> handled`
- `terminal:send_mouse(event_table) -> handled`
- `terminal:paste(text) -> ok, warning`
- `terminal:focus(focused)`

Render:

- `terminal:update_render() -> snapshot_or_nil`
- `terminal:clear_dirty()`
- `terminal:is_dirty() -> boolean`

State:

- `terminal:title() -> string`
- `terminal:cwd() -> string`
- `terminal:mouse_tracking() -> boolean`
- `terminal:bracketed_paste() -> boolean`
- `terminal:scrollbar() -> table`
- `terminal:scroll(delta)`
- `terminal:scroll_top()`
- `terminal:scroll_bottom()`

Events:

- `terminal:poll_events() -> { events... }`

Grid lookup:

- `terminal:hyperlink_at(col, row) -> uri_or_nil`
- `terminal:text_at_row(row) -> string` if needed for click detection.

## 24. Edge Cases

### 24.1 Heavy Output

The reader thread prevents PTY backpressure from Lua frame rate. Render snapshots still occur on the Lite XL main loop. If output is continuous, coalesce redraws to one per frame.

### 24.2 Child Exit While Data Remains

The reader thread should drain remaining PTY bytes before marking the terminal fully exited where possible. Lua should show final output before applying close policy.

### 24.3 Resize Races

Resize locks the terminal mutex, calls `ghostty_terminal_resize`, and calls `ioctl(TIOCSWINSZ)` on PTY. If the child exits during resize, ignore PTY ioctl errors.

### 24.4 Borrowed Ghostty Strings

Ghostty title and pwd strings are borrowed. Native code must copy them before releasing the mutex or pushing to event queue.

### 24.5 OSC Observer False Positives

The observer must only parse actual OSC sequences beginning with `ESC ]`. Plain output that looks like `9;...` is irrelevant.

### 24.6 Clipboard Security

OSC 52 can overwrite user clipboard. Default is ask once per terminal session. Reads are denied by default.

### 24.7 Terminal App Mouse Capture

When mouse tracking is enabled, plain clicks go to the TUI. Click-to-open requires the configured modifier. Selection may require Shift-drag or a config option when mouse tracking is active.

### 24.8 Lite XL Shortcut Conflicts

Terminal focus captures most control keys. Users need a documented inversion modifier for editor commands. The plugin should preserve core escape routes like command palette if Lite XL requires them.

### 24.9 Relative Paths Without Cwd

Use Lite XL project root fallback. If neither cwd nor project root is known, do not open relative paths.

### 24.10 Unicode

Rendering uses Ghostty grapheme data, converts codepoints to UTF-8, and skips wide spacer tails. Lua selection may still be less correct than rendering.

### 24.11 Kitty Keyboard Limitations

Lite XL may not expose physical key code, unshifted codepoint, consumed modifiers, or key release events in all cases. The spec accepts partial Kitty keyboard support in MVP via hybrid routing.

### 24.12 Debug Logging

Debug logging must be opt-in. It may log PTY bytes to a file for troubleshooting, but should redact OSC 52 payloads by default.

## 25. Acceptance Criteria

MVP is acceptable when:

- A user can open a drawer terminal and a tab terminal.
- Shell starts in the configured cwd.
- Terminal resizes correctly with the Lite XL view.
- Running `vim`, `less`, `htop`, or similar alternate-screen TUIs works.
- Running Codex CLI or Claude Code does not break on basic keyboard/mouse usage.
- Bracketed paste works.
- Mouse reporting works in TUIs that request it.
- OSC 7 cwd updates emit `cwd-changed`.
- OSC 0/2 title updates change the view title.
- OSC 8 links can be Cmd/Ctrl-clicked.
- Detected file paths with `:line[:col]` open in Lite XL.
- OSC 9 emits a Lite XL notification event.
- OSC 52 write prompts on first write per terminal session.
- Heavy output does not freeze Lite XL.
- Closing a terminal cleans up the child process and reader thread.

## 26. Implementation Plan

1. Build skeleton:
   - Create plugin layout.
   - Build native module that loads in Lite XL.
   - Link `libghostty-vt.a`.

2. Native terminal basics:
   - `forkpty`.
   - Reader thread.
   - `GhosttyTerminal`.
   - `GhosttyRenderState`.
   - Basic render snapshot.

3. Lua view basics:
   - Drawer and tab commands.
   - Resize.
   - Draw text spans.
   - Close and exit policy.

4. Input:
   - Text input.
   - Key encoder path.
   - Lua fallback key tables.
   - Paste encoding.

5. Mouse:
   - Scrollback.
   - Mouse encoder.
   - Focus reporting.

6. Events:
   - Native event queue.
   - Lua pub/sub.
   - Title, cwd, bell, exit.

7. Sidecar OSC:
   - Streaming OSC parser.
   - OSC 9.
   - OSC 52 write policy.

8. Click-to-open:
   - OSC 8 lookup.
   - URL and path detection.
   - File open with line/col.

9. Selection:
   - Visible grid selection.
   - Copy.
   - Mouse interactions.

10. Polish:
   - Config defaults.
   - Status integration.
   - Debug logging.
   - Build docs.

## 27. Deferred Work

- Windows/ConPTY backend.
- Kitty graphics rendering.
- Ghostty selection API integration.
- Search UI.
- Agent profile manager.
- Better prompt/command lifecycle events if more OSC 133 data becomes public.
- Native desktop notifications.
- Git SHA click actions.
- Memory-cap scrollback if `libghostty-vt` exposes it.

