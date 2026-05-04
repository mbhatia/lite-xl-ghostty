#ifndef LXL_GHOSTTY_PTY_UNIX_H
#define LXL_GHOSTTY_PTY_UNIX_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct {
  char **argv;
  const char *cwd;
  char **env;
  uint16_t cols;
  uint16_t rows;
} LxlGhosttyPtySpawnOptions;

bool lxl_ghostty_pty_spawn(const LxlGhosttyPtySpawnOptions *options,
                           int *out_fd,
                           pid_t *out_pid,
                           char *errbuf,
                           size_t errbuf_len);
void lxl_ghostty_pty_set_nonblocking(int fd);
bool lxl_ghostty_pty_resize(int fd, uint16_t cols, uint16_t rows);
void lxl_ghostty_pty_terminate(pid_t pid);

#endif
