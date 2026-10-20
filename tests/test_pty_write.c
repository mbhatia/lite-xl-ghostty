#include "terminal.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    abort(); \
  } \
} while (0)

typedef struct {
  LxlGhosttyTerminal *terminal;
  int receiver;
  char *output;
  size_t output_len;
  size_t expected_len;
} DrainContext;

static void *drain_peer(void *arg) {
  DrainContext *ctx = arg;
  while (ctx->output_len < ctx->expected_len) {
    ssize_t n = read(ctx->receiver, ctx->output + ctx->output_len,
                     ctx->expected_len - ctx->output_len);
    if (n > 0) ctx->output_len += (size_t)n;
    else if (n < 0 && errno == EINTR) continue;
    else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) usleep(1000);
    else break;
    CHECK(lxl_ghostty_terminal_flush_writes(ctx->terminal));
  }
  return NULL;
}

int main(void) {
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  int flags = fcntl(fds[0], F_GETFL, 0);
  CHECK(flags >= 0 && fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) == 0);
  int sndbuf = 4096;
  CHECK(setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0);

  LxlGhosttyTerminal terminal = { .pty_fd = fds[0] };
  CHECK(pthread_mutex_init(&terminal.write_mu, NULL) == 0);
  atomic_init(&terminal.stopping, false);

  const size_t payload_len = 4 * 1024 * 1024;
  char *payload = malloc(payload_len);
  CHECK(payload);
  for (size_t i = 0; i < payload_len; i++) payload[i] = (char)(i * 31u + i / 127u);
  static const char start[] = "\033[200~";
  static const char end[] = "\033[201~";
  static const char later[] = "later-input";
  const size_t expected_len = payload_len + sizeof(start) - 1 + sizeof(end) - 1 + sizeof(later) - 1;
  char *expected = malloc(expected_len);
  char *actual = malloc(expected_len);
  CHECK(expected && actual);
  size_t off = 0;
  memcpy(expected + off, start, sizeof(start) - 1); off += sizeof(start) - 1;
  memcpy(expected + off, payload, payload_len); off += payload_len;
  memcpy(expected + off, end, sizeof(end) - 1); off += sizeof(end) - 1;
  memcpy(expected + off, later, sizeof(later) - 1);

  /* Queue everything before the receiver reads: this forces EAGAIN and proves
     enqueue returns promptly while preserving successive write order. */
  CHECK(lxl_ghostty_terminal_write(&terminal, start, sizeof(start) - 1));
  CHECK(lxl_ghostty_terminal_write(&terminal, payload, payload_len));
  CHECK(lxl_ghostty_terminal_write(&terminal, end, sizeof(end) - 1));
  CHECK(lxl_ghostty_terminal_write(&terminal, later, sizeof(later) - 1));
  CHECK(terminal.write_head != NULL);

  DrainContext ctx = { .terminal = &terminal, .receiver = fds[1], .output = actual, .expected_len = expected_len };
  pthread_t drain;
  CHECK(pthread_create(&drain, NULL, drain_peer, &ctx) == 0);
  CHECK(pthread_join(drain, NULL) == 0);
  CHECK(ctx.output_len == expected_len);
  CHECK(memcmp(actual, expected, expected_len) == 0);

  free(actual); free(expected);
  close(fds[0]); close(fds[1]);
  pthread_mutex_destroy(&terminal.write_mu);

  /* A dead receiver must fail the enqueue and release all queued buffers. */
  CHECK(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  close(fds[1]);
  LxlGhosttyTerminal closed = { .pty_fd = fds[0] };
  CHECK(pthread_mutex_init(&closed.write_mu, NULL) == 0);
  atomic_init(&closed.stopping, false);
  CHECK(!lxl_ghostty_terminal_write(&closed, payload, payload_len));
  CHECK(closed.write_head == NULL && closed.write_tail == NULL);
  close(fds[0]);
  pthread_mutex_destroy(&closed.write_mu);
  free(payload);
  puts("pty write backpressure: ok");
  return 0;
}
