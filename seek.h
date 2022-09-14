#ifndef SEEK_H_
#define SEEK_H_

// TODO: check that all functions are in the right place
// complete rework / file renaming first

#include "buffer.h"
#define LIMIT(x, a, b) (x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)

struct delimiter {
    char* start;
    char* end;
};

int str_contains_char(const char* string, char check);
/////////////
// result must be freed
char* file_path_get_path(const char* path);
int is_file_type(const char* file_path, const char* file_type);
int path_is_folder(const char* path);

int fb_is_on_a_word(const struct file_buffer* fb, int offset, const char* word_seperators);
int fb_is_start_of_a_word(const struct file_buffer* fb, int offset, const char* word_seperators);
int fb_is_on_word(const struct file_buffer* fb, int offset, const char* word_seperators, const char* word);

int fb_offset_starts_with(const struct file_buffer* fb, int offset, const char* start);

int fb_seek_char(const struct file_buffer* fb, int offset, char byte);
int fb_seek_char_backwards(const struct file_buffer* fb, int offset, char byte);

int fb_seek_string(const struct file_buffer* fb, int offset, const char* string);
int fb_seek_string_not_escaped(const struct file_buffer* fb, int offset, const char* string);
int fb_seek_string_backwards(const struct file_buffer* fb, int offset, const char* string);
int fb_seek_string_backwards_not_escaped(const struct file_buffer* fb, int offset, const char* string);
int wb_seek_string_wrap(const struct window_buffer* wb, int offset, const char* search);
int wb_seek_string_wrap_backwards(const struct window_buffer* wb, int offset, const char* search);

int fb_seek_word(const struct file_buffer* fb, int offset, const char* word_seperators);
int fb_seek_word_end(const struct file_buffer* fb, int offset, const char* word_seperators);
int fb_seek_word_backwards(const struct file_buffer* fb, int offset, const char* word_seperators);
int fb_seek_start_of_word_backwards(const struct file_buffer* fb, int offset, const char* word_seperators);

int fb_seek_whitespace(const struct file_buffer* fb, int offset);
int fb_seek_whitespace_backwards(const struct file_buffer* fb, int offset);
int fb_seek_not_whitespace(const struct file_buffer* fb, int offset);
int fb_seek_not_whitespace_backwards(const struct file_buffer* fb, int offset);
// TODO:
//int fb_has_whitespace_between(const struct file_buffer* fb, int start, int end);

int fb_count_string_instances(const struct file_buffer* fb, const char* string, int offset, int* before_offset);

////////////////////////
// struct delimiter* ignore is a null terminated pointer array
int fb_get_delimiter(const struct file_buffer* fb, int offset, struct delimiter d, struct delimiter* ignore, int* start, int* end);


#endif // SEEK_H_
