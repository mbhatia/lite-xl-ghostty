#include "lxl_ghostty.h"
#include "terminal.h"
#include "utf8.h"

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

static int f_copy_selection(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (!ud->terminal) {
    lua_pushnil(L);
    return 1;
  }

  int start_col = (int)luaL_checkinteger(L, 2) - 1;
  int start_row = (int)luaL_checkinteger(L, 3) - 1;
  int end_col = (int)luaL_checkinteger(L, 4) - 1;
  int end_row = (int)luaL_checkinteger(L, 5) - 1;
  bool rectangle = lua_toboolean(L, 6);
  if (start_col < 0 || start_row < 0 || end_col < 0 || end_row < 0) {
    lua_pushnil(L);
    return 1;
  }

  LxlGhosttyTerminal *t = ud->terminal;
  pthread_mutex_lock(&t->mu);

  GhosttySelection selection = GHOSTTY_INIT_SIZED(GhosttySelection);
  GhosttyPoint start = {
    .tag = GHOSTTY_POINT_TAG_VIEWPORT,
    .value = { .coordinate = { .x = (uint16_t)start_col, .y = (uint32_t)start_row } },
  };
  GhosttyPoint end = {
    .tag = GHOSTTY_POINT_TAG_VIEWPORT,
    .value = { .coordinate = { .x = (uint16_t)end_col, .y = (uint32_t)end_row } },
  };
  GhosttyResult result = ghostty_terminal_grid_ref(t->terminal, start, &selection.start);
  if (result == GHOSTTY_SUCCESS) result = ghostty_terminal_grid_ref(t->terminal, end, &selection.end);
  if (result != GHOSTTY_SUCCESS) {
    pthread_mutex_unlock(&t->mu);
    lua_pushnil(L);
    return 1;
  }
  selection.rectangle = rectangle;

  GhosttyFormatterTerminalOptions options = GHOSTTY_INIT_SIZED(GhosttyFormatterTerminalOptions);
  options.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
  options.unwrap = false;
  options.trim = true;
  options.selection = &selection;
  options.extra = GHOSTTY_INIT_SIZED(GhosttyFormatterTerminalExtra);
  options.extra.screen = GHOSTTY_INIT_SIZED(GhosttyFormatterScreenExtra);

  GhosttyFormatter formatter = NULL;
  result = ghostty_formatter_terminal_new(NULL, &formatter, t->terminal, options);
  if (result != GHOSTTY_SUCCESS) {
    pthread_mutex_unlock(&t->mu);
    lua_pushnil(L);
    return 1;
  }

  size_t needed = 0;
  result = ghostty_formatter_format_buf(formatter, NULL, 0, &needed);
  if (result != GHOSTTY_OUT_OF_SPACE && result != GHOSTTY_SUCCESS) {
    ghostty_formatter_free(formatter);
    pthread_mutex_unlock(&t->mu);
    lua_pushnil(L);
    return 1;
  }

  uint8_t *buf = needed ? (uint8_t *)malloc(needed) : NULL;
  if (needed && !buf) {
    ghostty_formatter_free(formatter);
    pthread_mutex_unlock(&t->mu);
    return luaL_error(L, "out of memory");
  }
  result = ghostty_formatter_format_buf(formatter, buf, needed, &needed);
  ghostty_formatter_free(formatter);
  pthread_mutex_unlock(&t->mu);

  if (result == GHOSTTY_SUCCESS) lua_pushlstring(L, (const char *)buf, needed);
  else lua_pushnil(L);
  free(buf);
  return 1;
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

static GhosttyKey key_from_name(const char *key) {
  if (!key) return GHOSTTY_KEY_UNIDENTIFIED;
  if (strcmp(key, "return") == 0 || strcmp(key, "enter") == 0) return GHOSTTY_KEY_ENTER;
  if (strcmp(key, "backspace") == 0) return GHOSTTY_KEY_BACKSPACE;
  if (strcmp(key, "delete") == 0) return GHOSTTY_KEY_DELETE;
  if (strcmp(key, "tab") == 0) return GHOSTTY_KEY_TAB;
  if (strcmp(key, "escape") == 0) return GHOSTTY_KEY_ESCAPE;
  if (strcmp(key, "up") == 0) return GHOSTTY_KEY_ARROW_UP;
  if (strcmp(key, "down") == 0) return GHOSTTY_KEY_ARROW_DOWN;
  if (strcmp(key, "left") == 0) return GHOSTTY_KEY_ARROW_LEFT;
  if (strcmp(key, "right") == 0) return GHOSTTY_KEY_ARROW_RIGHT;
  if (strcmp(key, "home") == 0) return GHOSTTY_KEY_HOME;
  if (strcmp(key, "end") == 0) return GHOSTTY_KEY_END;
  if (strcmp(key, "pageup") == 0) return GHOSTTY_KEY_PAGE_UP;
  if (strcmp(key, "pagedown") == 0) return GHOSTTY_KEY_PAGE_DOWN;
  if (strcmp(key, "insert") == 0) return GHOSTTY_KEY_INSERT;
  if (key[0] == 'f') {
    int n = atoi(key + 1);
    if (n >= 1 && n <= 12) return (GhosttyKey)(GHOSTTY_KEY_F1 + (n - 1));
  }
  if (strlen(key) == 1) {
    char c = key[0];
    if (c >= 'a' && c <= 'z') return (GhosttyKey)(GHOSTTY_KEY_A + (c - 'a'));
    if (c >= '0' && c <= '9') return (GhosttyKey)(GHOSTTY_KEY_DIGIT_0 + (c - '0'));
    if (c == ' ') return GHOSTTY_KEY_SPACE;
  }
  return GHOSTTY_KEY_UNIDENTIFIED;
}

static GhosttyMods mods_from_table(lua_State *L, int index) {
  GhosttyMods mods = 0;
  if (!lua_istable(L, index)) return mods;
  lua_getfield(L, index, "shift");
  if (lua_toboolean(L, -1)) mods |= GHOSTTY_MODS_SHIFT;
  lua_pop(L, 1);
  lua_getfield(L, index, "ctrl");
  if (lua_toboolean(L, -1)) mods |= GHOSTTY_MODS_CTRL;
  lua_pop(L, 1);
  lua_getfield(L, index, "alt");
  if (lua_toboolean(L, -1)) mods |= GHOSTTY_MODS_ALT;
  lua_pop(L, 1);
  lua_getfield(L, index, "cmd");
  if (lua_toboolean(L, -1)) mods |= GHOSTTY_MODS_SUPER;
  lua_pop(L, 1);
  return mods;
}

static int f_send_key(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (!ud->terminal) {
    lua_pushboolean(L, 0);
    return 1;
  }
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_getfield(L, 2, "key");
  const char *key_name = lua_tostring(L, -1);
  GhosttyKey gkey = key_from_name(key_name);
  lua_pop(L, 1);
  if (gkey == GHOSTTY_KEY_UNIDENTIFIED) {
    lua_pushboolean(L, 0);
    return 1;
  }

  lua_getfield(L, 2, "mods");
  GhosttyMods mods = mods_from_table(L, lua_gettop(L));
  lua_pop(L, 1);
  lua_getfield(L, 2, "text");
  size_t text_len = 0;
  const char *text = lua_tolstring(L, -1, &text_len);

  LxlGhosttyTerminal *t = ud->terminal;
  char buf[256];
  size_t written = 0;
  pthread_mutex_lock(&t->mu);
  ghostty_key_encoder_setopt_from_terminal(t->key_encoder, t->terminal);
  GhosttyOptionAsAlt option_as_alt = GHOSTTY_OPTION_AS_ALT_TRUE;
  ghostty_key_encoder_setopt(t->key_encoder, GHOSTTY_KEY_ENCODER_OPT_MACOS_OPTION_AS_ALT, &option_as_alt);
  ghostty_key_event_set_key(t->key_event, gkey);
  ghostty_key_event_set_action(t->key_event, GHOSTTY_KEY_ACTION_PRESS);
  ghostty_key_event_set_mods(t->key_event, mods);
  ghostty_key_event_set_consumed_mods(t->key_event, 0);
  ghostty_key_event_set_utf8(t->key_event, text, text_len);
  GhosttyResult result = ghostty_key_encoder_encode(t->key_encoder, t->key_event, buf, sizeof(buf), &written);
  pthread_mutex_unlock(&t->mu);
  lua_pop(L, 1);
  if (result == GHOSTTY_SUCCESS && written > 0) {
    lua_pushboolean(L, lxl_ghostty_terminal_write(t, buf, written));
  } else {
    lua_pushboolean(L, 0);
  }
  return 1;
}

static GhosttyMouseButton mouse_button_from_name(const char *button) {
  if (!button) return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
  if (strcmp(button, "left") == 0) return GHOSTTY_MOUSE_BUTTON_LEFT;
  if (strcmp(button, "right") == 0) return GHOSTTY_MOUSE_BUTTON_RIGHT;
  if (strcmp(button, "middle") == 0) return GHOSTTY_MOUSE_BUTTON_MIDDLE;
  if (strcmp(button, "wheel_up") == 0) return GHOSTTY_MOUSE_BUTTON_FOUR;
  if (strcmp(button, "wheel_down") == 0) return GHOSTTY_MOUSE_BUTTON_FIVE;
  return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
}

static GhosttyMouseAction mouse_action_from_name(const char *action) {
  if (!action) return GHOSTTY_MOUSE_ACTION_PRESS;
  if (strcmp(action, "release") == 0) return GHOSTTY_MOUSE_ACTION_RELEASE;
  if (strcmp(action, "motion") == 0) return GHOSTTY_MOUSE_ACTION_MOTION;
  return GHOSTTY_MOUSE_ACTION_PRESS;
}

static int f_send_mouse(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (!ud->terminal) {
    lua_pushboolean(L, 0);
    return 1;
  }
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_getfield(L, 2, "action");
  GhosttyMouseAction action = mouse_action_from_name(lua_tostring(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, 2, "button");
  GhosttyMouseButton button = mouse_button_from_name(lua_tostring(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, 2, "mods");
  GhosttyMods mods = mods_from_table(L, lua_gettop(L));
  lua_pop(L, 1);
  lua_getfield(L, 2, "x");
  float x = (float)luaL_optnumber(L, -1, 0);
  lua_pop(L, 1);
  lua_getfield(L, 2, "y");
  float y = (float)luaL_optnumber(L, -1, 0);
  lua_pop(L, 1);

  LxlGhosttyTerminal *t = ud->terminal;
  char buf[256];
  size_t written = 0;
  pthread_mutex_lock(&t->mu);
  ghostty_mouse_encoder_setopt_from_terminal(t->mouse_encoder, t->terminal);
  GhosttyMouseEncoderSize size = {
    .size = sizeof(GhosttyMouseEncoderSize),
    .screen_width = (uint32_t)t->cols * t->cell_width_px,
    .screen_height = (uint32_t)t->rows * t->cell_height_px,
    .cell_width = t->cell_width_px,
    .cell_height = t->cell_height_px,
  };
  ghostty_mouse_encoder_setopt(t->mouse_encoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size);
  ghostty_mouse_event_set_action(t->mouse_event, action);
  if (button == GHOSTTY_MOUSE_BUTTON_UNKNOWN || action == GHOSTTY_MOUSE_ACTION_MOTION) ghostty_mouse_event_clear_button(t->mouse_event);
  else ghostty_mouse_event_set_button(t->mouse_event, button);
  ghostty_mouse_event_set_mods(t->mouse_event, mods);
  ghostty_mouse_event_set_position(t->mouse_event, (GhosttyMousePosition){ .x = x, .y = y });
  GhosttyResult result = ghostty_mouse_encoder_encode(t->mouse_encoder, t->mouse_event, buf, sizeof(buf), &written);
  pthread_mutex_unlock(&t->mu);
  if (result == GHOSTTY_SUCCESS && written > 0) {
    lua_pushboolean(L, lxl_ghostty_terminal_write(t, buf, written));
  } else {
    lua_pushboolean(L, 0);
  }
  return 1;
}

static int f_unhandled_false(lua_State *L) {
  (void)L;
  lua_pushboolean(L, 0);
  return 1;
}

static int f_hyperlink_at(lua_State *L) {
  LuaTerminal *ud = check_terminal(L, 1);
  if (!ud->terminal) {
    lua_pushnil(L);
    return 1;
  }
  int col = (int)luaL_checkinteger(L, 2) - 1;
  int row = (int)luaL_checkinteger(L, 3) - 1;
  if (col < 0 || row < 0) {
    lua_pushnil(L);
    return 1;
  }
  LxlGhosttyTerminal *t = ud->terminal;
  pthread_mutex_lock(&t->mu);
  GhosttyGridRef ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
  GhosttyPoint point = {
    .tag = GHOSTTY_POINT_TAG_VIEWPORT,
    .value = { .coordinate = { .x = (uint16_t)col, .y = (uint32_t)row } },
  };
  GhosttyResult result = ghostty_terminal_grid_ref(t->terminal, point, &ref);
  size_t len = 0;
  if (result == GHOSTTY_SUCCESS) result = ghostty_grid_ref_hyperlink_uri(&ref, NULL, 0, &len);
  if (result != GHOSTTY_OUT_OF_SPACE || len == 0) {
    pthread_mutex_unlock(&t->mu);
    lua_pushnil(L);
    return 1;
  }
  uint8_t *buf = (uint8_t *)malloc(len);
  if (!buf) {
    pthread_mutex_unlock(&t->mu);
    lua_pushnil(L);
    return 1;
  }
  result = ghostty_grid_ref_hyperlink_uri(&ref, buf, len, &len);
  pthread_mutex_unlock(&t->mu);
  if (result == GHOSTTY_SUCCESS) lua_pushlstring(L, (const char *)buf, len);
  else lua_pushnil(L);
  free(buf);
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
  LxlGhosttyTerminal *t = ud->terminal;
  pthread_mutex_lock(&t->mu);
  GhosttyResult result = ghostty_render_state_update(t->render_state, t->terminal);
  if (result != GHOSTTY_SUCCESS) {
    pthread_mutex_unlock(&t->mu);
    lua_pushnil(L);
    return 1;
  }
  uint16_t cols = t->cols;
  uint16_t rows = t->rows;
  GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
  ghostty_render_state_colors_get(t->render_state, &colors);

  lua_newtable(L);
  lua_pushinteger(L, cols);
  lua_setfield(L, -2, "cols");
  lua_pushinteger(L, rows);
  lua_setfield(L, -2, "rows");

  lua_newtable(L);
  lua_pushinteger(L, colors.background.r);
  lua_rawseti(L, -2, 1);
  lua_pushinteger(L, colors.background.g);
  lua_rawseti(L, -2, 2);
  lua_pushinteger(L, colors.background.b);
  lua_rawseti(L, -2, 3);
  lua_setfield(L, -2, "background");

  lua_newtable(L);
  lua_pushinteger(L, colors.foreground.r);
  lua_rawseti(L, -2, 1);
  lua_pushinteger(L, colors.foreground.g);
  lua_rawseti(L, -2, 2);
  lua_pushinteger(L, colors.foreground.b);
  lua_rawseti(L, -2, 3);
  lua_setfield(L, -2, "foreground");

  bool cursor_visible = false;
  bool cursor_has_value = false;
  uint16_t cursor_x = 0;
  uint16_t cursor_y = 0;
  ghostty_render_state_get(t->render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursor_visible);
  ghostty_render_state_get(t->render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursor_has_value);
  if (cursor_has_value) {
    ghostty_render_state_get(t->render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cursor_x);
    ghostty_render_state_get(t->render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cursor_y);
  }
  lua_newtable(L);
  lua_pushinteger(L, cursor_x);
  lua_setfield(L, -2, "x");
  lua_pushinteger(L, cursor_y);
  lua_setfield(L, -2, "y");
  lua_pushboolean(L, cursor_visible && cursor_has_value);
  lua_setfield(L, -2, "visible");
  lua_setfield(L, -2, "cursor");

  GhosttyTerminalScrollbar scrollbar = {0};
  if (ghostty_terminal_get(t->terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar) == GHOSTTY_SUCCESS) {
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)scrollbar.total);
    lua_setfield(L, -2, "total");
    lua_pushinteger(L, (lua_Integer)scrollbar.offset);
    lua_setfield(L, -2, "offset");
    lua_pushinteger(L, (lua_Integer)scrollbar.len);
    lua_setfield(L, -2, "len");
    lua_setfield(L, -2, "scrollbar");
  }

  lua_newtable(L);
  if (ghostty_render_state_get(t->render_state, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &t->row_iter) == GHOSTTY_SUCCESS) {
    int row_index = 1;
    while (ghostty_render_state_row_iterator_next(t->row_iter)) {
      bool row_dirty = false;
      ghostty_render_state_row_get(t->row_iter, GHOSTTY_RENDER_STATE_ROW_DATA_DIRTY, &row_dirty);
      lua_newtable(L);
      lua_pushboolean(L, row_dirty);
      lua_setfield(L, -2, "dirty");
      lua_pushliteral(L, "none");
      lua_setfield(L, -2, "semantic");
      lua_newtable(L);
      if (ghostty_render_state_row_get(t->row_iter, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &t->row_cells) == GHOSTTY_SUCCESS) {
        int span_index = 1;
        int x = 0;
        while (ghostty_render_state_row_cells_next(t->row_cells)) {
          uint32_t grapheme_len = 0;
          ghostty_render_state_row_cells_get(t->row_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &grapheme_len);
          if (grapheme_len > 0) {
            uint32_t stack_buf[16];
            uint32_t *codepoints = stack_buf;
            if (grapheme_len > 16) codepoints = (uint32_t *)malloc(sizeof(uint32_t) * grapheme_len);
            if (codepoints) {
              ghostty_render_state_row_cells_get(t->row_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, codepoints);
              char text_buf[256];
              size_t text_len = 0;
              for (uint32_t i = 0; i < grapheme_len && text_len + 4 <= sizeof(text_buf); i++) {
                text_len += lxl_ghostty_utf8_encode(codepoints[i], text_buf + text_len);
              }
              if (grapheme_len > 16) free(codepoints);

              GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
              GhosttyColorRgb fg = colors.foreground;
              GhosttyColorRgb bg = colors.background;
              bool has_bg = false;
              ghostty_render_state_row_cells_get(t->row_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);
              ghostty_render_state_row_cells_get(t->row_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);
              has_bg = ghostty_render_state_row_cells_get(t->row_cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg) == GHOSTTY_SUCCESS;

              lua_newtable(L);
              lua_pushinteger(L, x);
              lua_setfield(L, -2, "x");
              lua_pushlstring(L, text_buf, text_len);
              lua_setfield(L, -2, "text");
              lua_newtable(L);
              lua_pushinteger(L, fg.r);
              lua_rawseti(L, -2, 1);
              lua_pushinteger(L, fg.g);
              lua_rawseti(L, -2, 2);
              lua_pushinteger(L, fg.b);
              lua_rawseti(L, -2, 3);
              lua_setfield(L, -2, "fg");
              if (has_bg) {
                lua_newtable(L);
                lua_pushinteger(L, bg.r);
                lua_rawseti(L, -2, 1);
                lua_pushinteger(L, bg.g);
                lua_rawseti(L, -2, 2);
                lua_pushinteger(L, bg.b);
                lua_rawseti(L, -2, 3);
                lua_setfield(L, -2, "bg");
              }
              lua_pushboolean(L, style.bold);
              lua_setfield(L, -2, "bold");
              lua_pushboolean(L, style.italic);
              lua_setfield(L, -2, "italic");
              lua_pushboolean(L, style.underline != 0);
              lua_setfield(L, -2, "underline");
              lua_pushboolean(L, style.strikethrough);
              lua_setfield(L, -2, "strikethrough");
              lua_pushboolean(L, style.inverse);
              lua_setfield(L, -2, "inverse");
              lua_rawseti(L, -2, span_index++);
            }
          }
          x++;
        }
      }
      lua_setfield(L, -2, "spans");
      bool clean = false;
      ghostty_render_state_row_set(t->row_iter, GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY, &clean);
      lua_rawseti(L, -2, row_index++);
    }
  }
  lua_setfield(L, -2, "rows_data");
  GhosttyRenderStateDirty clean_state = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
  ghostty_render_state_set(t->render_state, GHOSTTY_RENDER_STATE_OPTION_DIRTY, &clean_state);
  pthread_mutex_unlock(&t->mu);
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
  { "copy_selection", f_copy_selection },
  { "focus", f_focus },
  { "scroll", f_scroll },
  { "scroll_top", f_scroll_top },
  { "scroll_bottom", f_scroll_bottom },
  { "bracketed_paste", f_bracketed_paste },
  { "mouse_tracking", f_mouse_tracking },
  { "send_key", f_send_key },
  { "send_mouse", f_send_mouse },
  { "hyperlink_at", f_hyperlink_at },
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

int luaopen_ghostty_lxl(lua_State *L) {
  return open_module(L);
}
#else
int luaopen_lite_xl_libghostty_lxl(lua_State *L, void *XL) {
  lite_xl_plugin_init(XL);
  return open_module(L);
}

int luaopen_lite_xl_ghostty_lxl(lua_State *L, void *XL) {
  lite_xl_plugin_init(XL);
  return open_module(L);
}
#endif
