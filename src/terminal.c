#include "terminal.h"

#include "pty_unix.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>

#define READ_BUF_SIZE (16 * 1024)
#define DEFAULT_SCROLLBACK 10000
#define DEFAULT_COLS 80
#define DEFAULT_ROWS 24
#define DEFAULT_CELL_WIDTH 8
#define DEFAULT_CELL_HEIGHT 16
#define VERSION_STRING "lite-xl-ghostty 0.1.0"

static void push_simple_event(LxlGhosttyTerminal *t, LxlGhosttyEventKind kind) {
  LxlGhosttyEvent event = { .kind = kind };
  lxl_ghostty_event_queue_push(&t->events, &event);
}

static void effect_write_pty(GhosttyTerminal terminal, void *userdata, const uint8_t *data, size_t len) {
  (void)terminal;
  LxlGhosttyTerminal *t = (LxlGhosttyTerminal *)userdata;
  if (!t || t->pty_fd < 0 || !data || len == 0) return;
  ssize_t written = write(t->pty_fd, data, len);
  if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    (void)write(t->pty_fd, data, len);
  }
}

static void effect_bell(GhosttyTerminal terminal, void *userdata) {
  (void)terminal;
  LxlGhosttyTerminal *t = (LxlGhosttyTerminal *)userdata;
  if (t) push_simple_event(t, LXL_GHOSTTY_EVENT_BELL);
}

static GhosttyString effect_enquiry(GhosttyTerminal terminal, void *userdata) {
  (void)terminal;
  (void)userdata;
  static const uint8_t response[] = "lite-xl-ghostty";
  return (GhosttyString){ .ptr = response, .len = sizeof(response) - 1 };
}

static GhosttyString effect_xtversion(GhosttyTerminal terminal, void *userdata) {
  (void)terminal;
  (void)userdata;
  static const uint8_t version[] = VERSION_STRING;
  return (GhosttyString){ .ptr = version, .len = sizeof(version) - 1 };
}

static bool effect_size(GhosttyTerminal terminal, void *userdata, GhosttySizeReportSize *out_size) {
  (void)terminal;
  LxlGhosttyTerminal *t = (LxlGhosttyTerminal *)userdata;
  if (!t || !out_size) return false;
  out_size->rows = t->rows;
  out_size->columns = t->cols;
  out_size->cell_width = t->cell_width_px;
  out_size->cell_height = t->cell_height_px;
  return true;
}

static bool effect_device_attributes(GhosttyTerminal terminal, void *userdata, GhosttyDeviceAttributes *out_attrs) {
  (void)terminal;
  (void)userdata;
  if (!out_attrs) return false;
  memset(out_attrs, 0, sizeof(*out_attrs));
  out_attrs->primary.conformance_level = GHOSTTY_DA_CONFORMANCE_VT220;
  out_attrs->primary.features[0] = GHOSTTY_DA_FEATURE_COLUMNS_132;
  out_attrs->primary.features[1] = GHOSTTY_DA_FEATURE_SELECTIVE_ERASE;
  out_attrs->primary.features[2] = GHOSTTY_DA_FEATURE_ANSI_COLOR;
  out_attrs->primary.num_features = 3;
  out_attrs->secondary.device_type = GHOSTTY_DA_DEVICE_TYPE_VT220;
  out_attrs->secondary.firmware_version = 1;
  out_attrs->secondary.rom_cartridge = 0;
  out_attrs->tertiary.unit_id = 0;
  return true;
}

static bool effect_color_scheme(GhosttyTerminal terminal, void *userdata, GhosttyColorScheme *out_scheme) {
  (void)terminal;
  (void)userdata;
  if (!out_scheme) return false;
  *out_scheme = GHOSTTY_COLOR_SCHEME_DARK;
  return true;
}

static char *copy_ghostty_string(GhosttyString s) {
  return lxl_ghostty_strdup_len((const char *)s.ptr, s.len);
}

static void emit_string_change(LxlGhosttyTerminal *t,
                               GhosttyTerminalData data,
                               char **last,
                               LxlGhosttyEventKind kind) {
  GhosttyString value = {0};
  if (ghostty_terminal_get(t->terminal, data, &value) != GHOSTTY_SUCCESS || !value.ptr) return;
  if (*last && strlen(*last) == value.len && memcmp(*last, value.ptr, value.len) == 0) return;
  char *copy = copy_ghostty_string(value);
  if (!copy) return;
  LxlGhosttyEvent event = { .kind = kind };
  if (kind == LXL_GHOSTTY_EVENT_TITLE) event.title = copy;
  else event.body = copy;
  lxl_ghostty_event_queue_push(&t->events, &event);
  free(*last);
  *last = copy;
}

static void effect_title_changed(GhosttyTerminal terminal, void *userdata) {
  (void)terminal;
  LxlGhosttyTerminal *t = (LxlGhosttyTerminal *)userdata;
  if (t) emit_string_change(t, GHOSTTY_TERMINAL_DATA_TITLE, &t->last_title, LXL_GHOSTTY_EVENT_TITLE);
}

void lxl_ghostty_terminal_poll_state_events(LxlGhosttyTerminal *t) {
  if (!t || !t->terminal) return;
  pthread_mutex_lock(&t->mu);
  emit_string_change(t, GHOSTTY_TERMINAL_DATA_PWD, &t->last_pwd, LXL_GHOSTTY_EVENT_CWD);
  pthread_mutex_unlock(&t->mu);
}

static bool configure_ghostty(LxlGhosttyTerminal *t, size_t max_scrollback) {
  GhosttyTerminalOptions opts = {
    .cols = t->cols,
    .rows = t->rows,
    .max_scrollback = max_scrollback ? max_scrollback : DEFAULT_SCROLLBACK,
  };
  if (ghostty_terminal_new(NULL, &t->terminal, opts) != GHOSTTY_SUCCESS) return false;
  if (ghostty_render_state_new(NULL, &t->render_state) != GHOSTTY_SUCCESS) return false;
  if (ghostty_render_state_row_iterator_new(NULL, &t->row_iter) != GHOSTTY_SUCCESS) return false;
  if (ghostty_render_state_row_cells_new(NULL, &t->row_cells) != GHOSTTY_SUCCESS) return false;
  if (ghostty_key_encoder_new(NULL, &t->key_encoder) != GHOSTTY_SUCCESS) return false;
  if (ghostty_key_event_new(NULL, &t->key_event) != GHOSTTY_SUCCESS) return false;
  if (ghostty_mouse_encoder_new(NULL, &t->mouse_encoder) != GHOSTTY_SUCCESS) return false;
  if (ghostty_mouse_event_new(NULL, &t->mouse_event) != GHOSTTY_SUCCESS) return false;

  uint64_t zero = 0;
  ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_USERDATA, t);
  ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY, (const void *)effect_write_pty);
  ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_BELL, (const void *)effect_bell);
  ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_ENQUIRY, (const void *)effect_enquiry);
  ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_XTVERSION, (const void *)effect_xtversion);
  ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED, (const void *)effect_title_changed);
  ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_SIZE, (const void *)effect_size);
  ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_COLOR_SCHEME, (const void *)effect_color_scheme);
  ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_DEVICE_ATTRIBUTES, (const void *)effect_device_attributes);
  ghostty_terminal_set(t->terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_STORAGE_LIMIT, &zero);
  ghostty_terminal_resize(t->terminal, t->cols, t->rows, t->cell_width_px, t->cell_height_px);
  return true;
}

static void push_exit_event(LxlGhosttyTerminal *t, int status) {
  LxlGhosttyEvent event = { .kind = LXL_GHOSTTY_EVENT_EXIT };
  if (WIFEXITED(status)) event.code = WEXITSTATUS(status);
  if (WIFSIGNALED(status)) event.signal = WTERMSIG(status);
  lxl_ghostty_event_queue_push(&t->events, &event);
}

static void *reader_main(void *arg) {
  LxlGhosttyTerminal *t = (LxlGhosttyTerminal *)arg;
  char buf[READ_BUF_SIZE];

  while (!atomic_load(&t->stopping)) {
    struct pollfd pfd = { .fd = t->pty_fd, .events = POLLIN | POLLHUP | POLLERR };
    int pr = poll(&pfd, 1, 50);
    if (pr < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (pr == 0) {
      int status = 0;
      pid_t result = waitpid(t->child_pid, &status, WNOHANG);
      if (result == t->child_pid) {
        t->exit_status = status;
        atomic_store(&t->child_alive, false);
        push_exit_event(t, status);
        break;
      }
      continue;
    }

    for (;;) {
      ssize_t n = read(t->pty_fd, buf, sizeof(buf));
      if (n > 0) {
        lxl_ghostty_osc_observer_feed(&t->osc_observer, buf, (size_t)n);
        pthread_mutex_lock(&t->mu);
        ghostty_terminal_vt_write(t->terminal, (const uint8_t *)buf, (size_t)n);
        emit_string_change(t, GHOSTTY_TERMINAL_DATA_PWD, &t->last_pwd, LXL_GHOSTTY_EVENT_CWD);
        pthread_mutex_unlock(&t->mu);
        atomic_store(&t->dirty, true);
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
      int status = 0;
      pid_t result = waitpid(t->child_pid, &status, WNOHANG);
      if (result == t->child_pid) {
        t->exit_status = status;
        atomic_store(&t->child_alive, false);
        push_exit_event(t, status);
      }
      atomic_store(&t->stopping, true);
      break;
    }
  }
  return NULL;
}

bool lxl_ghostty_terminal_new(const LxlGhosttyTerminalOptions *options,
                              LxlGhosttyTerminal **out,
                              char *errbuf,
                              size_t errbuf_len) {
  if (!options || !out) return false;
  LxlGhosttyTerminal *t = (LxlGhosttyTerminal *)calloc(1, sizeof(*t));
  if (!t) return false;
  t->pty_fd = -1;
  t->cols = options->cols ? options->cols : DEFAULT_COLS;
  t->rows = options->rows ? options->rows : DEFAULT_ROWS;
  t->cell_width_px = options->cell_width_px ? options->cell_width_px : DEFAULT_CELL_WIDTH;
  t->cell_height_px = options->cell_height_px ? options->cell_height_px : DEFAULT_CELL_HEIGHT;
  atomic_store(&t->child_alive, true);

  if (pthread_mutex_init(&t->mu, NULL) != 0 ||
      !lxl_ghostty_event_queue_init(&t->events) ||
      !lxl_ghostty_osc_observer_init(&t->osc_observer, &t->events, options->osc_max_bytes) ||
      !configure_ghostty(t, options->max_scrollback)) {
    if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "failed to initialize terminal state");
    lxl_ghostty_terminal_close(t);
    return false;
  }

  LxlGhosttyPtySpawnOptions spawn = {
    .argv = options->argv,
    .cwd = options->cwd,
    .env = options->env,
    .cols = t->cols,
    .rows = t->rows,
  };
  if (!lxl_ghostty_pty_spawn(&spawn, &t->pty_fd, &t->child_pid, errbuf, errbuf_len)) {
    lxl_ghostty_terminal_close(t);
    return false;
  }
  if (pthread_create(&t->reader_thread, NULL, reader_main, t) != 0) {
    if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "failed to start reader thread");
    lxl_ghostty_terminal_close(t);
    return false;
  }
  atomic_store(&t->reader_started, true);
  *out = t;
  return true;
}

void lxl_ghostty_terminal_close(LxlGhosttyTerminal *t) {
  if (!t) return;
  atomic_store(&t->stopping, true);
  if (t->pty_fd >= 0) close(t->pty_fd);
  if (atomic_load(&t->reader_started)) pthread_join(t->reader_thread, NULL);
  if (atomic_load(&t->child_alive)) lxl_ghostty_pty_terminate(t->child_pid);

  ghostty_mouse_event_free(t->mouse_event);
  ghostty_mouse_encoder_free(t->mouse_encoder);
  ghostty_key_event_free(t->key_event);
  ghostty_key_encoder_free(t->key_encoder);
  ghostty_render_state_row_cells_free(t->row_cells);
  ghostty_render_state_row_iterator_free(t->row_iter);
  ghostty_render_state_free(t->render_state);
  ghostty_terminal_free(t->terminal);
  lxl_ghostty_osc_observer_deinit(&t->osc_observer);
  lxl_ghostty_event_queue_deinit(&t->events);
  pthread_mutex_destroy(&t->mu);
  free(t->last_title);
  free(t->last_pwd);
  free(t);
}

bool lxl_ghostty_terminal_resize(LxlGhosttyTerminal *t,
                                 uint16_t cols,
                                 uint16_t rows,
                                 uint32_t cell_width_px,
                                 uint32_t cell_height_px) {
  if (!t || cols == 0 || rows == 0 || cell_width_px == 0 || cell_height_px == 0) return false;
  pthread_mutex_lock(&t->mu);
  t->cols = cols;
  t->rows = rows;
  t->cell_width_px = cell_width_px;
  t->cell_height_px = cell_height_px;
  GhosttyResult result = ghostty_terminal_resize(t->terminal, cols, rows, cell_width_px, cell_height_px);
  pthread_mutex_unlock(&t->mu);
  lxl_ghostty_pty_resize(t->pty_fd, cols, rows);
  atomic_store(&t->dirty, true);
  return result == GHOSTTY_SUCCESS;
}

bool lxl_ghostty_terminal_write(LxlGhosttyTerminal *t, const char *data, size_t len) {
  if (!t || t->pty_fd < 0 || !data || len == 0) return false;
  size_t off = 0;
  while (off < len) {
    ssize_t n = write(t->pty_fd, data + off, len - off);
    if (n > 0) {
      off += (size_t)n;
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    } else {
      return false;
    }
  }
  return true;
}

bool lxl_ghostty_terminal_paste(LxlGhosttyTerminal *t,
                                const char *data,
                                size_t len,
                                bool *safe) {
  if (!t || !data) return false;
  bool bracketed = lxl_ghostty_terminal_bracketed_paste(t);
  bool is_safe = ghostty_paste_is_safe(data, len);
  if (safe) *safe = is_safe;
  char *copy = lxl_ghostty_strdup_len(data, len);
  if (!copy) return false;
  size_t needed = 0;
  GhosttyResult result = ghostty_paste_encode(copy, len, bracketed, NULL, 0, &needed);
  if (result != GHOSTTY_OUT_OF_SPACE && result != GHOSTTY_SUCCESS) {
    free(copy);
    return false;
  }
  char *encoded = (char *)malloc(needed + 1);
  if (!encoded) {
    free(copy);
    return false;
  }
  result = ghostty_paste_encode(copy, len, bracketed, encoded, needed, &needed);
  bool ok = result == GHOSTTY_SUCCESS && lxl_ghostty_terminal_write(t, encoded, needed);
  free(encoded);
  free(copy);
  return ok;
}

void lxl_ghostty_terminal_focus(LxlGhosttyTerminal *t, bool focused) {
  if (!t) return;
  char buf[16];
  size_t len = 0;
  if (ghostty_focus_encode(focused ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST, buf, sizeof(buf), &len) == GHOSTTY_SUCCESS) {
    lxl_ghostty_terminal_write(t, buf, len);
  }
}

static void scroll_viewport(LxlGhosttyTerminal *t, GhosttyTerminalScrollViewport sv) {
  if (!t) return;
  pthread_mutex_lock(&t->mu);
  ghostty_terminal_scroll_viewport(t->terminal, sv);
  pthread_mutex_unlock(&t->mu);
  atomic_store(&t->dirty, true);
}

void lxl_ghostty_terminal_scroll(LxlGhosttyTerminal *t, intptr_t delta) {
  GhosttyTerminalScrollViewport sv = {
    .tag = GHOSTTY_SCROLL_VIEWPORT_DELTA,
    .value = { .delta = delta },
  };
  scroll_viewport(t, sv);
}

void lxl_ghostty_terminal_scroll_top(LxlGhosttyTerminal *t) {
  GhosttyTerminalScrollViewport sv = { .tag = GHOSTTY_SCROLL_VIEWPORT_TOP };
  scroll_viewport(t, sv);
}

void lxl_ghostty_terminal_scroll_bottom(LxlGhosttyTerminal *t) {
  GhosttyTerminalScrollViewport sv = { .tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM };
  scroll_viewport(t, sv);
}

bool lxl_ghostty_terminal_bracketed_paste(LxlGhosttyTerminal *t) {
  if (!t) return false;
  bool enabled = false;
  pthread_mutex_lock(&t->mu);
  ghostty_terminal_mode_get(t->terminal, GHOSTTY_MODE_BRACKETED_PASTE, &enabled);
  pthread_mutex_unlock(&t->mu);
  return enabled;
}

bool lxl_ghostty_terminal_mouse_tracking(LxlGhosttyTerminal *t) {
  if (!t) return false;
  bool tracking = false;
  pthread_mutex_lock(&t->mu);
  ghostty_terminal_get(t->terminal, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &tracking);
  pthread_mutex_unlock(&t->mu);
  return tracking;
}

bool lxl_ghostty_terminal_exited(LxlGhosttyTerminal *t, int *code, int *signal_out) {
  if (!t || atomic_load(&t->child_alive)) return false;
  if (code) *code = WIFEXITED(t->exit_status) ? WEXITSTATUS(t->exit_status) : 0;
  if (signal_out) *signal_out = WIFSIGNALED(t->exit_status) ? WTERMSIG(t->exit_status) : 0;
  return true;
}
