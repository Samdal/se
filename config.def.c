#include <assert.h>
#include <time.h>

#include "x.h"

////////////////////////////////////////
// apperance
//

// font: see http://freedesktop.org/software/fontconfig/fontconfig-user.html
char *font = "Iosevka:pixelsize=16:antialias=true:autohint=true";

// pixels of border around the window
int border_px = 2;

// default size of the editor
unsigned int default_cols = 80;
unsigned int default_rows = 24;

// Kerning / character bounding-box multipliers
float cw_scale = 1.0;
float ch_scale = 1.0;

int wrap_buffer = 0;

// spaces per tab (tabs will self align)
unsigned int tabspaces = 8;

///////////////////////////////////////////
// colour scheme
// this color scheme is gruvbox
// see colors at: https://github.com/morhetz/gruvbox
//

enum colour_names {
	bg,
	bg0_h,
	fg,
	sel,
	line,
	red,
	dark_green,
	green,
	teal,
	yellow,
	orange,
	blue,
	purple,
	aqua,
	gray,
};

const char * const colors[] = {
	[bg]	 = "#282828",
	[bg0_h]	 = "#1d2021",
	[fg]	 = "#fbf1c7",
	[sel]	 = "#504945",
	[line]   = "#32302f",
	[red]	 = "#cc251d",
	[dark_green] = "#98971a",
	[green]  = "#b8bb26",
	[teal]   = "#8ec07c",
	[yellow] = "#fabd2f",
	[orange] = "#d65d0e",
	[blue]	 = "#458588",
	[purple] = "#b16286",
	[aqua]	 = "#83a598",
	[gray]	 = "#a89984",
	NULL
};

// default colors
Glyph default_attributes = {.fg = fg, .bg = bg};
unsigned int mouse_fg = fg;
unsigned int mouse_bg = bg;
unsigned int selection_bg = sel;
unsigned int mouse_line_bg = line;

// Default shape of cursor
// 2: Block ("█")
// 4: Underline ("_")
// 6: Bar ("|")
// 7: Snowman ("☃")
unsigned int cursor_shape = 2;

// thickness of underline and bar cursors
unsigned int cursor_thickness = 2;

//////////////////////////////////////////
// Color scheme
//
#define str_color gray
#define comment_color gray
#define type_color teal
#define keyword_color green
#define macro_color yellow
#define operator_color yellow
#define constants_color dark_green
#define number_color gray
#define color_macro(_str) {COLOR_WORD,{_str},    macro_color}
#define color_keyword(_str) {COLOR_WORD,{_str},  keyword_color}
#define color_type(_str) {COLOR_WORD,{_str},  type_color}
#define color_number(_num)												\
	{COLOR_WORD_STARTING_WITH_STR, {_num}, number_color},				\
	{COLOR_WORD_ENDING_WITH_STR, {_num".f"},number_color}

// (order matters)
const struct color_scheme_entry c_color_scheme[] =  {
	// Coloring type           arguments       Color

	// strings
	{COLOR_AROUND_TO_LINE,    {"\"", "\""},   str_color},
	{COLOR_STR,               {"''"},         fg},
	{COLOR_AROUND_TO_LINE,    {"'", "'"},     str_color},
	{COLOR_INSIDE_TO_LINE,    {"#include <", ">"}, str_color},
	{COLOR_INSIDE_TO_LINE,    {"#include<", ">"},  str_color},
	// comments
	{COLOR_AROUND,            {"/*", "*/"},   comment_color},
	{COLOR_AROUND,            {"//", "\n"},   comment_color},
	// macros
	{COLOR_UPPER_CASE_WORD,   {0},            constants_color},
	{COLOR_STR_AFTER_WORD,    {"#ifdef"},     constants_color},
	{COLOR_STR_AFTER_WORD,    {"#ifndef"},    constants_color},
	{COLOR_STR_AFTER_WORD,    {"#define"},    constants_color},
	{COLOR_STR_AFTER_WORD,    {"#undef"},     constants_color},
	{COLOR_WORD_STARTING_WITH_STR, {"#"},     macro_color},
	color_macro("sizeof"),
	color_macro("alignof"),
	color_macro("offsetof"),
	color_macro("va_arg"),
	color_macro("va_start"),
	color_macro("va_end"),
	color_macro("va_copy"),
	{COLOR_STR_AFTER_WORD,    {"defined"},       constants_color},
	color_macro("defined"),
	// operators
	{COLOR_STR,               {"!="},         fg},
	{COLOR_STR,               {"!"},          operator_color},
	{COLOR_STR,               {"~"},          operator_color},
	{COLOR_STR,               {"?"},          operator_color},
	// keywords
	{COLOR_WORD_STR,          {"...", ")"},   keyword_color},
	{COLOR_WORD_STR,          {"struct", "{"},keyword_color},
	{COLOR_WORD_STR,          {"union", "{"}, keyword_color},
	{COLOR_WORD_STR,          {"enum", "{"},  keyword_color},
	{COLOR_STR_AFTER_WORD,    {"struct"},     type_color},
	{COLOR_STR_AFTER_WORD,    {"union"},      type_color},
	{COLOR_STR_AFTER_WORD,    {"enum"},       type_color},
	{COLOR_STR_AFTER_WORD,    {"goto"},       constants_color},
	{COLOR_WORD_INSIDE,       {"}", ":"},     constants_color},
	{COLOR_WORD_INSIDE,       {"{", ":"},     constants_color},
	{COLOR_WORD_INSIDE,       {";", ":"},     constants_color},
	color_keyword("struct"),
	color_keyword("enum"),
	color_keyword("union"),
	color_keyword("const"),
	color_keyword("typedef"),
	color_keyword("extern"),
	color_keyword("static"),
	color_keyword("inline"),
	color_keyword("if"),
	color_keyword("else"),
	color_keyword("for"),
	color_keyword("while"),
	color_keyword("case"),
	color_keyword("switch"),
	color_keyword("do"),
	color_keyword("return"),
	color_keyword("break"),
	color_keyword("continue"),
	color_keyword("goto"),
	color_keyword("restrict"),
	color_keyword("register"),
	// functions
	//{COLOR_WORD_BEFORE_STR,      {"("},          aqua},
	// types
	color_type("int"),
	color_type("unsigned"),
	color_type("long"),
	color_type("short"),
	color_type("char"),
	color_type("void"),
	color_type("float"),
	color_type("double"),
	color_type("complex"),
	color_type("bool"),
	color_type("_Bool"),
	color_type("FILE"),
	color_type("va_list"),
	{COLOR_WORD_ENDING_WITH_STR, {"_t"},          type_color},
	{COLOR_WORD_ENDING_WITH_STR, {"_type"},       type_color},
	{COLOR_WORD_ENDING_WITH_STR, {"T"},           type_color},
	// numbers
	color_number("0"),
	color_number("1"),
	color_number("2"),
	color_number("3"),
	color_number("4"),
	color_number("5"),
	color_number("6"),
	color_number("7"),
	color_number("8"),
	color_number("9"),
};

#define default_word_seperators "., \n\t*+-/%!~<>=(){}[]\"^&|\\\'?:;"

const struct color_scheme color_schemes[] = {
	{".c", default_word_seperators, c_color_scheme, LEN(c_color_scheme)},
	{".h", default_word_seperators, c_color_scheme, LEN(c_color_scheme)},
	{0},
};

///////////////////////////////////////////////////
// Declarations
//

typedef union {
	int i;
	uint ui;
	float f;
	int vec2i[2];
	const void *v;
	const char *s;
} Arg;

static void numlock(const Arg* arg);
static void window_split(const Arg* arg);
static void window_resize(const Arg *arg);
static void window_delete(const Arg *arg);
static void window_change(const Arg* arg);
static void zoom(const Arg* arg);
static void zoomabs(const Arg* arg);
static void zoomreset(const Arg* arg);
static void cursor_move_x_relative(const Arg* arg);
static void cursor_move_y_relative(const Arg* arg);
static void save_buffer(const Arg* arg);
static void toggle_selection(const Arg* arg);
static void move_cursor_to_offset(const Arg* arg);
static void move_cursor_to_end_of_buffer(const Arg* arg);
static void clipboard_copy(const Arg* arg);
static void clipboard_paste(const Arg* arg);
static void undo(const Arg* arg);
static void redo(const Arg* arg);

static void cursor_callback(struct window_buffer* buf, enum cursor_reason callback_reason);
static void buffer_content_callback(struct file_buffer* buffer, int offset, enum buffer_content_reason reason);
static int keypress_actions(KeySym keysym, int modkey);
static void string_insert_callback(const char* buf, int buflen);
static void xunloadfonts(void);
static void xunloadfont(Font *);
static void buffer_copy_ub_to_current(struct window_buffer* buffer);
static void keep_cursor_col(struct window_buffer* buf, enum cursor_reason callback_reason);
static void move_selection(struct window_buffer* buf, enum cursor_reason callback_reason);
static void add_to_undo_buffer(struct file_buffer* buffer, int offset, enum buffer_content_reason reason);

/////////////////////////////////////////
// Shortcuts
//

typedef struct {
	uint mod;
	KeySym keysym;
	void (*func)(const Arg* arg);
	const Arg arg;
} Shortcut;

#define MODKEY Mod1Mask
#define TERMMOD (ControlMask|ShiftMask)

const Shortcut shortcuts[] = {
//    mask                  keysym          function        argument
	{ 0,                    XK_Right,       cursor_move_x_relative,       {.i = +1} },
	{ 0,                    XK_Left,        cursor_move_x_relative,       {.i = -1} },
	{ 0,                    XK_Down,        cursor_move_y_relative,       {.i = +1} },
	{ 0,                    XK_Up,          cursor_move_y_relative,       {.i = -1} },
	{ ControlMask,          XK_Right,       window_change,                {.i = MOVE_RIGHT} },
	{ ControlMask,          XK_Left,        window_change,                {.i = MOVE_LEFT}  },
	{ ControlMask,          XK_Down,        window_change,                {.i = MOVE_DOWN}  },
	{ ControlMask,          XK_Up,          window_change,                {.i = MOVE_UP}    },
	{ TERMMOD,              XK_Right,       window_resize,                {.i = MOVE_RIGHT} },
	{ TERMMOD,              XK_Left,        window_resize,                {.i = MOVE_LEFT}  },
	{ TERMMOD,              XK_Down,        window_resize,                {.i = MOVE_DOWN}  },
	{ TERMMOD,              XK_Up,          window_resize,                {.i = MOVE_UP}    },
	{ ControlMask,          XK_m,           toggle_selection,             {0}       },
	{ ControlMask,          XK_g,           move_cursor_to_offset,        {0}       },
	{ TERMMOD,              XK_G,           move_cursor_to_end_of_buffer, {0}       },
	{ ControlMask,          XK_l,           window_split,   {.i = WINDOW_HORISONTAL}},
	{ ControlMask,          XK_k,           window_split,   {.i = WINDOW_VERTICAL}  },
	{ ControlMask,          XK_d,           window_delete,  {0}      },
	{ ControlMask,          XK_z,           undo,           {0}       },
	{ TERMMOD,              XK_Z,           redo,           {0}       },
	{ ControlMask,          XK_s,           save_buffer,    {0}       },
	{ ControlMask,          XK_c,           clipboard_copy, {0}       },
	{ ControlMask,          XK_v,           clipboard_paste,{0}       },
	{ TERMMOD,              XK_Prior,       zoom,           {.f = +1} },
	{ TERMMOD,              XK_Next,        zoom,           {.f = -1} },
	{ TERMMOD,              XK_Home,        zoomreset,      {.f =  0} },
	{ TERMMOD,              XK_Num_Lock,    numlock,        {.i =  0} },
};

/////////////////////////////////////////////////
// callbacks
//

void(*cursor_movement_callback)(struct window_buffer*, enum cursor_reason) = cursor_callback;
void(*buffer_contents_updated)(struct file_buffer*, int, enum buffer_content_reason) = buffer_content_callback;
int(*keypress_callback)(KeySym, int) = keypress_actions;
void(*string_input_callback)(const char*, int) = string_insert_callback;
void(*draw_callback)(void) = NULL;
void(*buffer_written_to_screen_callback)(struct file_buffer*, int, int,  int, int, int, int) = NULL;


////////////////////////////////////////////////
// external globals
//

extern struct window_split_node root_node;
extern struct window_buffer* focused_window;
extern struct window_split_node* focused_node;
extern TermWindow win;
extern XWindow xw;
extern DC dc;

extern Term term;

extern Fontcache *frc;
extern int frclen;
extern Atom xtarget;
extern double defaultfontsize;
extern double usedfontsize;
extern char* copy_buffer;
extern int copy_len;

/////////////////////////////////////////////////
// function implementations
//

void
numlock(const Arg *dummy)
{
	win.mode ^= MODE_NUMLOCK;
}

void window_split(const Arg *arg)
{
	window_node_split(focused_node, 0.5, arg->i);
	focused_node = focused_node->node2;
	focused_window = &focused_node->window;
}

void window_resize(const Arg *arg)
{
	window_node_resize(focused_node, arg->i);
}

void window_delete(const Arg *arg)
{
	struct window_split_node* new_node = window_node_delete(focused_node);
	while (new_node->mode != WINDOW_SINGULAR)
		new_node = new_node->node1;
	focused_node = new_node;
	focused_window = &focused_node->window;
}

void window_change(const Arg *arg)
{
	focused_node = window_switch_to_window(focused_node, arg->i);
	focused_window = &focused_node->window;
}

void
zoom(const Arg *arg)
{
	Arg larg;

	larg.f = usedfontsize + arg->f;
	zoomabs(&larg);
}

void
zoomabs(const Arg *arg)
{
	xunloadfonts();
	xloadfonts(font, arg->f);
	cresize(0, 0);
	xhints();
}

void
zoomreset(const Arg *arg)
{
	Arg larg;

	if (defaultfontsize > 0) {
		larg.f = defaultfontsize;
		zoomabs(&larg);
	}
}

void
cursor_move_x_relative(const Arg* arg)
{
	buffer_move_on_line(focused_window, arg->i, CURSOR_RIGHT_LEFT_MOVEMENT);
}

void
cursor_move_y_relative(const Arg* arg)
{
	buffer_move_lines(focused_window, arg->i, 0);
	buffer_move_to_x(focused_window, focused_window->cursor_col, CURSOR_UP_DOWN_MOVEMENT);
}

void
save_buffer(const Arg* arg)
{
	buffer_write_to_filepath(get_file_buffer(focused_window));
}

void
toggle_selection(const Arg* arg)
{
	struct file_buffer* fb = get_file_buffer(focused_window);
	if (fb->mode & BUFFER_SELECTION_ON) {
		fb->mode &= ~(BUFFER_SELECTION_ON);
	} else {
		fb->mode |= BUFFER_SELECTION_ON;
		fb->s1o = fb->s2o = focused_window->cursor_offset;
	}
}

void
move_cursor_to_offset(const Arg* arg)
{
	focused_window->cursor_offset = arg->i;
}

void
move_cursor_to_end_of_buffer(const Arg* arg)
{
	focused_window->cursor_offset = get_file_buffer(focused_window)->len-1;
}

void
clipboard_copy(const Arg* arg)
{
	struct file_buffer* fb = get_file_buffer(focused_window);
	int len;
	char* temp = buffer_get_selection(fb, &len);
	if (!temp)
		return;
	if (copy_buffer)
		free(copy_buffer);
	copy_buffer = temp;
	copy_len = len;

	buffer_move_cursor_to_selection_start(focused_window);
	fb->mode &= ~BUFFER_SELECTION_ON;

	Atom clipboard;
	clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
	XSetSelectionOwner(xw.dpy, clipboard, xw.win, CurrentTime);
}

void
clipboard_paste(const Arg* arg)
{
	Atom clipboard;

	clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
	XConvertSelection(xw.dpy, clipboard, xtarget, clipboard,
					  xw.win, CurrentTime);
}

void
buffer_copy_ub_to_current(struct window_buffer* buffer)
{
	struct file_buffer* fb = get_file_buffer(buffer);
	struct undo_buffer* cub = &fb->ub[fb->current_undo_buffer];
	assert(cub->contents);

	fb->contents = xrealloc(fb->contents, cub->capacity);
	memcpy(fb->contents, cub->contents, cub->capacity);
	fb->len = cub->len;
	fb->capacity = cub->capacity;

	buffer_move_to_offset(buffer, cub->cursor_offset, CURSOR_SNAPPED);
	buffer->y_scroll = cub->y_scroll;
}

void
undo(const Arg* arg)
{
	struct file_buffer* fb = get_file_buffer(focused_window);
	if (fb->current_undo_buffer == 0)
		return;
	fb->current_undo_buffer--;
	fb->available_redo_buffers++;

	buffer_copy_ub_to_current(focused_window);
}

void
redo(const Arg* arg)
{
	struct file_buffer* fb = get_file_buffer(focused_window);
	if (fb->available_redo_buffers == 0)
		return;
	fb->available_redo_buffers--;
	fb->current_undo_buffer++;

	buffer_copy_ub_to_current(focused_window);
}

void
xunloadfonts(void)
{
	/* Free the loaded fonts in the font cache.  */
	while (frclen > 0)
		XftFontClose(xw.dpy, frc[--frclen].font);

	xunloadfont(&dc.font);
	xunloadfont(&dc.bfont);
	xunloadfont(&dc.ifont);
	xunloadfont(&dc.ibfont);
}

void
xunloadfont(Font *f)
{
	XftFontClose(xw.dpy, f->match);
	FcPatternDestroy(f->pattern);
	if (f->set)
		FcFontSetDestroy(f->set);
}

////////////////////////////////////////////////////////7
// Callbacks
//

void
keep_cursor_col(struct window_buffer* buf, enum cursor_reason callback_reason)
{
	static int last_cursor_col;

	if (callback_reason == CURSOR_COMMAND_MOVEMENT || callback_reason == CURSOR_RIGHT_LEFT_MOVEMENT) {
		int y;
		buffer_offset_to_xy(buf, buf->cursor_offset, -1, &buf->cursor_col, &y);
		last_cursor_col = buf->cursor_col;
	}
}

void move_selection(struct window_buffer* buf, enum cursor_reason callback_reason)
{
	struct file_buffer* fb = get_file_buffer(buf);
	if (fb->mode & BUFFER_SELECTION_ON) {
		fb->s2o = buf->cursor_offset;
	}
}

void
cursor_callback(struct window_buffer* buf, enum cursor_reason callback_reason)
{
	keep_cursor_col(buf, callback_reason);
	move_selection(buf, callback_reason);

	printf("mved to: %d | reason: %d\n", buf->cursor_offset, callback_reason);
}

void
add_to_undo_buffer(struct file_buffer* buffer, int offset, enum buffer_content_reason reason)
{
	static time_t last_normal_edit;
	static int edits;

	if (reason == BUFFER_CONTENT_NORMAL_EDIT) {
		time_t previous_time = last_normal_edit;
		last_normal_edit = time(NULL);

		if (last_normal_edit - previous_time < 2 && edits < 30) {
			edits++;
			goto copy_undo_buffer;
		} else {
			edits = 0;
		}
	} else if (reason == BUFFER_CONTENT_INIT) {
		goto copy_undo_buffer;
	}

	if (buffer->available_redo_buffers > 0) {
		buffer->available_redo_buffers = 0;
		buffer->current_undo_buffer++;
		goto copy_undo_buffer;
	}

	if (buffer->current_undo_buffer == UNDO_BUFFERS_COUNT-1) {
		char* begin_buffer = buffer->ub[0].contents;
		memmove(buffer->ub, &(buffer->ub[1]), (UNDO_BUFFERS_COUNT-1) * sizeof(struct undo_buffer));
		buffer->ub[buffer->current_undo_buffer].contents = begin_buffer;
	} else {
		buffer->current_undo_buffer++;
	}

copy_undo_buffer: ;
	struct undo_buffer* cub = &buffer->ub[buffer->current_undo_buffer];

	cub->contents = xrealloc(cub->contents, buffer->capacity);
	memcpy(cub->contents, buffer->contents, buffer->capacity);
	cub->len = buffer->len;
	cub->capacity = buffer->capacity;
	cub->cursor_offset = offset;
	if (focused_window)
		cub->y_scroll = focused_window->y_scroll;
	else
		cub->y_scroll = 0;
}

void
buffer_content_callback(struct file_buffer* buffer, int offset, enum buffer_content_reason reason)
{
	add_to_undo_buffer(buffer, offset, reason);
}

int
keypress_actions(KeySym keysym, int modkey)
{
	// check shortcuts
	for (int i = 0; i < LEN(shortcuts); i++) {
		if (keysym == shortcuts[i].keysym && match(shortcuts[i].mod, modkey)) {
			shortcuts[i].func(&(shortcuts[i].arg));
			return 1;
		}
	}

	// default actions

	int offset = focused_window->cursor_offset;
	struct file_buffer* fb = get_file_buffer(focused_window);

	switch (keysym) {
		int move;
	case XK_BackSpace:
		if (delete_selection(fb)) return 1;
		if (offset <= 0) return 1;

		if (fb->contents[offset-1] == '\n')
			buffer_move_lines(focused_window, -1, CURSOR_COMMAND_MOVEMENT);
		 else
			buffer_move_on_line(focused_window, -1, CURSOR_COMMAND_MOVEMENT);

		offset = focused_window->cursor_offset;

		// FALLTHROUGH
	case XK_Delete:

		if (delete_selection(fb)) return 1;

		move = buffer_remove(fb, offset, 1, 0, 0);
		window_move_all_cursors_on_same_buf(&root_node, focused_node, focused_window->buffer_index, offset,
											buffer_move_offset_relative, -move, CURSOR_COMMAND_MOVEMENT);
		return 1;
	case XK_Return:
		delete_selection(fb);

		buffer_insert(fb, "\n", 1, offset, 0);
		window_move_all_cursors_on_same_buf(&root_node, NULL, focused_window->buffer_index, offset,
											buffer_move_offset_relative, 1, CURSOR_COMMAND_MOVEMENT);
		window_move_all_yscrolls(&root_node, focused_node, focused_window->buffer_index, offset, 1);
		return 1;
	case XK_Home: {
		int new_offset = buffer_seek_char_backwards(fb, offset, '\n');
		if (new_offset < 0)
			new_offset = 0;
		buffer_move_to_offset(focused_window, new_offset, CURSOR_COMMAND_MOVEMENT);
		return 1;
	}
	case XK_End: {
		int new_offset = buffer_seek_char(fb, offset, '\n');
		if (new_offset < 0)
			new_offset = fb->len-1;
		buffer_move_to_offset(focused_window, new_offset, CURSOR_COMMAND_MOVEMENT);
		return 1;
	}
	case XK_Page_Down:
		buffer_move_lines(focused_window, (term.row-1) / 2, 0);
		buffer_move_to_x(focused_window, focused_window->cursor_col, CURSOR_UP_DOWN_MOVEMENT);
		focused_window->y_scroll += (term.row-1) / 2;
		return 1;
	case XK_Page_Up:
		buffer_move_lines(focused_window, -((term.row-1) / 2), 0);
		buffer_move_to_x(focused_window, focused_window->cursor_col, CURSOR_UP_DOWN_MOVEMENT);
		focused_window->y_scroll -= (term.row-1) / 2;
		return 1;
	case XK_Tab:
		buffer_insert(fb, "\t", 1, offset, 0);
		window_move_all_cursors_on_same_buf(&root_node, NULL, focused_window->buffer_index, offset,
											buffer_move_on_line, 1, CURSOR_COMMAND_MOVEMENT);
		return 1;
	}
	return 0;
}

void string_insert_callback(const char* buf, int buflen)
{
	struct file_buffer* fb = get_file_buffer(focused_window);

	// TODO: allow blocking of the bufferwrite, redirecting to keybinds with multiple characther length
	if (buf[0] >= 32) {
		delete_selection(fb);
		buffer_insert(fb, buf, buflen, focused_window->cursor_offset, 0);
		window_move_all_cursors_on_same_buf(&root_node, NULL, focused_window->buffer_index, focused_window->cursor_offset,
											buffer_move_offset_relative, buflen, CURSOR_COMMAND_MOVEMENT);
	} else {
		printf("unhandled control character %x\n", buf[0]);
	}
}
