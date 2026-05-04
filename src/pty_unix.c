#include "pty_unix.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

static void apply_env(char **env) {
  if (!env) return;
  for (char **it = env; *it; it++) {
    char *eq = strchr(*it, '=');
    if (!eq) continue;
    *eq = '\0';
    setenv(*it, eq + 1, 1);
    *eq = '=';
  }
}

bool lxl_ghostty_pty_spawn(const LxlGhosttyPtySpawnOptions *options,
                           int *out_fd,
                           pid_t *out_pid,
                           char *errbuf,
                           size_t errbuf_len) {
  if (!options || !options->argv || !options->argv[0] || !out_fd || !out_pid) {
    if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "missing PTY spawn options");
    return false;
  }

  struct winsize ws;
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = options->cols ? options->cols : 80;
  ws.ws_row = options->rows ? options->rows : 24;

  int pty_fd = -1;
  pid_t pid = forkpty(&pty_fd, NULL, NULL, &ws);
  if (pid < 0) {
    if (errbuf && errbuf_len) snprintf(errbuf, errbuf_len, "forkpty failed: %s", strerror(errno));
    return false;
  }

  if (pid == 0) {
    if (options->cwd && chdir(options->cwd) != 0) {
      fprintf(stderr, "lite-xl-ghostty: chdir(%s) failed: %s\n", options->cwd, strerror(errno));
    }
    apply_env(options->env);
    execvp(options->argv[0], options->argv);
    fprintf(stderr, "lite-xl-ghostty: execvp(%s) failed: %s\n", options->argv[0], strerror(errno));
    _exit(127);
  }

  *out_fd = pty_fd;
  *out_pid = pid;
  lxl_ghostty_pty_set_nonblocking(pty_fd);
  return true;
}

void lxl_ghostty_pty_set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool lxl_ghostty_pty_resize(int fd, uint16_t cols, uint16_t rows) {
  if (fd < 0) return false;
  struct winsize ws;
  memset(&ws, 0, sizeof(ws));
  ws.ws_col = cols;
  ws.ws_row = rows;
  return ioctl(fd, TIOCSWINSZ, &ws) == 0;
}

void lxl_ghostty_pty_terminate(pid_t pid) {
  if (pid <= 0) return;
  if (kill(pid, 0) == 0) {
    kill(pid, SIGHUP);
    for (int i = 0; i < 20; i++) {
      int status = 0;
      pid_t result = waitpid(pid, &status, WNOHANG);
      if (result == pid || result < 0) return;
      usleep(10000);
    }
    kill(pid, SIGTERM);
  }
}
