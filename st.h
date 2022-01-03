/* See LICENSE for license details. */

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
#define TIMEDIFF(t1, t2)	((t1.tv_sec-t2.tv_sec)*1000 + \
				(t1.tv_nsec-t2.tv_nsec)/1E6)
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

#define Glyph Glyph_
typedef struct {
	Rune u;           /* character code */
	ushort mode;      /* attribute flags */
	uint32_t fg;      /* foreground  */
	uint32_t bg;      /* background  */
} Glyph;

typedef Glyph *Line;

typedef union {
	int i;
	uint ui;
	float f;
	int vec2i[2];
	const void *v;
	const char *s;
} Arg;

enum buffer_flags {
	BUFFER_SELECTION_ON = 1 << 0,
	BUFFER_BLOCK_SELECT = 1 << 1,
	BUFFER_READ_ONLY    = 1 << 2,
};

struct undo_buffer {
	char* contents; // not null terminated
	int len, capacity;
	int cx, cy; // cursor x and y
	int xscroll, yscroll;
};

/* Contents of a file buffer */
typedef struct {
	char* file_path;
	char* contents; // !! NOT NULL TERMINATED !!
	int len;
	int capacity;
	int y_scroll, x_scroll;
	int cursor_col;
	int mode; // flags
	int s1o, s2o; // selection start offset and end offset
	struct undo_buffer* ub;
	int undo_buffer_len;
	int current_undo_buffer;
	int available_redo_buffers;
} Buffer;

void bufferwrite(const char* buf, const int buflen);
int  buffer_new(Buffer* buffer, char* file_path);
void buffer_insert(Buffer* buffer, const char* new_content, const int len, const int offset, int do_not_callback);
void buffer_change(Buffer* buffer, const char* new_content, const int len, const int offset, int do_not_callback);
void buffer_remove(Buffer* buffer, const int offset, int len, int do_not_calculate_charsize, int do_not_callback);
void buffer_write_to_screen(Buffer* buffer);
void buffer_write_to_filepath(const Buffer* buffer);
int  buffer_snap_cursor(Buffer* buffer, int do_not_callback);
void buffer_move_cursor_to_offset(Buffer* buffer, int offset, int do_not_callback);
void buffer_scroll(Buffer* buffer, int xscroll, int yscroll);

void buffer_write_selection(Buffer* buffer);
///////////////////////////////////
// returns a null terminated string containing the selection
// the returned value must be freed by the reciever
// for conveniance the length of the string may be taken with the pointer
// a selection_len of NULL wil be ignored
char* buffer_get_selection(Buffer* buffer, int* selection_len);
int   buffer_is_selection_start_top_left(Buffer* buffer);
void  buffer_move_cursor_to_selection_start(Buffer* buffer);
void  buffer_remove_selection(Buffer* buffer);

void die(const char *, ...);
void redraw(void);
void draw(void);

enum term_mode {
	MODE_INSERT      = 1 << 1,
	MODE_ALTSCREEN   = 1 << 2,
	MODE_UTF8        = 1 << 6,
};

//TODO: make these attributes more fit this program
typedef struct {
	Glyph attr; /* current char attributes */
	int x;
	int y;
} TCursor;

/* Internal representation of the screen */
typedef struct {
	int row;      /* nb row */
	int col;      /* nb col */
	Line *line;   /* screen */
	Line *alt;    /* alternate screen */
	int *dirty;   /* dirtyness of lines */
	TCursor c;    /* cursor */
	int ocx, ocy; // old cursor
	int mode;     /* terminal mode flags */
	int *tabs;
	Rune lastc;   /* last printed char outside of sequence, 0 if control */
} Term;
extern Term term;

int tattrset(int);
void tnew(int, int);
void tresize(int, int);
void tsetdirtattr(int);

int tisdirty();
int tsetchar(Rune, Glyph, int, int);

void resettitle(void);

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

void buffer_move_cursor(Buffer* buffer, int x, int y, enum cursor_reason callback_reason);
void buffer_move_cursor_relative(Buffer* buffer, int x, int y, enum cursor_reason callback_reason);

/* callbacks */
extern void(*cursor_movement_callback)(int, int, enum cursor_reason); // cursor x & y, callback_reason
extern void(*buffer_contents_updated)(Buffer*, int, int, enum buffer_content_reason); // modified buffer, cursor x & y
// TODO: planned callbacks:
// buffer written

/* config.h globals */
extern char *termname;
extern unsigned int tabspaces;
extern unsigned int defaultfg;
extern unsigned int defaultbg;
extern int default_mode;
extern int undo_buffers;
