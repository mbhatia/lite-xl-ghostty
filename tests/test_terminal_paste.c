#include "terminal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    abort(); \
  } \
} while (0)

enum { PASTE_SIZE = 2 * 1024 * 1024 };
static const char paste_start[] = "\033[200~";
static const char paste_end[] = "\033[201~";
static const char following[] = "later-input";

static char paste_byte(size_t i) { return (char)('a' + (i % 26)); }

static int child_main(bool stalled) {
  struct termios term;
  if (tcgetattr(STDIN_FILENO, &term) != 0) return 2;
  cfmakeraw(&term);
  if (tcsetattr(STDIN_FILENO, TCSANOW, &term) != 0) return 3;
  static const char enable[] = "\033[?2004h";
  if (write(STDOUT_FILENO, enable, sizeof(enable) - 1) != (ssize_t)(sizeof(enable) - 1)) return 4;
  if (stalled) {
    for (;;) sleep(60);
  }
  usleep(200000); /* Let the parent observe bracketed-paste mode before reading. */

  size_t expected_len = sizeof(paste_start) - 1 + PASTE_SIZE + sizeof(paste_end) - 1 + sizeof(following) - 1;
  size_t offset = 0;
  char buf[8192];
  while (offset < expected_len) {
    size_t amount = expected_len - offset;
    if (amount > sizeof(buf)) amount = sizeof(buf);
    ssize_t n = read(STDIN_FILENO, buf, amount);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return 5;
    for (ssize_t j = 0; j < n; j++, offset++) {
      char expected;
      if (offset < sizeof(paste_start) - 1) expected = paste_start[offset];
      else if (offset < sizeof(paste_start) - 1 + PASTE_SIZE)
        expected = paste_byte(offset - (sizeof(paste_start) - 1));
      else if (offset < sizeof(paste_start) - 1 + PASTE_SIZE + sizeof(paste_end) - 1)
        expected = paste_end[offset - (sizeof(paste_start) - 1 + PASTE_SIZE)];
      else expected = following[offset - (sizeof(paste_start) - 1 + PASTE_SIZE + sizeof(paste_end) - 1)];
      if (buf[j] != expected) return 6;
    }
  }
  return 0;
}

static void wait_for_bracketed_paste(LxlGhosttyTerminal *terminal) {
  for (int i = 0; i < 500; i++) {
    if (lxl_ghostty_terminal_bracketed_paste(terminal)) return;
    usleep(10000);
  }
  CHECK(false && "child did not enable bracketed paste");
}

static bool wait_for_exit(LxlGhosttyTerminal *terminal, int *code) {
  for (int i = 0; i < 1000; i++) {
    if (lxl_ghostty_terminal_exited(terminal, code, NULL)) return true;
    usleep(10000);
  }
  return false;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--child") == 0) return child_main(false);
  if (argc == 2 && strcmp(argv[1], "--stalled") == 0) return child_main(true);

  LxlGhosttyTerminalOptions options = {
    .argv = (char *[]){ argv[0], "--child", NULL },
    .cols = 80, .rows = 24,
  };
  LxlGhosttyTerminal *terminal = NULL;
  char error[256] = {0};
  CHECK(lxl_ghostty_terminal_new(&options, &terminal, error, sizeof(error)));
  wait_for_bracketed_paste(terminal);

  char *payload = malloc(PASTE_SIZE);
  CHECK(payload);
  for (size_t i = 0; i < PASTE_SIZE; i++) payload[i] = paste_byte(i);
  bool safe = false;
  CHECK(lxl_ghostty_terminal_paste(terminal, payload, PASTE_SIZE, &safe));
  CHECK(safe);
  CHECK(lxl_ghostty_terminal_write(terminal, following, sizeof(following) - 1));
  free(payload);
  int code = -1;
  CHECK(wait_for_exit(terminal, &code));
  CHECK(code == 0);
  lxl_ghostty_terminal_close(terminal);

  /* Closing with a large write still queued must stop the poller and release it. */
  options.argv = (char *[]){ argv[0], "--stalled", NULL };
  CHECK(lxl_ghostty_terminal_new(&options, &terminal, error, sizeof(error)));
  wait_for_bracketed_paste(terminal);
  char *large = malloc(PASTE_SIZE);
  CHECK(large);
  memset(large, 'z', PASTE_SIZE);
  CHECK(lxl_ghostty_terminal_write(terminal, large, PASTE_SIZE));
  pthread_mutex_lock(&terminal->write_mu);
  CHECK(terminal->write_head != NULL);
  pthread_mutex_unlock(&terminal->write_mu);
  free(large);
  lxl_ghostty_terminal_close(terminal);
  return 0;
}
