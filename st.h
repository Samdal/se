/* See LICENSE for license details. */
#ifndef _ST_H
#define _ST_H

#include <stdint.h>
#include <sys/types.h>
#include <wchar.h>

/* macros */
#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) < (b) ? (b) : (a))
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#define ATTRCMP(a, b)		((a).mode != (b).mode || (a).fg != (b).fg || \
				(a).bg != (b).bg)
#define MODBIT(x, set, bit)	((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))

#define TRUECOLOR(r,g,b)	(1 << 24 | (r) << 16 | (g) << 8 | (b))
#define IS_TRUECOL(x)		(1 << 24 & (x))

enum glyph_attribute {
	ATTR_NULL       = 0,
	ATTR_BOLD       = 1 << 0,
	ATTR_FAINT      = 1 << 1,
	ATTR_ITALIC     = 1 << 2,
	ATTR_UNDERLINE  = 1 << 3,
	ATTR_REVERSE    = 1 << 5,
	ATTR_INVISIBLE  = 1 << 6,
	ATTR_STRUCK     = 1 << 7,
	ATTR_WRAP       = 1 << 8,
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
	Rune u;           /* character code */
	ushort mode;      /* attribute flags */
	uint32_t fg;      /* foreground  */
	uint32_t bg;      /* background  */
} Glyph_;
#define Glyph Glyph_

typedef Glyph *Line;

typedef union {
	int i;
	uint ui;
	float f;
	int vec2i[2];
	const void *v;
	const char *s;
} Arg;

struct undo_buffer {
	char* contents; // not null terminated
	int len, capacity;
	int cursor_offset;
	int y_scroll;
};

struct window_buffer {
	int y_scroll;
	int cursor_offset;
	int cursor_col; // last right_left movement for snapping the cursor

	int buffer_index; // index into an array storing buffers
};

enum window_split_mode {
	WINDOW_SINGULAR,
	WINDOW_HORISONTAL,
	WINDOW_VERTICAL,
	WINDOW_HORISONTAL_NOT_RESISABLE,
};
#define IS_HORISONTAL(mode) ((mode) == WINDOW_HORISONTAL || (mode) == WINDOW_HORISONTAL_NOT_RESISABLE)

struct window_split_node {
	struct window_buffer window;
	enum window_split_mode mode;
	float ratio;
	struct window_split_node *node1, *node2, *parent;
};

void window_buffer_split(struct window_split_node* parent, float ratio, enum window_split_mode mode);
// uses focused_window to draw the cursor
void window_write_tree_to_screen(struct window_split_node* root, int minx, int miny, int maxx, int maxy);

enum move_directons {
	MOVE_RIGHT,
	MOVE_LEFT,
	MOVE_UP,
	MOVE_DOWN,
};
struct window_split_node* window_switch_to_window(struct window_split_node* current, enum move_directons move);


enum buffer_flags {
	BUFFER_SELECTION_ON = 1 << 0,
	BUFFER_BLOCK_SELECT = 1 << 1,
	BUFFER_READ_ONLY    = 1 << 2,
	BUFFER_UTF8_SIGNED  = 1 << 3,
};

/* Contents of a file buffer */
struct file_buffer {
	char* file_path;
	char* contents; // !! NOT NULL TERMINATED !!
	int len;
	int capacity;
	int mode; // flags
	struct undo_buffer* ub;
	int current_undo_buffer;
	int available_redo_buffers;
	int s1o, s2o; // selection start offset and end offset
	//TODO: colour instructions
};

struct file_buffer buffer_new(char* file_path);
void buffer_insert(struct file_buffer* buf, const char* new_content, const int len, const int offset, int do_not_callback);
void buffer_change(struct file_buffer* buf, const char* new_content, const int len, const int offset, int do_not_callback);
void buffer_remove(struct file_buffer* buf, const int offset, int len, int do_not_calculate_charsize, int do_not_callback);
void buffer_offset_to_xy(struct window_buffer* buf, int offset, int maxx, int* cx, int* cy);
void buffer_write_to_screen(struct window_buffer* buf, int minx, int miny, int maxx, int maxy);
void buffer_write_to_filepath(const struct file_buffer* buffer);

void buffer_write_selection(struct window_buffer* buf, int minx, int miny, int maxx, int maxy);
///////////////////////////////////
// returns a null terminated string containing the selection
// the returned value must be freed by the reciever
// for conveniance the length of the string may be taken with the pointer
// a selection_len of NULL wil be ignored
char* buffer_get_selection(struct file_buffer* buf, int* selection_len);
int   buffer_is_selection_start_top_left(const struct file_buffer* buffer);
void  buffer_move_cursor_to_selection_start(struct window_buffer* buffer);
void  buffer_remove_selection(struct file_buffer* buffer);

void die(const char *, ...);

void draw(int cursor_x, int cursor_y);

/* Internal representation of the screen */
typedef struct {
	int row;      /* nb row */
	int col;      /* nb col */
	Line *line;   /* screen */
	int ocx, ocy; // old cursor
	Rune lastc;   /* last printed char outside of sequence, 0 if control */
} Term;
extern Term term;

int tattrset(int);
void tnew(int, int);
void tresize(int, int);
void tsetregion(int x1, int y1, int x2, int y2, Rune u);
int tsetchar(Rune u, int x, int y);

size_t utf8encode(Rune, char *);
void *xmalloc(size_t);
void *xrealloc(void *, size_t);

enum cursor_reason {
	CURSOR_DO_NOT_CALLBACK = 0,
	CURSOR_COMMAND_MOVEMENT = 1,
	CURSOR_UP_DOWN_MOVEMENT,
	CURSOR_RIGHT_LEFT_MOVEMENT,
	CURSOR_SNAPPED,
	CURSOR_WINDOW_RESIZED,
	CURSOR_SCROLL_ONLY,
};

enum buffer_content_reason {
	BUFFER_CONTENT_DO_NOT_CALLBACK,
	BUFFER_CONTENT_OPERATION_ENDED,
	BUFFER_CONTENT_NORMAL_EDIT,
	BUFFER_CONTENT_BIG_CHANGE,
	BUFFER_CONTENT_INIT,
};

void buffer_move_on_line(struct window_buffer* buf, int amount, enum cursor_reason callback_reason);
void buffer_move_lines(struct window_buffer* buf, int amount, enum cursor_reason callback_reason);
void buffer_move_to_offset(struct window_buffer* buf, int offset, enum cursor_reason callback_reason);
void buffer_move_to_x(struct window_buffer* buf, int x, enum cursor_reason callback_reason);
int  buffer_seek_char(const struct file_buffer* buf, int offset, char byte);
int  buffer_seek_char_backwards(const struct file_buffer* buf, int offset, char byte);
int  buffer_seek_string(const struct file_buffer* buf, int offset, const char* string);
int  buffer_seek_string_backwards(const struct file_buffer* buf, int offset, const char* string);

/* callbacks */
extern void(*cursor_movement_callback)(struct window_buffer*, enum cursor_reason); // buffer, current_pos, reason
extern void(*buffer_contents_updated)(struct file_buffer*, int, enum buffer_content_reason); // modified buffer, current_pos
// TODO: planned callbacks:
// buffer written

/* config.h globals */
extern Glyph default_attributes;
extern char *termname;
extern unsigned int tabspaces;

#define UNDO_BUFFERS_COUNT 32
#define WRAP_BUFFER 0

#endif // _ST_H
