#include "event_queue.h"

#include <stdlib.h>
#include <string.h>

char *lxl_ghostty_strdup_len(const char *data, size_t len) {
  char *copy = (char *)malloc(len + 1);
  if (!copy) return NULL;
  if (len > 0 && data) memcpy(copy, data, len);
  copy[len] = '\0';
  return copy;
}

char *lxl_ghostty_strdup(const char *data) {
  return data ? lxl_ghostty_strdup_len(data, strlen(data)) : NULL;
}

static bool event_dup(LxlGhosttyEvent *dst, const LxlGhosttyEvent *src) {
  memset(dst, 0, sizeof(*dst));
  dst->kind = src->kind;
  dst->code = src->code;
  dst->signal = src->signal;
  dst->bytes = src->bytes;
  if (src->title && !(dst->title = lxl_ghostty_strdup(src->title))) goto fail;
  if (src->body && !(dst->body = lxl_ghostty_strdup(src->body))) goto fail;
  if (src->text && !(dst->text = lxl_ghostty_strdup(src->text))) goto fail;
  if (src->clipboard && !(dst->clipboard = lxl_ghostty_strdup(src->clipboard))) goto fail;
  return true;

fail:
  lxl_ghostty_event_clear(dst);
  return false;
}

void lxl_ghostty_event_clear(LxlGhosttyEvent *event) {
  if (!event) return;
  free(event->title);
  free(event->body);
  free(event->text);
  free(event->clipboard);
  memset(event, 0, sizeof(*event));
}

bool lxl_ghostty_event_queue_init(LxlGhosttyEventQueue *queue) {
  memset(queue, 0, sizeof(*queue));
  return pthread_mutex_init(&queue->mu, NULL) == 0;
}

void lxl_ghostty_event_queue_deinit(LxlGhosttyEventQueue *queue) {
  if (!queue) return;
  pthread_mutex_lock(&queue->mu);
  for (size_t i = 0; i < queue->len; i++) {
    lxl_ghostty_event_clear(&queue->items[i]);
  }
  free(queue->items);
  queue->items = NULL;
  queue->len = 0;
  queue->cap = 0;
  pthread_mutex_unlock(&queue->mu);
  pthread_mutex_destroy(&queue->mu);
}

static bool ensure_cap(LxlGhosttyEventQueue *queue, size_t cap) {
  if (queue->cap >= cap) return true;
  size_t next = queue->cap ? queue->cap * 2 : 16;
  while (next < cap) next *= 2;
  LxlGhosttyEvent *items = (LxlGhosttyEvent *)realloc(queue->items, next * sizeof(*items));
  if (!items) return false;
  queue->items = items;
  queue->cap = next;
  return true;
}

static bool coalesce_latest_string_event(LxlGhosttyEventQueue *queue, const LxlGhosttyEvent *event) {
  if (event->kind != LXL_GHOSTTY_EVENT_TITLE && event->kind != LXL_GHOSTTY_EVENT_CWD) return false;
  for (size_t i = queue->len; i > 0; i--) {
    LxlGhosttyEvent *existing = &queue->items[i - 1];
    if (existing->kind == event->kind) {
      char *title = event->title ? lxl_ghostty_strdup(event->title) : NULL;
      char *body = event->body ? lxl_ghostty_strdup(event->body) : NULL;
      if ((event->title && !title) || (event->body && !body)) {
        free(title);
        free(body);
        return true;
      }
      free(existing->title);
      free(existing->body);
      existing->title = title;
      existing->body = body;
      existing->bytes = event->bytes;
      return true;
    }
  }
  return false;
}

bool lxl_ghostty_event_queue_push(LxlGhosttyEventQueue *queue, const LxlGhosttyEvent *event) {
  if (!queue || !event || event->kind == LXL_GHOSTTY_EVENT_NONE) return false;
  pthread_mutex_lock(&queue->mu);

  if (event->kind == LXL_GHOSTTY_EVENT_EXIT) {
    if (queue->exit_delivered) {
      pthread_mutex_unlock(&queue->mu);
      return true;
    }
    queue->exit_delivered = true;
  }

  if (event->kind == LXL_GHOSTTY_EVENT_BELL) {
    for (size_t i = queue->len; i > 0; i--) {
      if (queue->items[i - 1].kind == LXL_GHOSTTY_EVENT_BELL) {
        queue->items[i - 1].bytes++;
        pthread_mutex_unlock(&queue->mu);
        return true;
      }
    }
  }

  if (coalesce_latest_string_event(queue, event)) {
    pthread_mutex_unlock(&queue->mu);
    return true;
  }

  if (!ensure_cap(queue, queue->len + 1)) {
    pthread_mutex_unlock(&queue->mu);
    return false;
  }
  bool ok = event_dup(&queue->items[queue->len], event);
  if (ok && queue->items[queue->len].kind == LXL_GHOSTTY_EVENT_BELL && queue->items[queue->len].bytes == 0) {
    queue->items[queue->len].bytes = 1;
  }
  if (ok) queue->len++;
  pthread_mutex_unlock(&queue->mu);
  return ok;
}

bool lxl_ghostty_event_queue_pop(LxlGhosttyEventQueue *queue, LxlGhosttyEvent *out) {
  if (!queue || !out) return false;
  pthread_mutex_lock(&queue->mu);
  if (queue->len == 0) {
    pthread_mutex_unlock(&queue->mu);
    memset(out, 0, sizeof(*out));
    return false;
  }
  *out = queue->items[0];
  memmove(&queue->items[0], &queue->items[1], (queue->len - 1) * sizeof(queue->items[0]));
  queue->len--;
  memset(&queue->items[queue->len], 0, sizeof(queue->items[queue->len]));
  pthread_mutex_unlock(&queue->mu);
  return true;
}

size_t lxl_ghostty_event_queue_len(LxlGhosttyEventQueue *queue) {
  if (!queue) return 0;
  pthread_mutex_lock(&queue->mu);
  size_t len = queue->len;
  pthread_mutex_unlock(&queue->mu);
  return len;
}
