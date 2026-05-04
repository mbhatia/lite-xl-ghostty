#ifndef LXL_GHOSTTY_OSC_OBSERVER_H
#define LXL_GHOSTTY_OSC_OBSERVER_H

#include "event_queue.h"

#include <stddef.h>
#include <stdbool.h>

typedef struct {
  LxlGhosttyEventQueue *events;
  char *buf;
  size_t len;
  size_t cap;
  size_t max_bytes;
  bool in_osc;
  bool saw_esc;
  bool overflowed;
} LxlGhosttyOscObserver;

bool lxl_ghostty_osc_observer_init(LxlGhosttyOscObserver *observer,
                                   LxlGhosttyEventQueue *events,
                                   size_t max_bytes);
void lxl_ghostty_osc_observer_deinit(LxlGhosttyOscObserver *observer);
void lxl_ghostty_osc_observer_feed(LxlGhosttyOscObserver *observer,
                                   const char *data,
                                   size_t len);

#endif
