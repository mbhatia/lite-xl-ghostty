#ifndef LXL_GHOSTTY_UTF8_H
#define LXL_GHOSTTY_UTF8_H

#include <stddef.h>
#include <stdint.h>

size_t lxl_ghostty_utf8_encode(uint32_t codepoint, char out[4]);
int lxl_ghostty_utf8_validate(const char *data, size_t len);

#endif
