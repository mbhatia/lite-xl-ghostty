#include "../src/event_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    abort(); \
  } \
} while (0)

static void test_fifo_and_ownership(void) {
  LxlGhosttyEventQueue q;
  CHECK(lxl_ghostty_event_queue_init(&q));
  LxlGhosttyEvent e = {
    .kind = LXL_GHOSTTY_EVENT_NOTIFICATION,
    .title = "title",
    .body = "body",
  };
  CHECK(lxl_ghostty_event_queue_push(&q, &e));
  CHECK(lxl_ghostty_event_queue_len(&q) == 1);

  LxlGhosttyEvent out;
  CHECK(lxl_ghostty_event_queue_pop(&q, &out));
  CHECK(out.kind == LXL_GHOSTTY_EVENT_NOTIFICATION);
  CHECK(strcmp(out.title, "title") == 0);
  CHECK(strcmp(out.body, "body") == 0);
  lxl_ghostty_event_clear(&out);
  CHECK(!lxl_ghostty_event_queue_pop(&q, &out));
  lxl_ghostty_event_queue_deinit(&q);
}

static void test_coalesces_bell_title_cwd_and_exit(void) {
  LxlGhosttyEventQueue q;
  CHECK(lxl_ghostty_event_queue_init(&q));

  LxlGhosttyEvent bell = { .kind = LXL_GHOSTTY_EVENT_BELL };
  CHECK(lxl_ghostty_event_queue_push(&q, &bell));
  CHECK(lxl_ghostty_event_queue_push(&q, &bell));

  LxlGhosttyEvent title1 = { .kind = LXL_GHOSTTY_EVENT_TITLE, .title = "old" };
  LxlGhosttyEvent title2 = { .kind = LXL_GHOSTTY_EVENT_TITLE, .title = "new" };
  CHECK(lxl_ghostty_event_queue_push(&q, &title1));
  CHECK(lxl_ghostty_event_queue_push(&q, &title2));

  LxlGhosttyEvent exit1 = { .kind = LXL_GHOSTTY_EVENT_EXIT, .code = 0 };
  LxlGhosttyEvent exit2 = { .kind = LXL_GHOSTTY_EVENT_EXIT, .code = 1 };
  CHECK(lxl_ghostty_event_queue_push(&q, &exit1));
  CHECK(lxl_ghostty_event_queue_push(&q, &exit2));

  CHECK(lxl_ghostty_event_queue_len(&q) == 3);

  LxlGhosttyEvent out;
  CHECK(lxl_ghostty_event_queue_pop(&q, &out));
  CHECK(out.kind == LXL_GHOSTTY_EVENT_BELL);
  CHECK(out.bytes == 2);
  lxl_ghostty_event_clear(&out);

  CHECK(lxl_ghostty_event_queue_pop(&q, &out));
  CHECK(out.kind == LXL_GHOSTTY_EVENT_TITLE);
  CHECK(strcmp(out.title, "new") == 0);
  lxl_ghostty_event_clear(&out);

  CHECK(lxl_ghostty_event_queue_pop(&q, &out));
  CHECK(out.kind == LXL_GHOSTTY_EVENT_EXIT);
  CHECK(out.code == 0);
  lxl_ghostty_event_clear(&out);

  lxl_ghostty_event_queue_deinit(&q);
}

int main(void) {
  test_fifo_and_ownership();
  test_coalesces_bell_title_cwd_and_exit();
  return 0;
}
