#include "utf8.h"

size_t lxl_ghostty_utf8_encode(uint32_t cp, char out[4]) {
  if (cp <= 0x7f) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp <= 0x7ff) {
    out[0] = (char)(0xc0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3f));
    return 2;
  }
  if (cp >= 0xd800 && cp <= 0xdfff) cp = 0xfffd;
  if (cp <= 0xffff) {
    out[0] = (char)(0xe0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[2] = (char)(0x80 | (cp & 0x3f));
    return 3;
  }
  if (cp > 0x10ffff) cp = 0xfffd;
  out[0] = (char)(0xf0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
  out[3] = (char)(0x80 | (cp & 0x3f));
  return 4;
}

int lxl_ghostty_utf8_validate(const char *data, size_t len) {
  const unsigned char *s = (const unsigned char *)data;
  for (size_t i = 0; i < len;) {
    unsigned char c = s[i];
    size_t n = 0;
    uint32_t cp = 0;
    if (c <= 0x7f) {
      i++;
      continue;
    } else if ((c & 0xe0) == 0xc0) {
      n = 2;
      cp = c & 0x1f;
      if (cp == 0) return 0;
    } else if ((c & 0xf0) == 0xe0) {
      n = 3;
      cp = c & 0x0f;
    } else if ((c & 0xf8) == 0xf0) {
      n = 4;
      cp = c & 0x07;
    } else {
      return 0;
    }
    if (i + n > len) return 0;
    for (size_t j = 1; j < n; j++) {
      if ((s[i + j] & 0xc0) != 0x80) return 0;
      cp = (cp << 6) | (s[i + j] & 0x3f);
    }
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000)) return 0;
    if ((cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return 0;
    i += n;
  }
  return 1;
}
