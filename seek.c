

#if 1
#define _GNU_SOURCE
#endif

#include "seek.h"

#include <string.h>
#include <ctype.h>

#include "config.h"
#include <sys/stat.h>

#if 0
#define BOUNDS_CHECK(x, a, b) (x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#else
#define BOUNDS_CHECK(x, a, b)
#endif

int
str_contains_char(const char* string, char check)
{
		return strchr(string, check) != NULL;
}

inline int
is_file_type(const char* file_path, const char* file_type)
{
		int ftlen  = strlen(file_type);
		int offset = strlen(file_path) - ftlen;
		if(offset >= 0 && memcmp(file_path + offset, file_type, ftlen) == 0)
				return 1;
		return 0;
}

char*
file_path_get_path(const char* path)
{
		soft_assert(path, path = "/";);

		const char* folder_start = strrchr(path, '/');
		if (!folder_start)
				folder_start = path;
		else
				folder_start++;
		int folder_len = folder_start - path;
		char* folder = xmalloc(folder_len + 1);

		memcpy(folder, path, folder_len);
		folder[folder_len] = '\0';

		return folder;
}

inline int
path_is_folder(const char* path)
{
		struct stat statbuf;
		if (stat(path, &statbuf) != 0)
				return 0;
		return S_ISDIR(statbuf.st_mode);
}

inline int
fb_seek_char(const struct file_buffer* fb, int offset, char byte)
{
		if (offset > fb->len) return -1;
		char* new_buf = memchr(fb->contents + offset, byte, fb->len - offset);
		if (!new_buf) return -1;
		return new_buf - fb->contents;
}

inline int
fb_seek_char_backwards(const struct file_buffer* fb, int offset, char byte)
{
		BOUNDS_CHECK(offset, 0, fb->len-1);
		for (int n = offset-1; n >= 0; n--) {
				if (fb->contents[n] == byte) {
						return n+1;
				}
		}
		return -1;
}

inline int
fb_seek_string(const struct file_buffer* fb, int offset, const char* string)
{
		BOUNDS_CHECK(offset, 0, fb->len-1);
		int str_len = strlen(string);

#if 0
		for (int n = offset; n < fb->len - str_len; n++)
				if (!memcmp(fb->contents + n, string, str_len))
						return n;
#else
		char* res = memmem(fb->contents + offset, fb->len - offset,
						   string, str_len);
		if (res)
				return res - fb->contents;
#endif
		return -1;
}

inline int
fb_seek_string_backwards(const struct file_buffer* fb, int offset, const char* string)
{
		int str_len = strlen(string);
		offset += str_len;
		BOUNDS_CHECK(offset, 0, fb->len-1);

		for (int n = offset - str_len; n >= 0; n--)
				if (!memcmp(fb->contents + n, string, str_len))
						return n;
		return -1;
}

inline int
wb_seek_string_wrap(const struct window_buffer* wb, int offset, const char* search)
{
		struct file_buffer* fb = get_fb(focused_window);
		if (*search == 0 || !fb_count_string_instances(fb, search, 0, NULL))
				return -1;

		int new_offset = fb_seek_string(fb, offset, search);
		if (new_offset < 0)
				new_offset = fb_seek_string(fb, 0, search);

		if (!(fb->mode & FB_SEARCH_BLOCKING))
				fb->mode |= FB_SEARCH_BLOCKING_IDLE;
		return new_offset;
}

inline int
wb_seek_string_wrap_backwards(const struct window_buffer* wb, int offset, const char* search)
{
		struct file_buffer* fb = get_fb(focused_window);
		if (*search == 0 || !fb_count_string_instances(fb, search, 0, NULL))
				return -1;

		int new_offset = fb_seek_string_backwards(fb, offset, search);
		if (new_offset < 0)
				new_offset = fb_seek_string_backwards(fb, fb->len, search);

		if (!(fb->mode & FB_SEARCH_BLOCKING))
				fb->mode |= FB_SEARCH_BLOCKING_IDLE;
		return new_offset;
}

int
fb_is_on_a_word(const struct file_buffer* fb, int offset, const char* word_seperators)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		return !str_contains_char(word_seperators, fb->contents[offset]);
}

inline int
fb_is_start_of_a_word(const struct file_buffer* fb, int offset, const char* word_seperators)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		return fb_is_on_a_word(fb, offset, word_seperators) &&
				(offset-1 <= 0 || str_contains_char(word_seperators, fb->contents[offset-1]));
}

inline int
fb_is_on_word(const struct file_buffer* fb, int offset, const char* word_seperators, const char* word)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		int word_start = fb_seek_start_of_word_backwards(fb, offset, word_seperators);
		int word_len   = strlen(word);
		if (word_start < offset - (word_len-1))
				return 0;
		return fb_offset_starts_with(fb, word_start, word) &&
				!fb_is_on_a_word(fb, word_start + word_len, word_seperators);
}

inline int
fb_offset_starts_with(const struct file_buffer* fb, int offset, const char* start)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		if (offset > 0 && fb->contents[offset-1] == '\\') return 0;

		int len = strlen(start);
		int mlen = MIN(len, fb->len - offset);
		return memcmp(fb->contents + offset, start, mlen) == 0;
}

inline int
fb_seek_word(const struct file_buffer* fb, int offset, const char* word_seperators)
{
		if (fb_is_on_a_word(fb, offset, word_seperators))
				offset = fb_seek_word_end(fb, offset, word_seperators);
		while (offset < fb->len && str_contains_char(word_seperators, fb->contents[offset])) offset++;
		return offset;
}

inline int
fb_seek_word_end(const struct file_buffer* fb, int offset, const char* word_seperators)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		if (!fb_is_on_a_word(fb, offset, word_seperators))
				offset = fb_seek_word(fb, offset, word_seperators);
		while (offset < fb->len && !str_contains_char(word_seperators, fb->contents[offset])) offset++;
		return offset;
}

inline int
fb_seek_word_backwards(const struct file_buffer* fb, int offset, const char* word_seperators)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		if (!fb_is_on_a_word(fb, offset, word_seperators))
				while (offset > 0 && str_contains_char(word_seperators, fb->contents[offset])) offset--;
		return offset;
}

inline int
fb_seek_start_of_word_backwards(const struct file_buffer* fb, int offset, const char* word_seperators)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		if (!fb_is_on_a_word(fb, offset, word_seperators))
				while (offset > 0 && str_contains_char(word_seperators, fb->contents[offset])) offset--;
		while (offset > 0 && !str_contains_char(word_seperators, fb->contents[offset])) offset--;
		return offset+1;
}

inline int
fb_seek_whitespace(const struct file_buffer* fb, int offset)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		while (offset < fb->len && !isspace(fb->contents[offset])) offset++;
		return offset;
}

inline int
fb_seek_whitespace_backwards(const struct file_buffer* fb, int offset)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		while (offset > 0 && !isspace(fb->contents[offset])) offset--;
		return offset;
}

inline int
fb_seek_not_whitespace(const struct file_buffer* fb, int offset)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		while (offset < fb->len && isspace(fb->contents[offset])) offset++;
		return offset;
}

inline int
fb_seek_not_whitespace_backwards(const struct file_buffer* fb, int offset)
{
		BOUNDS_CHECK(offset, 0, fb->len);
		while (offset > 0 && isspace(fb->contents[offset])) offset--;
		return offset;
}

inline int
fb_count_string_instances(const struct file_buffer* fb, const char* string, int offset, int* before_offset)
{
		int tmp;
		if (!before_offset)
				before_offset = &tmp;
		if (!string || *string == 0) {
				*before_offset = 0;
				return 0;
		}

		int pos   = -1;
		int count =  0;
		int once  =  1;
		while((pos = fb_seek_string(fb, pos+1, string)) >= 0) {
				if (pos > 0 && fb->contents[pos-2] == '\\')
						continue;
				if (once && pos > offset) {
						*before_offset = count;
						once = 0;
				}
				count++;
		}
		if (once)
				*before_offset = count;
		return count;
}

/*
** Search from the start of the file
** Once any patterns are found, other patterns are disabled.
** If the pattern we are searching for extends beyond *offset* then we will use that as our delimiter
*/

///////////////
// "   \" // we're here "
// ↑                   ↑
// start               seeked end
int
fb_seek_string_not_escaped(const struct file_buffer* fb, int offset, const char* string)
{
		for (;;) {
				offset = fb_seek_string(fb, offset, string);
				if (offset >= 0 && fb->contents[offset-1] == '\\')
						offset++;
				else
						break;
		}
		return offset;
}

inline int
fb_seek_string_backwards_not_escaped(const struct file_buffer* fb, int offset, const char* string)
{
		for (;;) {
				offset = fb_seek_string_backwards(fb, offset, string);
				if (offset >= 0 && fb->contents[offset-1] == '\\')
						offset--;
				else
						break;
		}
		return offset;
}

inline static int
fb_get_delimiter_equal_start_end(const struct file_buffer* fb, int offset, const char* string, struct delimiter* ignore, int* start, int* end)
{
		int ignore_index = 0;
		int last_distance = 0;

		while (last_distance >= 0) {
				int d_res = INT32_MAX;
				int i = 0;
				if (ignore) {
						for (struct delimiter* ign = ignore; ign->start; ign++) {
								int d = fb_seek_string_not_escaped(fb, last_distance, ign->start);
								if (d < 0 || d > offset) {
										i++;
										continue;
								}

								if (d < d_res) {
										d_res = d;
										ignore_index = i;
								}
								i++;
						}
				}
				int str_d = fb_seek_string_not_escaped(fb, last_distance, string);

				if ((str_d <= 0 || str_d > offset) && d_res == INT32_MAX)
						return 0;

				const char* end_str;
				if (str_d >= 0 && str_d <= d_res) {
						last_distance = str_d;
						end_str = string;
						*start = last_distance;
				} else {
						last_distance = d_res;
						end_str = ignore[ignore_index].end;
				}

				last_distance = fb_seek_string_not_escaped(fb, last_distance+1, end_str);

				if (end_str == string && last_distance > offset) {
						*end = last_distance;
						return 1;
				}
				if (last_distance >= 0)
						last_distance++;
		}
		return 0;
}

inline static int
fb_get_matched_delimiter_end(const struct file_buffer* fb, int offset, struct delimiter d)
{
		int closers_left = 1;
		while (closers_left) {
				int next_open = fb_seek_string_not_escaped(fb, offset, d.start);
				int next_close = fb_seek_string_not_escaped(fb, offset, d.end);

				if (next_close < 0 || next_open < 0)
						return next_close;

				if (next_open < next_close) {
						closers_left++;
						offset = next_open+1;
				} else {
						closers_left--;
						offset = next_close;
						if (closers_left)
								offset++;
				}
		}
		return offset;
}

inline static int
fb_get_matched_delimiter_start(const struct file_buffer* fb, int offset, struct delimiter d)
{
		int openers_left = 1;
		while (openers_left) {
				int next_open = fb_seek_string_backwards_not_escaped(fb, offset, d.start);
				int next_close = fb_seek_string_backwards_not_escaped(fb, offset, d.end);

				if (next_close < 0 || next_open < 0)
						return next_open;

				if (next_open < next_close) {
						openers_left++;
						offset = next_open-1;
				} else {
						openers_left--;
						offset = next_open;
						if (openers_left)
								offset--;
				}
		}
		return offset;
}

inline int
fb_get_delimiter(const struct file_buffer* fb, int offset, struct delimiter d, struct delimiter* ignore, int* start, int* end)
{
		const char* start_word = d.start;
		const char* end_word = d.end;

		soft_assert(start, return 0;);
		soft_assert(end, return 0;);
		soft_assert(start_word, return 0;);
		soft_assert(end_word, return 0;);

		if (strcmp(start_word, end_word) == 0)
				return fb_get_delimiter_equal_start_end(fb, offset, start_word, ignore, start, end);
		int wanted_opener = fb_get_matched_delimiter_start(fb, offset, d);

		int ignore_index = 0;
		int last_distance = 0;

		while (last_distance >= 0) {
				int d_res = INT32_MAX;
				int i = 0;
				if (ignore) {
						for (struct delimiter* ign = ignore; ign->start; ign++) {
								int d = fb_seek_string_not_escaped(fb, last_distance, ign->start);
								if (d < 0 || d > offset) {
										i++;
										continue;
								}

								if (d < d_res) {
										d_res = d;
										ignore_index = i;
								}
								i++;
						}
				}
				int str_d = fb_seek_string_not_escaped(fb, last_distance, start_word);

				if ((str_d <= 0 || str_d > offset) && d_res == INT32_MAX)
						return 0;

				if (str_d >= 0 && str_d <= d_res) {
						if (str_d == wanted_opener) {
								last_distance = str_d;
								*start = last_distance;

								last_distance = fb_get_matched_delimiter_end(fb, last_distance+1, d);

								if (last_distance > offset) {
										*end = last_distance;
										return 1;
								}
						}
				} else {
						last_distance = d_res;

						last_distance = fb_seek_string_not_escaped(fb, last_distance+1, ignore[ignore_index].end);
				}

				if (last_distance >= 0)
						last_distance++;
		}
		return 0;
}
