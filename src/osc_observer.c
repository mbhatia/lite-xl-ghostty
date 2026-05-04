#include "osc_observer.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_OSC_MAX_BYTES (1024 * 1024)

static int b64_value(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  if (c == '=') return -2;
  if (isspace(c)) return -3;
  return -1;
}

static bool b64_decode(const char *in, size_t in_len, size_t max_bytes, char **out, size_t *out_len) {
  size_t cap = (in_len / 4 + 1) * 3;
  if (cap > max_bytes + 3) cap = max_bytes + 3;
  char *buf = (char *)malloc(cap + 1);
  if (!buf) return false;
  uint32_t acc = 0;
  int bits = 0;
  size_t n = 0;
  bool padded = false;
  for (size_t i = 0; i < in_len; i++) {
    int v = b64_value((unsigned char)in[i]);
    if (v == -3) continue;
    if (v == -1) {
      free(buf);
      return false;
    }
    if (v == -2) {
      padded = true;
      continue;
    }
    if (padded) {
      free(buf);
      return false;
    }
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= max_bytes) {
        free(buf);
        return false;
      }
      buf[n++] = (char)((acc >> bits) & 0xff);
    }
  }
  buf[n] = '\0';
  *out = buf;
  *out_len = n;
  return true;
}

bool lxl_ghostty_osc_observer_init(LxlGhosttyOscObserver *observer,
                                   LxlGhosttyEventQueue *events,
                                   size_t max_bytes) {
  memset(observer, 0, sizeof(*observer));
  observer->events = events;
  observer->max_bytes = max_bytes ? max_bytes : DEFAULT_OSC_MAX_BYTES;
  observer->cap = 256;
  observer->buf = (char *)malloc(observer->cap);
  return observer->buf != NULL;
}

void lxl_ghostty_osc_observer_deinit(LxlGhosttyOscObserver *observer) {
  if (!observer) return;
  free(observer->buf);
  memset(observer, 0, sizeof(*observer));
}

static void reset_sequence(LxlGhosttyOscObserver *observer) {
  observer->in_osc = false;
  observer->saw_esc = false;
  observer->overflowed = false;
  observer->len = 0;
}

static void push_debug(LxlGhosttyOscObserver *observer, const char *msg) {
  LxlGhosttyEvent event = {
    .kind = LXL_GHOSTTY_EVENT_DEBUG,
    .text = (char *)msg,
  };
  lxl_ghostty_event_queue_push(observer->events, &event);
}

static bool append_byte(LxlGhosttyOscObserver *observer, char c) {
  if (observer->len >= observer->max_bytes) {
    observer->overflowed = true;
    return false;
  }
  if (observer->len + 1 > observer->cap) {
    size_t next = observer->cap * 2;
    if (next < observer->len + 1) next = observer->len + 1;
    if (next > observer->max_bytes) next = observer->max_bytes;
    char *buf = (char *)realloc(observer->buf, next);
    if (!buf) {
      observer->overflowed = true;
      return false;
    }
    observer->buf = buf;
    observer->cap = next;
  }
  observer->buf[observer->len++] = c;
  return true;
}

static char *dup_trim(const char *data, size_t len) {
  while (len > 0 && isspace((unsigned char)data[0])) {
    data++;
    len--;
  }
  while (len > 0 && isspace((unsigned char)data[len - 1])) len--;
  return lxl_ghostty_strdup_len(data, len);
}

static void handle_osc9(LxlGhosttyOscObserver *observer, const char *payload, size_t len) {
  const char *sep = memchr(payload, ';', len);
  LxlGhosttyEvent event = { .kind = LXL_GHOSTTY_EVENT_NOTIFICATION };
  if (sep) {
    size_t title_len = (size_t)(sep - payload);
    event.title = dup_trim(payload, title_len);
    event.body = dup_trim(sep + 1, len - title_len - 1);
  } else {
    event.body = dup_trim(payload, len);
  }
  event.text = lxl_ghostty_strdup_len(payload, len);
  lxl_ghostty_event_queue_push(observer->events, &event);
  lxl_ghostty_event_clear(&event);
}

static void handle_osc52(LxlGhosttyOscObserver *observer, const char *payload, size_t len) {
  const char *sep = memchr(payload, ';', len);
  if (!sep) return;
  size_t clip_len = (size_t)(sep - payload);
  char *decoded = NULL;
  size_t decoded_len = 0;
  if (!b64_decode(sep + 1, len - clip_len - 1, observer->max_bytes, &decoded, &decoded_len)) {
    push_debug(observer, "invalid or oversized OSC 52 payload");
    return;
  }
  LxlGhosttyEvent event = {
    .kind = LXL_GHOSTTY_EVENT_CLIPBOARD_WRITE_REQUEST,
    .clipboard = lxl_ghostty_strdup_len(payload, clip_len),
    .text = decoded,
    .bytes = decoded_len,
  };
  lxl_ghostty_event_queue_push(observer->events, &event);
  lxl_ghostty_event_clear(&event);
}

static void finish_sequence(LxlGhosttyOscObserver *observer) {
  if (observer->overflowed) {
    push_debug(observer, "dropped oversized OSC sequence");
    reset_sequence(observer);
    return;
  }
  char *semi = memchr(observer->buf, ';', observer->len);
  if (!semi) {
    reset_sequence(observer);
    return;
  }
  size_t cmd_len = (size_t)(semi - observer->buf);
  const char *payload = semi + 1;
  size_t payload_len = observer->len - cmd_len - 1;
  if (cmd_len == 1 && observer->buf[0] == '9') {
    handle_osc9(observer, payload, payload_len);
  } else if (cmd_len == 2 && observer->buf[0] == '5' && observer->buf[1] == '2') {
    handle_osc52(observer, payload, payload_len);
  }
  reset_sequence(observer);
}

void lxl_ghostty_osc_observer_feed(LxlGhosttyOscObserver *observer,
                                   const char *data,
                                   size_t len) {
  if (!observer || !data) return;
  for (size_t i = 0; i < len; i++) {
    char c = data[i];
    if (!observer->in_osc) {
      if (observer->saw_esc) {
        if (c == ']') {
          observer->in_osc = true;
          observer->saw_esc = false;
          observer->len = 0;
          observer->overflowed = false;
        } else {
          observer->saw_esc = (c == '\x1b');
        }
      } else if (c == '\x1b') {
        observer->saw_esc = true;
      }
      continue;
    }

    if (observer->saw_esc) {
      if (c == '\\') {
        finish_sequence(observer);
      } else {
        append_byte(observer, '\x1b');
        append_byte(observer, c);
        observer->saw_esc = false;
      }
      continue;
    }

    if (c == '\a') {
      finish_sequence(observer);
    } else if (c == '\x1b') {
      observer->saw_esc = true;
    } else {
      append_byte(observer, c);
    }
  }
}
