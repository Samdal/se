#ifndef UTF8_H_
#define UTF8_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

#define UTF_INVALID   0xFFFD
#define UTF_SIZ       4

typedef uint_least32_t rune_t;

size_t utf8_encode(rune_t, char *);
int utf8_decode_buffer(const char* buffer, const int buflen, rune_t* u);
void utf8_remove_string_end(char* string);


#endif // UTF8_H_
