#include "../src/utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    abort(); \
  } \
} while (0)

int main(void) {
  char buf[4];
  CHECK(lxl_ghostty_utf8_encode('A', buf) == 1);
  CHECK(buf[0] == 'A');

  CHECK(lxl_ghostty_utf8_encode(0x20ac, buf) == 3);
  CHECK((unsigned char)buf[0] == 0xe2);
  CHECK((unsigned char)buf[1] == 0x82);
  CHECK((unsigned char)buf[2] == 0xac);

  CHECK(lxl_ghostty_utf8_validate("hello", 5));
  CHECK(lxl_ghostty_utf8_validate("\xe2\x82\xac", 3));
  CHECK(!lxl_ghostty_utf8_validate("\xe2\x28\xa1", 3));
  CHECK(!lxl_ghostty_utf8_validate("\xed\xa0\x80", 3));

  return 0;
}
