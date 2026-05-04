#include "../src/osc_observer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    abort(); \
  } \
} while (0)

static LxlGhosttyEvent pop_one(LxlGhosttyEventQueue *q) {
  LxlGhosttyEvent out;
  CHECK(lxl_ghostty_event_queue_pop(q, &out));
  return out;
}

static void test_osc9_bel_and_st_fragmentation(void) {
  LxlGhosttyEventQueue q;
  LxlGhosttyOscObserver osc;
  CHECK(lxl_ghostty_event_queue_init(&q));
  CHECK(lxl_ghostty_osc_observer_init(&osc, &q, 1024));

  const char osc9_bel[] = "\x1b]9;hello\a";
  lxl_ghostty_osc_observer_feed(&osc, osc9_bel, sizeof(osc9_bel) - 1);
  LxlGhosttyEvent out = pop_one(&q);
  CHECK(out.kind == LXL_GHOSTTY_EVENT_NOTIFICATION);
  CHECK(strcmp(out.body, "hello") == 0);
  lxl_ghostty_event_clear(&out);

  const char osc9_part1[] = "\x1b]9;title;";
  const char osc9_part2[] = "body\x1b\\";
  lxl_ghostty_osc_observer_feed(&osc, osc9_part1, sizeof(osc9_part1) - 1);
  lxl_ghostty_osc_observer_feed(&osc, osc9_part2, sizeof(osc9_part2) - 1);
  out = pop_one(&q);
  CHECK(out.kind == LXL_GHOSTTY_EVENT_NOTIFICATION);
  CHECK(strcmp(out.title, "title") == 0);
  CHECK(strcmp(out.body, "body") == 0);
  lxl_ghostty_event_clear(&out);

  lxl_ghostty_osc_observer_deinit(&osc);
  lxl_ghostty_event_queue_deinit(&q);
}

static void test_osc52_clipboard_write(void) {
  LxlGhosttyEventQueue q;
  LxlGhosttyOscObserver osc;
  CHECK(lxl_ghostty_event_queue_init(&q));
  CHECK(lxl_ghostty_osc_observer_init(&osc, &q, 1024));

  const char osc52[] = "\x1b]52;c;aGVsbG8=\a";
  lxl_ghostty_osc_observer_feed(&osc, osc52, sizeof(osc52) - 1);
  LxlGhosttyEvent out = pop_one(&q);
  CHECK(out.kind == LXL_GHOSTTY_EVENT_CLIPBOARD_WRITE_REQUEST);
  CHECK(strcmp(out.clipboard, "c") == 0);
  CHECK(strcmp(out.text, "hello") == 0);
  CHECK(out.bytes == 5);
  lxl_ghostty_event_clear(&out);

  lxl_ghostty_osc_observer_deinit(&osc);
  lxl_ghostty_event_queue_deinit(&q);
}

static void test_overflow_drops_sequence(void) {
  LxlGhosttyEventQueue q;
  LxlGhosttyOscObserver osc;
  CHECK(lxl_ghostty_event_queue_init(&q));
  CHECK(lxl_ghostty_osc_observer_init(&osc, &q, 8));

  const char oversized[] = "\x1b]9;this is too long\a";
  lxl_ghostty_osc_observer_feed(&osc, oversized, sizeof(oversized) - 1);
  LxlGhosttyEvent out = pop_one(&q);
  CHECK(out.kind == LXL_GHOSTTY_EVENT_DEBUG);
  CHECK(strstr(out.text, "oversized") != NULL);
  lxl_ghostty_event_clear(&out);
  CHECK(lxl_ghostty_event_queue_len(&q) == 0);

  lxl_ghostty_osc_observer_deinit(&osc);
  lxl_ghostty_event_queue_deinit(&q);
}

int main(void) {
  test_osc9_bel_and_st_fragmentation();
  test_osc52_clipboard_write();
  test_overflow_drops_sequence();
  return 0;
}
