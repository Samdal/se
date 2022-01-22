#include <assert.h>

#include "x.h"

////////////////////////////////////////
// apperance
//

// font: see http://freedesktop.org/software/fontconfig/fontconfig-user.html
//char *fontconfig = "Liberation Mono:pixelsize=16:antialias=true:autohint=true";
char *fontconfig = "Iosevka:pixelsize=16:antialias=true:autohint=true";

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

// Default shape of cursor
// 2: Block ("█")
// 4: Underline ("_")
// 6: Bar ("|")
unsigned int cursor_shape = 2;

// thickness of underline and bar cursors
unsigned int cursor_thickness = 2;

///////////////////////////////////////////
// color scheme
// the syntax highlighting is applied in one pass,
// so you can't have nested syntax highlighting
//

#include "plugins/color_schemes/gruvbox.h"

// disable coloring functions for the syntax schemes below
#undef function_color

#include "plugins/syntax/c.h"

const struct color_scheme color_schemes[] = {
	{".c", c_word_seperators, c_color_scheme, LEN(c_color_scheme)},
	{".h", c_word_seperators, c_color_scheme, LEN(c_color_scheme)},
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
static void change_to_file_buffer(const Arg* arg);
static void save_buffer(const Arg* arg);
static void toggle_selection(const Arg* arg);
static void move_cursor_to_offset(const Arg* arg);
static void move_cursor_to_end_of_buffer(const Arg* arg);
static void clipboard_copy(const Arg* arg);
static void clipboard_paste(const Arg* arg);
static void search(const Arg* arg);
static void undo(const Arg* arg);
static void redo(const Arg* arg);
static void search_next(const Arg* arg);
static void search_previous(const Arg* arg);
static void open_file_browser(const Arg* arg);
static void buffer_kill(const Arg* arg);

static void cursor_callback(struct window_buffer* buf, enum cursor_reason callback_reason);
static void buffer_content_callback(struct file_buffer* buffer, int offset, enum buffer_content_reason reason);
static void keep_cursor_col(struct window_buffer* buf, enum cursor_reason callback_reason);
static void move_selection(struct window_buffer* buf, enum cursor_reason callback_reason);
static int keypress_actions(KeySym keysym, int modkey);
static void string_insert_callback(const char* buf, int buflen);

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
	{ ControlMask,          XK_Tab,         change_to_file_buffer,        {.i = +1}         },
	{ ControlMask,          XK_m,           toggle_selection,             {0}       },
	{ ControlMask,          XK_g,           move_cursor_to_offset,        {0}       },
	{ TERMMOD,              XK_G,           move_cursor_to_end_of_buffer, {0}       },
	{ ControlMask,          XK_period,      open_file_browser,            {0}       },
	{ TERMMOD,              XK_D,           buffer_kill,                  {0}       },
	{ ControlMask,          XK_l,           window_split,   {.i = WINDOW_HORISONTAL}},
	{ ControlMask,          XK_k,           window_split,   {.i = WINDOW_VERTICAL}  },
	{ ControlMask,          XK_d,           window_delete,  {0}       },
	{ ControlMask,          XK_z,           undo,           {0}       },
	{ TERMMOD,              XK_Z,           redo,           {0}       },
	{ ControlMask,          XK_s,           save_buffer,    {0}       },
	{ ControlMask,          XK_f,           search,         {0}       },
	{ ControlMask,          XK_n,           search_next,    {0}       },
	{ TERMMOD,              XK_N,           search_previous,{0}       },
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
void(*startup_callback)(void) = NULL;
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
	if (focused_node->node2) {
		focused_node = focused_node->node2;
		focused_window = &focused_node->window;
	}
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
	xloadfonts(fontconfig, arg->f);
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
	if (focused_window->mode != WINDOW_BUFFER_FILE_BROWSER)
		buffer_move_on_line(focused_window, arg->i, CURSOR_RIGHT_LEFT_MOVEMENT);
}

void
cursor_move_y_relative(const Arg* arg)
{
	buffer_move_lines(focused_window, arg->i, 0);
	buffer_move_to_x(focused_window, focused_window->cursor_col, CURSOR_UP_DOWN_MOVEMENT);
}

void
change_to_file_buffer(const Arg* arg)
{
	if (arg->i < 0) {
		int prev_buffer_index = focused_window->buffer_index;
		focused_window->buffer_index += 1;
		get_file_buffer(focused_window);
		if (focused_window->buffer_index == prev_buffer_index)
			return;

		for (int i = 0; ; i++) {
			focused_window->buffer_index += prev_buffer_index - arg->i - i;
			get_file_buffer(focused_window);
			if (focused_window->buffer_index != prev_buffer_index)
				return;
		}
	}

	focused_window->buffer_index += arg->i;
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
	char* buf = buffer_get_selection(fb, &len);
	set_clipboard_copy(buf, len);

	buffer_move_cursor_to_selection_start(focused_window);
	fb->mode &= ~BUFFER_SELECTION_ON;
}

void
clipboard_paste(const Arg* arg)
{
	insert_clipboard_at_cursor();
}

void
search(const Arg* arg)
{
	get_file_buffer(focused_window)->mode &= ~BUFFER_SEARCH_BLOCKING_IDLE;
	get_file_buffer(focused_window)->mode |= BUFFER_SEARCH_BLOCKING;
	writef_to_status_bar("search: %s", get_file_buffer(focused_window)->search_term);
}

void
undo(const Arg* arg)
{
	buffer_undo(get_file_buffer(focused_window));
}

void
redo(const Arg* arg)
{
	buffer_redo(get_file_buffer(focused_window));
}

void
search_next(const Arg* arg)
{
	focused_window->cursor_offset =
		buffer_seek_string_wrap(focused_window, focused_window->cursor_offset+1,
								get_file_buffer(focused_window)->search_term);
}

void
search_previous(const Arg* arg)
{
	focused_window->cursor_offset =
		buffer_seek_string_wrap_backwards(focused_window, focused_window->cursor_offset-1,
										  get_file_buffer(focused_window)->search_term);
}

void
open_file_browser(const Arg* arg)
{
	int last_fb = focused_window->buffer_index;
	struct file_buffer* fb = get_file_buffer(focused_window);

	char* path = file_path_get_path(fb->file_path);
	*focused_window = window_buffer_new(new_file_buffer_entry(path));
	focused_window->cursor_col = last_fb;
	free(path);
}

void
buffer_kill(const Arg* arg)
{
	destroy_file_buffer_entry(focused_node, &root_node);
}

////////////////////////////////////////////////////////7
// Callbacks
//

void
keep_cursor_col(struct window_buffer* buf, enum cursor_reason callback_reason)
{
	if (callback_reason == CURSOR_COMMAND_MOVEMENT || callback_reason == CURSOR_RIGHT_LEFT_MOVEMENT) {
		int y;
		buffer_offset_to_xy(buf, buf->cursor_offset, -1, &buf->cursor_col, &y);
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

	 printf("moved to: %d | reason: %d\n", buf->cursor_offset, callback_reason);
}

void
buffer_content_callback(struct file_buffer* buffer, int offset, enum buffer_content_reason reason)
{
	buffer_add_to_undo(buffer, offset, reason);
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
	case XK_Escape:
		fb->mode &= ~BUFFER_SEARCH_BLOCKING_MASK;
		fb->mode &= ~BUFFER_SEARCH_NON_BLOCKING;
		fb->mode &= ~BUFFER_SEARCH_NON_BLOCKING_BACKWARDS;
		fb->mode &= ~BUFFER_SEARCH_BLOCKING_BACKWARDS;
		writef_to_status_bar("");
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

	if (buf[0] >= 32 || buflen > 1) {
		delete_selection(fb);
		buffer_insert(fb, buf, buflen, focused_window->cursor_offset, 0);
		window_move_all_cursors_on_same_buf(&root_node, NULL, focused_window->buffer_index, focused_window->cursor_offset,
											buffer_move_offset_relative, buflen, CURSOR_COMMAND_MOVEMENT);
	} else {
		printf("unhandled control character %x\n", buf[0]);
	}
}
