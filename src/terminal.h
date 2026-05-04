#ifndef LXL_GHOSTTY_TERMINAL_H
#define LXL_GHOSTTY_TERMINAL_H

#include "event_queue.h"
#include "osc_observer.h"

#include <ghostty/vt.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
  uint16_t cols;
  uint16_t rows;
  uint32_t cell_width_px;
  uint32_t cell_height_px;
  size_t max_scrollback;
  char **argv;
  char **env;
  const char *cwd;
  size_t osc_max_bytes;
} LxlGhosttyTerminalOptions;

typedef struct {
  pthread_mutex_t mu;
  pthread_t reader_thread;
  atomic_bool reader_started;
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

bool lxl_ghostty_terminal_new(const LxlGhosttyTerminalOptions *options,
                              LxlGhosttyTerminal **out,
                              char *errbuf,
                              size_t errbuf_len);
void lxl_ghostty_terminal_close(LxlGhosttyTerminal *terminal);
bool lxl_ghostty_terminal_resize(LxlGhosttyTerminal *terminal,
                                 uint16_t cols,
                                 uint16_t rows,
                                 uint32_t cell_width_px,
                                 uint32_t cell_height_px);
bool lxl_ghostty_terminal_write(LxlGhosttyTerminal *terminal, const char *data, size_t len);
bool lxl_ghostty_terminal_paste(LxlGhosttyTerminal *terminal,
                                const char *data,
                                size_t len,
                                bool *safe);
void lxl_ghostty_terminal_focus(LxlGhosttyTerminal *terminal, bool focused);
void lxl_ghostty_terminal_scroll(LxlGhosttyTerminal *terminal, intptr_t delta);
void lxl_ghostty_terminal_scroll_top(LxlGhosttyTerminal *terminal);
void lxl_ghostty_terminal_scroll_bottom(LxlGhosttyTerminal *terminal);
bool lxl_ghostty_terminal_bracketed_paste(LxlGhosttyTerminal *terminal);
bool lxl_ghostty_terminal_mouse_tracking(LxlGhosttyTerminal *terminal);
bool lxl_ghostty_terminal_exited(LxlGhosttyTerminal *terminal, int *code, int *signal);
void lxl_ghostty_terminal_poll_state_events(LxlGhosttyTerminal *terminal);

#endif
