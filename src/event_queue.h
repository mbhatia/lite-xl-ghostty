#ifndef LXL_GHOSTTY_EVENT_QUEUE_H
#define LXL_GHOSTTY_EVENT_QUEUE_H

#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
  LXL_GHOSTTY_EVENT_NONE = 0,
  LXL_GHOSTTY_EVENT_BELL,
  LXL_GHOSTTY_EVENT_TITLE,
  LXL_GHOSTTY_EVENT_CWD,
  LXL_GHOSTTY_EVENT_NOTIFICATION,
  LXL_GHOSTTY_EVENT_CLIPBOARD_WRITE_REQUEST,
  LXL_GHOSTTY_EVENT_CLIPBOARD_WRITE_DENIED,
  LXL_GHOSTTY_EVENT_CLIPBOARD_WRITE_ACCEPTED,
  LXL_GHOSTTY_EVENT_EXIT,
  LXL_GHOSTTY_EVENT_DEBUG,
} LxlGhosttyEventKind;

typedef struct {
  LxlGhosttyEventKind kind;
  char *title;
  char *body;
  char *text;
  char *clipboard;
  int code;
  int signal;
  size_t bytes;
} LxlGhosttyEvent;

typedef struct {
  pthread_mutex_t mu;
  LxlGhosttyEvent *items;
  size_t len;
  size_t cap;
  bool exit_delivered;
} LxlGhosttyEventQueue;

bool lxl_ghostty_event_queue_init(LxlGhosttyEventQueue *queue);
void lxl_ghostty_event_queue_deinit(LxlGhosttyEventQueue *queue);
void lxl_ghostty_event_clear(LxlGhosttyEvent *event);
bool lxl_ghostty_event_queue_push(LxlGhosttyEventQueue *queue, const LxlGhosttyEvent *event);
bool lxl_ghostty_event_queue_pop(LxlGhosttyEventQueue *queue, LxlGhosttyEvent *out);
size_t lxl_ghostty_event_queue_len(LxlGhosttyEventQueue *queue);

char *lxl_ghostty_strdup_len(const char *data, size_t len);
char *lxl_ghostty_strdup(const char *data);

#endif
