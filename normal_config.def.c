#include "config.h"

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

const struct syntax_scheme syntax_schemes[] = {
	{".c", c_word_seperators, c_syntax, LEN(c_syntax)},
	{".h", c_word_seperators, c_syntax, LEN(c_syntax)},
	{0},
};

/////////////////////////////////////////
// Shortcuts
//

#include "plugins/shortcuts.h"
#include "plugins/default_shortcuts.h"

#define MODKEY Mod1Mask
#define CtrlShift (ControlMask|ShiftMask)

shortcuts = {
//    mask                  keysym          function        argument
	{ 0,                    XK_Right,       cursor_move_x_relative,       {.i = +1} },
	{ 0,                    XK_Left,        cursor_move_x_relative,       {.i = -1} },
	{ 0,                    XK_Down,        cursor_move_y_relative,       {.i = +1} },
	{ 0,                    XK_Up,          cursor_move_y_relative,       {.i = -1} },
	{ ControlMask,          XK_Right,       window_change,                {.i = MOVE_RIGHT} },
	{ ControlMask,          XK_Left,        window_change,                {.i = MOVE_LEFT}  },
	{ ControlMask,          XK_Down,        window_change,                {.i = MOVE_DOWN}  },
	{ ControlMask,          XK_Up,          window_change,                {.i = MOVE_UP}    },
	{ CtrlShift,            XK_Right,       window_resize,                {.i = MOVE_RIGHT} },
	{ CtrlShift,            XK_Left,        window_resize,                {.i = MOVE_LEFT}  },
	{ CtrlShift,            XK_Down,        window_resize,                {.i = MOVE_DOWN}  },
	{ CtrlShift,            XK_Up,          window_resize,                {.i = MOVE_UP}    },
	{ ControlMask,          XK_Tab,         swap_to_next_file_buffer,     {0}       },
	{ ControlMask,          XK_m,           toggle_selection,             {0}       },
	{ ControlMask,          XK_g,           move_cursor_to_offset,        {0}       },
	{ CtrlShift,            XK_G,           move_cursor_to_end_of_buffer, {0}       },
	{ ControlMask,          XK_period,      open_file_browser,            {0}       },
	{ CtrlShift,            XK_D,           buffer_kill,                  {0}       },
	{ ControlMask,          XK_l,           window_split,   {.i = WINDOW_HORISONTAL}},
	{ ControlMask,          XK_k,           window_split,   {.i = WINDOW_VERTICAL}  },
	{ ControlMask,          XK_d,           window_delete,  {0}       },
	{ ControlMask,          XK_z,           undo,           {0}       },
	{ CtrlShift,            XK_Z,           redo,           {0}       },
	{ ControlMask,          XK_s,           save_buffer,    {0}       },
	{ ControlMask,          XK_f,           search,         {0}       },
	{ CtrlShift,            XK_F,           search_keyword_in_buffers,{0},},
	{ ControlMask,          XK_space,       search_for_buffer,{0},    },
	{ ControlMask,          XK_n,           search_next,    {0}       },
	{ CtrlShift,            XK_N,           search_previous,{0}       },
	{ ControlMask,          XK_c,           clipboard_copy, {0}       },
	{ ControlMask,          XK_v,           clipboard_paste,{0}       },
	{ CtrlShift,            XK_Prior,       zoom,           {.f = +1} },
	{ CtrlShift,            XK_Next,        zoom,           {.f = -1} },
	{ CtrlShift,            XK_Home,        zoomreset,      {.f =  0} },
	{ CtrlShift,            XK_Num_Lock,    numlock,        {.i =  0} },
};

/////////////////////////////////////////////////
// callbacks
//

static void cursor_callback(struct window_buffer* buf, enum cursor_reason callback_reason);
static void keep_cursor_col(struct window_buffer* buf, enum cursor_reason callback_reason);
static void move_selection(struct window_buffer* buf, enum cursor_reason callback_reason);
static int keypress_actions(KeySym keysym, int modkey);
static void string_insert_callback(const char* buf, int buflen);
#include "plugins/default_status_bar.h"

void(*cursor_movement_callback)(struct window_buffer*, enum cursor_reason) = cursor_callback;
void(*buffer_contents_updated)(struct file_buffer*, int, enum buffer_content_reason) = buffer_add_to_undo;
int(*keypress_callback)(KeySym, int) = keypress_actions;
void(*string_input_callback)(const char*, int) = string_insert_callback;
void(*draw_callback)(void) = NULL;
void(*startup_callback)(void) = NULL;
void(*buffer_written_to_screen_callback)(struct window_buffer* buf, int offset_start, int offset_end,  int minx, int miny, int maxx, int maxy) = NULL;
char*(*new_line_draw)(struct window_buffer* buf, int y, int lines_left, int minx, int maxx, Glyph* attr) = NULL;
void(*buffer_written_to_file_callback)(struct file_buffer* fb) = NULL;
int(*write_status_bar)(struct window_buffer* buf, int minx, int maxx, int cx, int cy, char line[LINE_MAX_LEN], Glyph* g) = default_status_bar;

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

	//writef_to_status_bar("moved to: %d | reason: %d\n", buf->cursor_offset, callback_reason);
}

int
keypress_actions(KeySym keysym, int modkey)
{
	check_shortcuts(keysym, modkey);

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

		goto skip_delete_remove_selection;
	case XK_Delete:

		if (delete_selection(fb)) return 1;
skip_delete_remove_selection:

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
		buffer_move_lines(focused_window, (focused_node->maxy - focused_node->miny) / 2, 0);
		buffer_move_to_x(focused_window, focused_window->cursor_col, CURSOR_UP_DOWN_MOVEMENT);
		focused_window->y_scroll += (focused_node->maxy - focused_node->miny) / 2;
		return 1;
	case XK_Page_Up:
		buffer_move_lines(focused_window, -((focused_node->maxy - focused_node->miny) / 2), 0);
		buffer_move_to_x(focused_window, focused_window->cursor_col, CURSOR_UP_DOWN_MOVEMENT);
		focused_window->y_scroll -= (focused_node->maxy - focused_node->miny) / 2;
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
		writef_to_status_bar("unhandled control character 0x%x\n", buf[0]);
	}
}
