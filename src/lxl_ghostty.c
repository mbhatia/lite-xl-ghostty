#include "lxl_ghostty.h"
#include "terminal.h"

#include <stdlib.h>
#include <string.h>

#ifdef LXL_GHOSTTY_STANDALONE
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#else
#define LITE_XL_PLUGIN_ENTRYPOINT
#include <lite_xl_plugin_api.h>
#endif

typedef struct {
  LxlGhosttyTerminal *terminal;
} LuaTerminal;

static LuaTerminal *check_terminal(lua_State *L, int index) {
  return (LuaTerminal *)luaL_checkudata(L, index, LXL_GHOSTTY_USERDATA);
}

static char *dup_lua_string(lua_State *L, int index) {
  size_t len = 0;
  const char *s = lua_tolstring(L, index, &len);
  return s ? lxl_ghostty_strdup_len(s, len) : NULL;
}

static size_t table_array_len(lua_State *L, int index) {
  size_t n = 0;
  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    if (lua_type(L, -2) == LUA_TNUMBER) n++;
    lua_pop(L, 1);
  }
  return n;
}

static char **read_string_array(lua_State *L, int index) {
  if (!lua_istable(L, index)) return NULL;
  size_t n = table_array_len(L, index);
  char **argv = (char **)calloc(n + 1, sizeof(char *));
  if (!argv) return NULL;
  for (size_t i = 0; i < n; i++) {
    lua_rawgeti(L, index, (lua_Integer)i + 1);
    argv[i] = dup_lua_string(L, -1);
    lua_pop(L, 1);
    if (!argv[i]) {
      for (size_t j = 0; j < i; j++) free(argv[j]);
      free(argv);
      return NULL;
    }
  }
  return argv;
}

static char **read_env_table(lua_State *L, int index) {
  if (!lua_istable(L, index)) return NULL;
  size_t n = 0;
  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    n++;
    lua_pop(L, 1);
  }
  char **env = (char **)calloc(n + 1, sizeof(char *));
  if (!env) return NULL;
  size_t i = 0;
  lua_pushnil(L);
  while (lua_next(L, index) != 0) {
    const char *key = lua_tostring(L, -2);
    const char *value = lua_tostring(L, -1);
    if (key && value) {
      size_t key_len = strlen(key);
      size_t value_len = strlen(value);
      env[i] = (char *)malloc(key_len + value_len + 2);
      if (env[i]) {
        memcpy(env[i], key, key_len);
        env[i][key_len] = '=';
        memcpy(env[i] + key_len + 1, value, value_len + 1);
        i++;
      }
    }
    lua_pop(L, 1);
  }
  return env;
}

static void free_string_array(char **items) {
  if (!items) return;
  for (char **it = items; *it; it++) free(*it);
  free(items);
}

static char **default_command(void) {
  const char *shell = getenv("SHELL");
  if (!shell || !shell[0]) shell = "/bin/sh";
  char **argv = (char **)calloc(2, sizeof(char *));
  if (!argv) return NULL;
  argv[0] = lxl_ghostty_strdup(shell);
  return argv;
}

static char **shell_command(lua_State *L, const char *command) {
  const char *shell = getenv("SHELL");
  if (!shell || !shell[0]) shell = "/bin/sh";
  char **argv = (char **)calloc(4, sizeof(char *));
  if (!argv) return NULL;
  argv[0] = lxl_ghostty_strdup(shell);
  argv[1] = lxl_ghostty_strdup("-lc");
  argv[2] = lxl_ghostty_strdup(command);
  if (!argv[0] || !argv[1] || !argv[2]) {
    free_string_array(argv);
    luaL_error(L, "out of memory");
  }
  return argv;
}

static void get_field(lua_State *L, int table, const char *name) {
  lua_getfield(L, table, name);
}

static int f_new(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int opts = 1;
  LxlGhosttyTerminalOptions options;
  memset(&options, 0, sizeof(options));

  get_field(L, opts, "cols");
  options.cols = (uint16_t)luaL_optinteger(L, -1, 80);
  lua_pop(L, 1);
  get_field(L, opts, "rows");
  options.rows = (uint16_t)luaL_optinteger(L, -1, 24);
  lua_pop(L, 1);
  get_field(L, opts, "cell_width");
  options.cell_width_px = (uint32_t)luaL_optinteger(L, -1, 8);
  lua_pop(L, 1);
  get_field(L, opts, "cell_height");
  options.cell_height_px = (uint32_t)luaL_optinteger(L, -1, 16);
  lua_pop(L, 1);
  get_field(L, opts, "max_scrollback");
  options.max_scrollback = (size_t)luaL_optinteger(L, -1, 10000);
  lua_pop(L, 1);
  get_field(L, opts, "osc_max_bytes");
  options.osc_max_bytes = (size_t)luaL_optinteger(L, -1, 1024 * 1024);
  lua_pop(L, 1);

  get_field(L, opts, "cwd");
  char *cwd = dup_lua_string(L, -1);
  lua_pop(L, 1);
  options.cwd = cwd;

  get_field(L, opts, "env");
  char **env = read_env_table(L, lua_gettop(L));
  lua_pop(L, 1);
  options.env = env;

  char **argv = NULL;
  get_field(L, opts, "command");
  if (lua_istable(L, -1)) {
    argv = read_string_array(L, lua_gettop(L));
  } else if (lua_isstring(L, -1)) {
    const char *command = lua_tostring(L, -1);
    get_field(L, opts, "shell");
    int use_shell = lua_toboolean(L, -1);
    lua_pop(L, 1);
    if (!use_shell) {
      free(cwd);
      free_string_array(env);
      return luaL_error(L, "string command requires shell = true");
    }
    argv = shell_command(L, command);
  }
  lua_pop(L, 1);
  if (!argv) argv = default_command();
  options.argv = argv;

  char errbuf[512] = {0};
  LxlGhosttyTerminal *terminal = NULL;
  bool ok = lxl_ghostty_terminal_new(&options, &terminal, errbuf, sizeof(errbuf));
  free(cwd);
  free_string_array(env);
  free_string_array(argv);
  if (!ok) return luaL_error(L, "%s", errbuf[0] ? errbuf : "failed to create terminal");

  LuaTerminal *ud = (LuaTerminal *)lua_newuserdata(L, sizeof(*ud));
  ud->terminal = terminal;
  luaL_getmetatable(L, LXL_GHOSTTY_USERDATA);
  lua_setmetatable(L, -2);
  return 1;
}

static int f_gc(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (ud->terminal) {
    lxl_ghostty_terminal_close(ud->terminal);
    ud->terminal = NULL;
  }
  return 0;
}

static int f_close(lua_State *L) {
  return f_gc(L);
}

static int f_pid(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  lua_pushinteger(L, ud->terminal ? ud->terminal->child_pid : 0);
  return 1;
}

static int f_exited(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  int code = 0;
  int sig = 0;
  bool exited = ud->terminal && lxl_ghostty_terminal_exited(ud->terminal, &code, &sig);
  lua_pushboolean(L, exited);
  lua_pushinteger(L, code);
  lua_pushinteger(L, sig);
  return 3;
}

static int f_resize(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  uint16_t cols = (uint16_t)luaL_checkinteger(L, 2);
  uint16_t rows = (uint16_t)luaL_checkinteger(L, 3);
  uint32_t cw = (uint32_t)luaL_checkinteger(L, 4);
  uint32_t ch = (uint32_t)luaL_checkinteger(L, 5);
  lua_pushboolean(L, ud->terminal && lxl_ghostty_terminal_resize(ud->terminal, cols, rows, cw, ch));
  return 1;
}

static int f_write(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  size_t len = 0;
  const char *data = luaL_checklstring(L, 2, &len);
  lua_pushboolean(L, ud->terminal && lxl_ghostty_terminal_write(ud->terminal, data, len));
  return 1;
}

static int f_is_dirty(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  lua_pushboolean(L, ud->terminal && atomic_load(&ud->terminal->dirty));
  return 1;
}

static int f_clear_dirty(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (ud->terminal) atomic_store(&ud->terminal->dirty, false);
  return 0;
}

static int f_paste(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  size_t len = 0;
  const char *data = luaL_checklstring(L, 2, &len);
  bool safe = true;
  bool ok = ud->terminal && lxl_ghostty_terminal_paste(ud->terminal, data, len, &safe);
  lua_pushboolean(L, ok);
  if (!safe) lua_pushliteral(L, "unsafe");
  else lua_pushnil(L);
  return 2;
}

static int f_focus(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (ud->terminal) lxl_ghostty_terminal_focus(ud->terminal, lua_toboolean(L, 2));
  return 0;
}

static int f_scroll(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (ud->terminal) lxl_ghostty_terminal_scroll(ud->terminal, (intptr_t)luaL_checkinteger(L, 2));
  return 0;
}

static int f_scroll_top(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (ud->terminal) lxl_ghostty_terminal_scroll_top(ud->terminal);
  return 0;
}

static int f_scroll_bottom(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (ud->terminal) lxl_ghostty_terminal_scroll_bottom(ud->terminal);
  return 0;
}

static int f_bracketed_paste(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  lua_pushboolean(L, ud->terminal && lxl_ghostty_terminal_bracketed_paste(ud->terminal));
  return 1;
}

static int f_mouse_tracking(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  lua_pushboolean(L, ud->terminal && lxl_ghostty_terminal_mouse_tracking(ud->terminal));
  return 1;
}

static int f_unhandled_false(lua_State *L) {
  (void)L;
  lua_pushboolean(L, 0);
  return 1;
}

static int f_title(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (!ud->terminal || !ud->terminal->last_title) lua_pushnil(L);
  else lua_pushstring(L, ud->terminal->last_title);
  return 1;
}

static int f_cwd(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (!ud->terminal || !ud->terminal->last_pwd) lua_pushnil(L);
  else lua_pushstring(L, ud->terminal->last_pwd);
  return 1;
}

static void push_event(lua_State *L, const LxlGhosttyEvent *event) {
  lua_newtable(L);
  lua_pushinteger(L, event->kind);
  lua_setfield(L, -2, "kind_id");
  switch (event->kind) {
    case LXL_GHOSTTY_EVENT_BELL: lua_pushliteral(L, "bell"); break;
    case LXL_GHOSTTY_EVENT_TITLE: lua_pushliteral(L, "title-changed"); break;
    case LXL_GHOSTTY_EVENT_CWD: lua_pushliteral(L, "cwd-changed"); break;
    case LXL_GHOSTTY_EVENT_NOTIFICATION: lua_pushliteral(L, "notification"); break;
    case LXL_GHOSTTY_EVENT_CLIPBOARD_WRITE_REQUEST: lua_pushliteral(L, "clipboard-write-request"); break;
    case LXL_GHOSTTY_EVENT_CLIPBOARD_WRITE_DENIED: lua_pushliteral(L, "clipboard-write-denied"); break;
    case LXL_GHOSTTY_EVENT_CLIPBOARD_WRITE_ACCEPTED: lua_pushliteral(L, "clipboard-write-accepted"); break;
    case LXL_GHOSTTY_EVENT_EXIT: lua_pushliteral(L, "terminal-exited"); break;
    case LXL_GHOSTTY_EVENT_DEBUG: lua_pushliteral(L, "debug"); break;
    default: lua_pushliteral(L, "unknown"); break;
  }
  lua_setfield(L, -2, "kind");
  if (event->title) {
    lua_pushstring(L, event->title);
    lua_setfield(L, -2, "title");
  }
  if (event->body) {
    lua_pushstring(L, event->body);
    lua_setfield(L, -2, "body");
    lua_pushstring(L, event->body);
    lua_setfield(L, -2, "cwd");
  }
  if (event->text) {
    lua_pushlstring(L, event->text, event->bytes ? event->bytes : strlen(event->text));
    lua_setfield(L, -2, "text");
  }
  if (event->clipboard) {
    lua_pushstring(L, event->clipboard);
    lua_setfield(L, -2, "clipboard");
  }
  lua_pushinteger(L, (lua_Integer)event->bytes);
  lua_setfield(L, -2, "bytes");
  lua_pushinteger(L, event->code);
  lua_setfield(L, -2, "code");
  lua_pushinteger(L, event->signal);
  lua_setfield(L, -2, "signal");
}

static int f_poll_events(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  lua_newtable(L);
  if (!ud->terminal) return 1;
  lxl_ghostty_terminal_poll_state_events(ud->terminal);
  int i = 1;
  LxlGhosttyEvent event;
  while (lxl_ghostty_event_queue_pop(&ud->terminal->events, &event)) {
    push_event(L, &event);
    lua_rawseti(L, -2, i++);
    lxl_ghostty_event_clear(&event);
  }
  return 1;
}

static int f_update_render(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (!ud->terminal) {
    lua_pushnil(L);
    return 1;
  }
  pthread_mutex_lock(&ud->terminal->mu);
  GhosttyResult result = ghostty_render_state_update(ud->terminal->render_state, ud->terminal->terminal);
  uint16_t cols = ud->terminal->cols;
  uint16_t rows = ud->terminal->rows;
  pthread_mutex_unlock(&ud->terminal->mu);
  if (result != GHOSTTY_SUCCESS) {
    lua_pushnil(L);
    return 1;
  }
  lua_newtable(L);
  lua_pushinteger(L, cols);
  lua_setfield(L, -2, "cols");
  lua_pushinteger(L, rows);
  lua_setfield(L, -2, "rows");
  return 1;
}

static const luaL_Reg terminal_methods[] = {
  { "__gc", f_gc },
  { "close", f_close },
  { "pid", f_pid },
  { "exited", f_exited },
  { "resize", f_resize },
  { "write", f_write },
  { "input_text", f_write },
  { "paste", f_paste },
  { "focus", f_focus },
  { "scroll", f_scroll },
  { "scroll_top", f_scroll_top },
  { "scroll_bottom", f_scroll_bottom },
  { "bracketed_paste", f_bracketed_paste },
  { "mouse_tracking", f_mouse_tracking },
  { "send_key", f_unhandled_false },
  { "send_mouse", f_unhandled_false },
  { "hyperlink_at", f_unhandled_false },
  { "text_at_row", f_unhandled_false },
  { "is_dirty", f_is_dirty },
  { "clear_dirty", f_clear_dirty },
  { "title", f_title },
  { "cwd", f_cwd },
  { "poll_events", f_poll_events },
  { "update_render", f_update_render },
  { NULL, NULL },
};

static const luaL_Reg module_methods[] = {
  { "new", f_new },
  { NULL, NULL },
};

static int open_module(lua_State *L) {
  luaL_newmetatable(L, LXL_GHOSTTY_USERDATA);
  luaL_setfuncs(L, terminal_methods, 0);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);

  lua_newtable(L);
  luaL_setfuncs(L, module_methods, 0);
  lua_pushliteral(L, "0.1.0");
  lua_setfield(L, -2, "version");
  return 1;
}

#ifdef LXL_GHOSTTY_STANDALONE
int luaopen_libghostty_lxl(lua_State *L) {
  return open_module(L);
}
#else
int luaopen_lite_xl_libghostty_lxl(lua_State *L, void *XL) {
  lite_xl_plugin_init(XL);
  return open_module(L);
}
#endif
