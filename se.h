/* See LICENSE for license details. */
#ifndef _ST_H
#define _ST_H

#include <stdint.h>
#include <sys/types.h>
#include <wchar.h>
#include <limits.h>
#include <dirent.h>

// Arbitrary sizes
#define UTF_INVALID   0xFFFD
#define UTF_SIZ       4
#define UNDO_BUFFERS_COUNT 32
#define UPPER_CASE_WORD_MIN_LEN 3
#define STATUS_BAR_MAX_LEN 4096
#define SEARCH_TERM_MAX_LEN PATH_MAX

#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) < (b) ? (b) : (a))
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))

////////////////////////////////////////////////
// Glyphs
//

enum glyph_attribute {
	ATTR_NULL       = 0,
	ATTR_BOLD       = 1 << 0,
	ATTR_FAINT      = 1 << 1,
	ATTR_ITALIC     = 1 << 2,
	ATTR_UNDERLINE  = 1 << 3,
	ATTR_REVERSE    = 1 << 5,
	ATTR_INVISIBLE  = 1 << 6,
	ATTR_STRUCK     = 1 << 7,
	ATTR_WIDE       = 1 << 9,
	ATTR_WDUMMY     = 1 << 10,
	ATTR_BOLD_FAINT = ATTR_BOLD | ATTR_FAINT,
};

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;

typedef uint_least32_t Rune;

typedef struct {
	Rune  u;		// character code
	ushort  mode;	// attribute flags
	uint32_t fg;	// foreground
	uint32_t bg;	// background
} Glyph_;
#define Glyph Glyph_

typedef Glyph *Line;

////////////////////////////////////////////////
// Window buffer
//

// NOTE:
// refrain from entering the buffers or buffer keyword searching
// mode if you already are in file browser mode
enum window_buffer_mode {
	WINDOW_BUFFER_NORMAL = 0,
	WINDOW_BUFFER_FILE_BROWSER,
	WINDOW_BUFFER_SEARCH_BUFFERS,
	WINDOW_BUFFER_KEYWORD_ALL_BUFFERS,
	WINDOW_BUFFER_MODE_LEN,
};

struct window_buffer {
	int y_scroll;
	int cursor_offset;
	int cursor_col;

	int buffer_index; // index into an array storing file buffers
	enum window_buffer_mode mode;
};

struct window_buffer window_buffer_new(int buffer_index);

void buffer_write_selection(struct window_buffer* buf, int minx, int miny, int maxx, int maxy);
void  buffer_move_cursor_to_selection_start(struct window_buffer* buffer);

void buffer_offset_to_xy(struct window_buffer* buf, int offset, int maxx, int* cx, int* cy);

enum cursor_reason {
	CURSOR_DO_NOT_CALLBACK = 0,
	CURSOR_COMMAND_MOVEMENT = 1,
	CURSOR_UP_DOWN_MOVEMENT,
	CURSOR_RIGHT_LEFT_MOVEMENT,
	CURSOR_SNAPPED,
};

void buffer_move_on_line(struct window_buffer* buf, int amount, enum cursor_reason callback_reason);
void buffer_move_lines(struct window_buffer* buf, int amount, enum cursor_reason callback_reason);
void buffer_move_to_offset(struct window_buffer* buf, int offset, enum cursor_reason callback_reason);
void buffer_move_offset_relative(struct window_buffer* buf, int amount, enum cursor_reason callback_reason);
void buffer_move_to_x(struct window_buffer* buf, int x, enum cursor_reason callback_reason);

enum window_split_mode {
	WINDOW_SINGULAR,
	WINDOW_HORISONTAL,
	WINDOW_VERTICAL,
	WINDOW_FILE_BROWSER,
};

struct window_split_node {
	struct window_buffer window;
	enum window_split_mode mode;
	float ratio;
	struct window_split_node *node1, *node2, *parent;
	char* search;
	int selected;
};

void window_node_split(struct window_split_node* parent, float ratio, enum window_split_mode mode);
struct window_split_node* window_node_delete(struct window_split_node* node);
// uses focused_window to draw the cursor
void window_draw_tree_to_screen(struct window_split_node* root, int minx, int miny, int maxx, int maxy);
void window_move_all_cursors_on_same_buf(struct window_split_node* root, struct window_split_node* excluded, int buf_index, int offset, void(movement)(struct window_buffer*, int, enum cursor_reason), int move, enum cursor_reason reason);
void window_move_all_yscrolls(struct window_split_node* root, struct window_split_node* excluded, int buf_index, int offset, int move);
int  window_other_nodes_contain_file_buffer(struct window_split_node* node, struct window_split_node* root);

enum move_directons {
	MOVE_RIGHT,
	MOVE_LEFT,
	MOVE_UP,
	MOVE_DOWN,
};

struct window_split_node* window_switch_to_window(struct window_split_node* node, enum move_directons move);
// NOTE: if you have two splits both having two splits of the same type, you can't resize the upper split
void window_node_resize(struct window_split_node* node, enum move_directons move, float amount);
void window_node_resize_absolute(struct window_split_node* node, enum move_directons move, float amount);

////////////////////////////////////////////////
// Color Scheme
//

struct color_scheme_arg {
	const char* start;
	const char* end;
};

enum color_scheme_mode {
	// needs two strings
	COLOR_AROUND,
	// needs two strings
	COLOR_AROUND_TO_LINE,
	// needs two strings
	COLOR_INSIDE,
	// needs two strings
	COLOR_INSIDE_TO_LINE,
	// needs two strings
	COLOR_WORD_INSIDE,
	// needs one string
	COLOR_WORD,
	// needs one string
	COLOR_WORD_ENDING_WITH_STR,
	// needs one string
	COLOR_WORD_STARTING_WITH_STR,
	// needs one string
	COLOR_STR,
	// needs two strings
	// colors word if string is found after it
	COLOR_WORD_STR,
	// needs one string
	// can be combined with others if this is first
	COLOR_STR_AFTER_WORD,
	// needs one string
	// "(" would color like this "not_colored colored("
	// "[" would color like this "not_colored colored ["
	COLOR_WORD_BEFORE_STR,
	// needs one string
	// "(" would color like this "colored not_colored("
	// "=" would color like this "colored not_colored ="
	COLOR_WORD_BEFORE_STR_STR,
	// no arguments needed
	COLOR_UPPER_CASE_WORD,
};

struct color_scheme_entry {
	const enum color_scheme_mode mode;
	const struct color_scheme_arg arg;
	const Glyph attr;
};

struct color_scheme {
	const char* file_ending;
	const char* word_seperators;
	const struct color_scheme_entry* entries;
	const int entry_count;
};

////////////////////////////////////////////////
// File buffer
//

struct undo_buffer {
	char* contents; // not null terminated
	int len, capacity;
	int cursor_offset;
	int y_scroll;
};

enum buffer_flags {
	BUFFER_SELECTION_ON = 1 << 0,
	BUFFER_BLOCK_SELECT = 1 << 1,
	BUFFER_LINE_SELECT  = 1 << 2,
	BUFFER_READ_ONLY    = 1 << 3,
	BUFFER_UTF8_SIGNED  = 1 << 4,
	BUFFER_SEARCH_BLOCKING       = 1 << 5,
	BUFFER_SEARCH_BLOCKING_IDLE  = 1 << 6,
	BUFFER_SEARCH_BLOCKING_MASK = (BUFFER_SEARCH_BLOCKING | BUFFER_SEARCH_BLOCKING_IDLE),
	BUFFER_SEARCH_NON_BLOCKING   = 1 << 7,
	BUFFER_SEARCH_BLOCKING_BACKWARDS   = 1 << 8,
	BUFFER_SEARCH_NON_BLOCKING_BACKWARDS   = 1 << 9,
};

// Contents of a file buffer
struct file_buffer {
	char* file_path;
	char* contents; // !! NOT NULL TERMINATED !!
	int len;
	int capacity;
	enum buffer_flags mode;
	struct undo_buffer* ub;
	int current_undo_buffer;
	int available_redo_buffers;
	int s1o, s2o; // selection start offset and end offset
	char* search_term;
	char* non_blocking_search_term;
};

const struct color_scheme* buffer_get_color_scheme(struct file_buffer* fb);
struct file_buffer buffer_new(const char* file_path);
void buffer_destroy(struct file_buffer* fb);
void buffer_insert(struct file_buffer* buf, const char* new_content, const int len, const int offset, int do_not_callback);
void buffer_change(struct file_buffer* buf, const char* new_content, const int len, const int offset, int do_not_callback);
int  buffer_remove(struct file_buffer* buf, const int offset, int len, int do_not_calculate_charsize, int do_not_callback);
void buffer_write_to_filepath(const struct file_buffer* buffer);
void buffer_undo(struct file_buffer* buf);
void buffer_redo(struct file_buffer* buf);

int buffer_is_on_a_word(const struct file_buffer* fb, int offset, const char* word_seperators);
int buffer_is_start_of_a_word(const struct file_buffer* fb, int offset, const char* word_seperators);
int buffer_is_on_word(const struct file_buffer* fb, int offset, const char* word_seperators, const char* word);
int buffer_offset_starts_with(const struct file_buffer* fb, int offset, const char* start);
int buffer_seek_char(const struct file_buffer* buf, int offset, char byte);
int buffer_seek_char_backwards(const struct file_buffer* buf, int offset, char byte);
int buffer_seek_string(const struct file_buffer* buf, int offset, const char* string);
int buffer_seek_string_backwards(const struct file_buffer* buf, int offset, const char* string);
int buffer_seek_string_wrap(const struct window_buffer* wb, int offset, const char* search);
int buffer_seek_string_wrap_backwards(const struct window_buffer* wb, int offset, const char* search);
int buffer_seek_word(const struct file_buffer* fb, int offset, const char* word_seperators);
int buffer_seek_word_end(const struct file_buffer* fb, int offset, const char* word_seperators);
int buffer_seek_word_backwards(const struct file_buffer* fb, int offset, const char* word_seperators);
int buffer_seek_whitespace(const struct file_buffer* fb, int offset);
int buffer_seek_whitespace_backwrads(const struct file_buffer* fb, int offset);
int buffer_seek_not_whitespace(const struct file_buffer* fb, int offset);
int buffer_seek_not_whitespace_backwrads(const struct file_buffer* fb, int offset);

int buffer_count_string_instances(const struct file_buffer* fb, const char* string, int offset, int* before_offset);

///////////////////////////////////
// returns a null terminated string containing the selection
// the returned value must be freed by the reciever
// for conveniance the length of the string may be taken with the pointer
// a selection_len of NULL wil be ignored
char* buffer_get_selection(struct file_buffer* buf, int* selection_len);
int   buffer_is_selection_start_top_left(const struct file_buffer* buffer);
void  buffer_remove_selection(struct file_buffer* buffer);

///////////////////////////////////
// returns a null terminated string containing the current line
// the returned value must be freed by the reciever
char* buffer_get_line_at_offset(const struct file_buffer* fb, int offset);
// result must be freed
char* file_path_get_path(const char* path);

////////////////////////////////////////////////
// Other
//

struct keyword_pos {
	int offset, buffer_index;
};

const char* file_browser_next_item(const char* path, const char* search, int* offset, Glyph* attr, void* data);
// data pointer will give the file buffer of the current item
const char* buffer_search_next_item(const char* tmp, const char* search, int* offset, Glyph* attr, void* data);
// data pointer will give the keyword_pos of the current item
const char* buffers_search_keyword_next_item(const char* tmp, const char* search, int* offset, Glyph* attr, void* data);

void die(const char *, ...);
int is_file_type(const char* file_path, const char* file_type);
int path_is_folder(const char* path);
int write_string(const char* string, int y, int minx, int maxx);
int writef_to_status_bar(const char* fmt, ...);
void draw_status_bar();
void remove_utf8_string_end(char* string);

// Internal representation of the screen
typedef struct {
	int row;      // row count
	int col;      // column count
	Line *line;   // array
} Term;

void tnew(int, int);
void tresize(int, int);
void tsetregion(int x1, int y1, int x2, int y2, Rune u);
int tsetchar(Rune u, int x, int y);
Glyph* tsetattr(int x, int y);
Rune tgetrune(int x, int y);

size_t utf8encode(Rune, char *);
void *xmalloc(size_t);
void *xrealloc(void *, size_t);

enum buffer_content_reason {
	BUFFER_CONTENT_DO_NOT_CALLBACK,
	BUFFER_CONTENT_OPERATION_ENDED,
	BUFFER_CONTENT_NORMAL_EDIT,
	BUFFER_CONTENT_BIG_CHANGE,
	BUFFER_CONTENT_INIT,
};

void buffer_add_to_undo(struct file_buffer* buffer, int offset, enum buffer_content_reason reason);

#endif // _ST_H
