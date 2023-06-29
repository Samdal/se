#include "utf8.h"

// TODO(not important):
// optimize charsize algorthim

#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))

static const uint8_t utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const uint8_t utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};

static size_t utf8_decode(const char *, rune_t *, size_t);
static rune_t utf8_decodebyte(char, size_t *);
static char   utf8_encodebyte(rune_t, size_t);
static size_t utf8_validate(rune_t *, size_t);

rune_t
utf8_decodebyte(char c, size_t *i)
{
		for (*i = 0; *i < LEN(utfmask); ++(*i))
				if (((uint8_t)c & utfmask[*i]) == utfbyte[*i])
						return (uint8_t)c & ~utfmask[*i];
		return 0;
}

size_t
utf8_encode(rune_t u, char *c)
{
		size_t len, i;

		len = utf8_validate(&u, 0);
		if (len > UTF_SIZ)
				return 0;

		for (i = len - 1; i != 0; --i) {
				c[i] = utf8_encodebyte(u, 0);
				u >>= 6;
		}
		c[0] = utf8_encodebyte(u, len);

		return len;
}

char
utf8_encodebyte(rune_t u, size_t i)
{
		return utfbyte[i] | (u & ~utfmask[i]);
}

size_t
utf8_validate(rune_t *u, size_t i)
{
		const rune_t utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
		const rune_t utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

		if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
				*u = UTF_INVALID;
		for (i = 1; *u > utfmax[i]; ++i)
				;

		return i;
}

size_t
utf8_decode(const char *c, rune_t *u, size_t clen)
{
		size_t i, j, len, type;
		rune_t udecoded;

		*u = UTF_INVALID;
		if (!clen)
				return 0;
		udecoded = utf8_decodebyte(c[0], &len);
		if (!BETWEEN(len, 1, UTF_SIZ))
				return 1;
		for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
				udecoded = (udecoded << 6) | utf8_decodebyte(c[i], &type);
				if (type != 0)
						return j;
		}
		if (j < len)
				return 0;
		*u = udecoded;
		utf8_validate(u, len);

		return len;
}

int
utf8_decode_buffer(const char* buffer, const int buflen, rune_t* u)
{
		if (!buflen) return 0;

		rune_t u_tmp;
		int charsize;
		if (!u)
				u = &u_tmp;

		// process a complete utf8 char
		charsize = utf8_decode(buffer, u, buflen);

		return charsize;
}

void
utf8_remove_string_end(char* string)
{
		char* end = string + strlen(string);
		if (end == string)
				return;

		do {
				end--;
				// if byte starts with 0b10, byte is an UTF-8 extender
		} while (end > string && (*end & 0xC0) == 0x80);
		*end = 0;
}
