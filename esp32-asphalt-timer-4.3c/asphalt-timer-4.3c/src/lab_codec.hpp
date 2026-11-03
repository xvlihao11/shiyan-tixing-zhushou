#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Zero-run encoding of an exact byte sequence. Zero denotes failure/no space.
inline size_t lab_pack(const uint8_t* source, size_t size, uint8_t* out, size_t capacity) {
  size_t input = 0, output = 0;
  while (input < size) {
    const bool zero = source[input] == 0;
    size_t n = 1;
    while (n < 128 && input + n < size && (source[input + n] == 0) == zero) ++n;
    if (output + 1 + (zero ? 0 : n) > capacity) return 0;
    out[output++] = (zero ? 0x80 : 0) | uint8_t(n - 1);
    if (!zero) { memcpy(out + output, source + input, n); output += n; }
    input += n;
  }
  return output;
}

inline bool lab_unpack(const uint8_t* source, size_t size, uint8_t* out, size_t expected) {
  size_t input = 0, output = 0;
  while (input < size) {
    const uint8_t control = source[input++];
    const size_t n = (control & 127) + 1;
    if (n > expected - output) return false;
    if (control & 128) memset(out + output, 0, n);
    else {
      if (n > size - input) return false;
      memcpy(out + output, source + input, n);
      input += n;
    }
    output += n;
  }
  return output == expected;
}
