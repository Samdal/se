#include "config.h"
#include "extension.h"
#include <ctype.h>

#define MODKEY Mod1Mask

// TODO: search hilight no longer works because syntax is ran after drawing search highlight

////////////////////////////////////////
// apperance
//

// font: see http://freedesktop.org/software/fontconfig/fontconfig-user.html
char *fontconfig = "Liberation Mono:pixelsize=21:antialias=true:autohint=true";
//char *fontconfig = "Iosevka:pixelsize=20:antialias=true:autohint=true";

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
unsigned int default_indent_len = 0; // 0 means tab

// Default shape of cursor
// 2: block ("█")
// 4: underline ("_")
// 6: bar ("|")
// TODO: half_block
// TODO: enums
unsigned int cursor_shape = 2;

// thickness of underline and bar cursors
unsigned int cursor_thickness = 2;

///////////////////////////////////////////
// custom window modes
//

enum window_modes {
		WB_MODES_START = WB_MODES_DEFAULT_END,
		#include "extensions/window_modes/choose_one_of_selection.enums"
		WB_MODES_END
};

///////////////////////////////////////////
// color scheme
// the syntax highlighting is applied in one pass,
// so you can't have nested syntax highlighting
//

#include "extensions/syntax/syntax.h"

#include "extensions/syntax/schemes/gruvbox.h"

// disable coloring functions for the syntax schemes below
#undef function_color
//#define function_color normal_color

#include "extensions/syntax/gd.h"
#include "extensions/syntax/c.h"

const struct syntax_scheme syntax[] = {
		{".c", c_word_seperators, c_syntax, LEN(c_syntax), c_indent, LEN(c_indent)},
		{".h", c_word_seperators, c_syntax, LEN(c_syntax), c_indent, LEN(c_indent)},
		{".gd", gd_word_seperators, gd_syntax, LEN(gd_syntax), gd_indent, LEN(gd_indent)},

		{0},
};

const struct syntax_scheme* syntax_schemes = syntax;

/////////////////////////////////////////
// Shortcuts
//

enum vim_modes {
		VIM_NORMAL,
		VIM_INSERT,
		VIM_REPLACE,
		VIM_SINGULAR_REPLACE,
		VIM_VISUAL,
		VIM_VISUAL_LINE,
		VIM_VISUAL_BLOCK,
};

static int vim_mode = VIM_NORMAL;

enum inside_around_delimiters {
		VIM_INSIDE_ROUND_BRACKETS,
		VIM_INSIDE_SQUARE_BRACKETS,
		VIM_INSIDE_CURLY_BRACKETS,
		VIM_INSIDE_ANGLE_BRACKET,
		VIM_INSIDE_DOUBLE_QUOTES,
		VIM_INSIDE_SINGLE_QUOTES,
		VIM_INSIDE_BACK_QUOTES,
		VIM_INSIDE_PARAGRAPH,

		VIM_INSIDE_COUNT,
		VIM_AROUND_START = VIM_INSIDE_COUNT,

		VIM_AROUND_ROUND_BRACKETS = VIM_AROUND_START,
		VIM_AROUND_SQUARE_BRACKETS,
		VIM_AROUND_CURLY_BRACKETS,
		VIM_AROUND_ANGLE_BRACKET,
		VIM_AROUND_DOUBLE_QUOTES,
		VIM_AROUND_SINGLE_QUOTES,
		VIM_AROUND_BACK_QUOTES,
		VIM_AROUND_PARAGRAPH,

		VIM_AROUND_END = VIM_AROUND_PARAGRAPH,
};

_Static_assert(VIM_AROUND_END - VIM_AROUND_START + 1 == VIM_INSIDE_COUNT, "Bracket count does not match");


const struct delimiter vim_delimiters[VIM_INSIDE_COUNT] = {
		{"(", ")"},
		{"[", "]"},
		{"{", "}"},
		{"<", ">"},
		{"\"", "\""},
		{"'", "'"},
		{"`", "`"},
		{"\n\n", "\n\n"}, // TODO: this works poorly
};

struct delimiter ignore_delimiters[] = {

		// C comments
		{"//", "\n"},
		{"/*", "*/"},

		// Python multiline strings
		{"\"\"\"", "\"\"\""},

		// Strings
		{"\"", "\""},
		{"'", "''"},
		{"'", "''"},
		{"`", "`"},

		{0},
};

// TODO: create a "delete in comments" function that
// selects around the current ignore delimiter

enum misc_delimiters {
		VIM_MISC_START = VIM_AROUND_END + 1,

		VIM_CURRENT_WORD = VIM_MISC_START,
		VIM_CURRENT_LINE,
		//VIM_CURRENT_LINE_INSIDE,
		VIM_CURRENT_WORD_AND_SURROUNDING_WHITESPACE,
		VIM_NEXT_QUOUTE,

		VIM_PREV_WORD_START,
		VIM_TO_START_OF_WORD,
		VIM_TO_END_OF_WORD,

		VIM_PREV_STRING_START,
		VIM_TO_START_OF_STRING,
		VIM_TO_END_OF_STRING,

		VIM_TO_START_OF_LINE,
		VIM_TO_END_OF_INDENT,
		VIM_TO_END_OF_LINE,
		VIM_TO_START_OF_FILE,
		VIM_TO_END_OF_FILE,

		VIM_SAME_INDENT_PLUS,

		VIM_CURRENT_SELECTION,

		VIM_MISC_COUNT,
};


const char* vim_default_word_seperators  = "., '\n\t*+-/%!~<>=(){}[]\"^&|\\`´?:;";
const char* vim_override_word_seperators = "., '\n\t*+-/%!~<>=(){}[]\"^&|\\`´?:;_";

struct chained_keybind {
		unsigned int mod;
		KeySym keysym;
		//  0 return value means it will keep this command level
		// +1 return value means it will move into this commands next_keybinds
		// +2 return value means it will continue looking another command
		// -1 return value means it will reset the chained keybind
		// -2 return value means it will reset the chained keybind (special case, e.g not reset state)
		int(*func)(int custom_mode);
		int custom_mode;
		const char* description;
		struct chained_keybind* next_keybinds;
		int keybind_count;
};

#define CHAINED_KEYBIND_LEN 1024
struct keypress_logg {
		KeySym ksym;
		unsigned int modkey;
		int len;
		char* buf;
};
struct keypress_logg* previous_chain;
struct keypress_logg current_chain[CHAINED_KEYBIND_LEN] = {0};
int previous_chain_len;
int chain_len;

int last_used_command_index;

static void
vim_copy_log(struct keypress_logg** dest, int dest_len, const struct keypress_logg* src, int src_len)
{
		*dest = xrealloc(*dest, src_len * sizeof(struct keypress_logg));
		if (dest_len == 0)
				(*dest)->buf = NULL;

		int total_buf_len = 0;
		for (int i = 0; i < src_len; i++)
				total_buf_len += src[i].len;
		char* chars = xrealloc((*dest)->buf, total_buf_len);

		for (int i = 0; i < src_len; i++) {
				struct keypress_logg* t = (*dest) + i;
				struct keypress_logg s = src[i];
				t->buf = chars;
				memcpy(t->buf, s.buf, s.len);
				t->ksym = s.ksym;
				t->modkey = s.modkey;
				t->len = s.len;
				chars += s.len;
		}
}

static int
vim_chain_parse_count_raw()
{
		int last_ksym_was_num = 0;
		int sum = 0;
		int current_num = 0;
		for (int i = last_used_command_index; i < chain_len; i++) {
				int current_val;
				switch(current_chain[i].ksym) {
				case XK_0: current_val = 0; break;
				case XK_1: current_val = 1; break;
				case XK_2: current_val = 2; break;
				case XK_3: current_val = 3; break;
				case XK_4: current_val = 4; break;
				case XK_5: current_val = 5; break;
				case XK_6: current_val = 6; break;
				case XK_7: current_val = 7; break;
				case XK_8: current_val = 8; break;
				case XK_9: current_val = 9; break;
				default:   current_val =-1; break;
				}
				if (current_val < 0) {
						if (last_ksym_was_num) {
								sum += current_num;
								current_num = 0;
								last_ksym_was_num = 0;
						}
						continue;
				}
				if (last_ksym_was_num)
						current_num *= 10;
				else
						last_ksym_was_num = 1;
				current_num += current_val;
		}
		if (last_ksym_was_num)
				sum += current_num;

		return sum;
}

static int
vim_chain_parse_count(int custom_mode)
{
		if (custom_mode == VIM_TO_END_OF_FILE || custom_mode == VIM_TO_START_OF_FILE)
				return 1;
		int sum = vim_chain_parse_count_raw();
		if (sum == 0)
				sum = 1;
		return sum;
}

static int
vim_get_delimiter(int delimiter_type, int offset, int* start, int* end)
{
		if (delimiter_type < 0)
				return 0;
		soft_assert(start, static int tmp; start = &tmp;);
		soft_assert(end, static int tmp; end = &tmp;);

		struct file_buffer* fb = get_fb(focused_window);
		LIMIT(offset, 0, fb->len-1);

		int count;

		if (delimiter_type < VIM_MISC_START) {
				int around = delimiter_type >= VIM_AROUND_START ? 1 : 0;
				if (around)
						delimiter_type -= VIM_AROUND_START;

				soft_assert(delimiter_type < VIM_INSIDE_COUNT, return 0;);
				if (!fb_get_delimiter(fb, offset, vim_delimiters[delimiter_type], ignore_delimiters, start, end)) {
						int next_offset = fb_seek_string(fb, offset, vim_delimiters[delimiter_type].start);
						if (next_offset < 0 || next_offset - offset >= 115)
								return 0;
						if (!fb_get_delimiter(fb, next_offset, vim_delimiters[delimiter_type], ignore_delimiters, start, end))
								return 0;
				}

				if (!around)
						*start += strlen(vim_delimiters[delimiter_type].start);
				else
						*end += strlen(vim_delimiters[delimiter_type].end);

				return 1;
		}


#ifdef SYNTAX_H_
		const struct syntax_scheme* cs = fb_get_syntax_scheme(fb);
		const char* word_seperators = cs ? cs->word_seperators : vim_default_word_seperators;
#else
		const char* word_seperators = vim_default_word_seperators;
#endif
		if (vim_override_word_seperators)
				word_seperators = vim_override_word_seperators;

		switch(delimiter_type) {
		case VIM_CURRENT_WORD:
				if (isspace(fb->contents[offset])) {
						int not_whitespace = fb_seek_not_whitespace_backwards(fb, offset);
						if (not_whitespace >= 0)
								not_whitespace += 1;
						int line_start = fb_seek_char_backwards(fb, offset, '\n');
						*start = MAX(not_whitespace, line_start);
						not_whitespace = fb_seek_not_whitespace(fb, offset);
						line_start = fb_seek_char(fb, offset, '\n');
						*end = MIN(not_whitespace, line_start);
						if (*end < 0)
								*end = MAX(not_whitespace, line_start);
				} else if (fb_is_on_a_word(fb, offset, word_seperators)) {
						*start = fb_seek_start_of_word_backwards(fb, offset, word_seperators);
						*end = fb_seek_word_end(fb, offset, word_seperators);
				} else {
						const char* word_seperators_no_newline = "., '\t*+-/%!~<>=(){}[]\"^&|\\`´?:;";
						*start = fb_seek_word_backwards(fb, offset, word_seperators_no_newline);
						if (*start >= 0)
								*start += 1;
						*end = fb_seek_word(fb, offset, word_seperators_no_newline);
				}
				if (*start < 0 || *end < 0)
						return 0;
				return 1;
		case VIM_CURRENT_LINE:
				*start = fb_seek_char_backwards(fb, offset, '\n');
				*end = fb_seek_char(fb, offset, '\n');
				if (*start < 0)
						*start = 0;
				if (*end < 0)
						*end = fb->len-1;
				if (*end > 0)
						*end += 1;
				return 1;
		case VIM_TO_END_OF_LINE:
				*end = fb_seek_char(fb, offset, '\n');
				if (*end < 0)
						return 0;
				*start = offset;
				return 1;
		case VIM_TO_START_OF_LINE:
				*start = fb_seek_char_backwards(fb, offset-1, '\n');
				if (*start < 0)
						*start = 0;
				*end = offset;
				return 1;
		case VIM_TO_END_OF_INDENT:
				*start = fb_seek_char_backwards(fb, offset, '\n');
				if (*start < 0)
						*start = 0;
				if (*start + 1 < fb->len-1 && isspace(fb->contents[*start + 1])) {
						int not_whitespace = fb_seek_not_whitespace(fb, *start + 1);
						int line_end = fb_seek_char(fb, *start + 1, '\n');
						if (line_end < not_whitespace)
								return 0;
						if (not_whitespace >= 0)
								*start = not_whitespace;
				}
				*end = offset;
				return 1;
		case VIM_TO_START_OF_FILE:
				count = vim_chain_parse_count_raw();
				if (count) {
						count--;
						int old_offset = focused_window->cursor_offset;

						focused_window->cursor_offset = 0;
						wb_move_lines(focused_window, count, 0);
						int new_offset = fb_seek_char_backwards(fb, focused_window->cursor_offset, '\n');
						if (new_offset < 0)
								new_offset = focused_window->cursor_offset;

						focused_window->cursor_offset = old_offset;

						if (new_offset < old_offset) {
								*start = new_offset;
								*end = old_offset;
						} else {
								*end = new_offset;
								*start = old_offset;
						}
						printf("%d %d\n", *start, *end);
				} else {
						*start = 0;
						*end = offset;
				}
				return 1;
		case VIM_TO_END_OF_FILE:
				count = vim_chain_parse_count_raw();
				if (count) {
						count--;
						int old_offset = focused_window->cursor_offset;
						focused_window->cursor_offset = 0;
						wb_move_lines(focused_window, count, 0);
						focused_window->cursor_offset = old_offset;
						int new_offset = fb_seek_char_backwards(fb, focused_window->cursor_offset, '\n');
						if (new_offset < old_offset) {
								*start = new_offset;
								*end = old_offset;
						} else {
								*end = new_offset;
								*start = old_offset;
						}
				} else {
						*start = offset;
						*end = fb->len-1;
				}
				return 1;
		case VIM_TO_START_OF_WORD:
				*end = fb_seek_word(fb, offset, word_seperators);
				*start = offset;
				if (*end < 0)
						return 0;
				return 1;
		case VIM_PREV_WORD_START:
				*start = fb_seek_start_of_word_backwards(fb, offset-1, word_seperators);
				*end = offset;
				if (*start < 0)
						return 0;
				return 1;
		case VIM_PREV_STRING_START:
				*end = offset;
				offset--;
				if (isspace(fb->contents[offset]))
						offset = fb_seek_not_whitespace_backwards(fb, offset);
				*start = fb_seek_whitespace_backwards(fb, offset-1);
				if (*start < 0)
						return 0;
				*start += 1;
				return 1;
		case VIM_TO_END_OF_WORD:
				*end = fb_seek_word_end(fb, offset, word_seperators);
				*start = offset;
				if (*end < 0)
						return 0;
				return 1;
		case VIM_TO_START_OF_STRING:
				*start = offset;
				if (!isspace(fb->contents[offset]))
						offset = fb_seek_whitespace(fb, offset);
				*end = fb_seek_not_whitespace(fb, offset);
				if (*end < 0)
						return 0;
				return 1;
		case VIM_TO_END_OF_STRING:
				*start = offset;
				if (isspace(fb->contents[offset]))
						offset = fb_seek_not_whitespace(fb, offset);
				*end = fb_seek_whitespace(fb, offset);
				if (*end < 0)
						return 0;
				return 1;
		case VIM_CURRENT_SELECTION:
				if (fb_is_selection_start_top_left(fb)) {
						*start = fb->s1o;
						*end = fb->s2o + 1;
				} else {
						*start = fb->s2o;
						*end = fb->s1o + 1;
				}
				return 1;
		default:
				return 0;
		}
}

static int
vim_change_mode(int custom_mode)
{
		int previous_mode = vim_mode;

		if (custom_mode == VIM_NORMAL) {
				if (vim_mode == VIM_INSERT)
						wb_move_on_line(focused_window, -1, CURSOR_COMMAND_MOVEMENT);
				struct file_buffer* fb = get_fb(focused_window);
				cursor_shape = 2;
				fb->mode &= ~FB_SELECT_MASK;
				fb->mode &= ~FB_SEARCH_BLOCKING_MASK;
				fb->mode &= ~FB_SEARCH_NON_BLOCKING;
				fb->mode &= ~FB_SEARCH_NON_BLOCKING_BACKWARDS;
				fb->mode &= ~FB_SEARCH_BLOCKING_BACKWARDS;
				writef_to_status_bar("Escape");
		} else if (custom_mode == VIM_INSERT) {
				cursor_shape = 6;
				writef_to_status_bar("-- INSERT --");
		} else if (custom_mode == VIM_REPLACE) {
				cursor_shape = 4;
		} else if (custom_mode == VIM_VISUAL || VIM_VISUAL_LINE) {
				struct file_buffer* fb = get_fb(focused_window);
				fb->mode |= FB_SELECTION_ON;
				if (custom_mode == VIM_VISUAL) {
						fb->s1o = fb->s2o = focused_window->cursor_offset;
						writef_to_status_bar("-- VISUAL --");
				} else if (custom_mode == VIM_VISUAL_LINE) {
						fb->s1o = fb_seek_char(fb, focused_window->cursor_offset, '\n');
						fb->s2o = fb_seek_char_backwards(fb, focused_window->cursor_offset, '\n');
						fb->mode |= FB_LINE_SELECT;
						writef_to_status_bar("-- VISUAL LINE --");

						custom_mode = VIM_VISUAL;
				}
		}
		vim_mode = custom_mode;
		if (custom_mode == VIM_VISUAL || (vim_mode == previous_mode))
				return -2;
		return -1;
}

static int
vim_exit(int custom_mode) {
		exit(0);
}

static int
vim_move_on_line(int custom_mode)
{
		if (focused_window->mode != WB_FILE_BROWSER)
				wb_move_on_line(focused_window, custom_mode * vim_chain_parse_count(0), CURSOR_RIGHT_LEFT_MOVEMENT);
		return -2;
}

static int
vim_move_lines(int custom_mode)
{
		wb_move_lines(focused_window, custom_mode * vim_chain_parse_count(0), 0);
		wb_move_to_x(focused_window, focused_window->cursor_col, CURSOR_UP_DOWN_MOVEMENT);
		return -2;
}

static int
vim_change_window(int custom_mode)
{
		int count = vim_chain_parse_count(0);
		while(count--) {
				focused_node = window_switch_to_window(focused_node, custom_mode);
				focused_window = &focused_node->wb;
		}
		return -2;
}

static int
vim_resize_window(int custom_mode)
{
		float amount = (custom_mode == MOVE_RIGHT || custom_mode == MOVE_LEFT) ? 0.1f : 0.05f;
		window_node_resize(focused_node, custom_mode, amount * vim_chain_parse_count(0));
		return -2;
}

static int
vim_swap_to_next_file_buffer(int custom_mode)
{
		focused_window->fb_index += vim_chain_parse_count(0);
		return -2;
}

static int
vim_split_window(int custom_mode)
{
		int count = vim_chain_parse_count(0);
		while(count--) {
				window_node_split(focused_node, 0.5, custom_mode);
#if 1
				if (focused_node->node2) {
						focused_node = focused_node->node2;
						focused_window = &focused_node->wb;
				}
#else
				if (focused_node->node1) {
						focused_node = focused_node->node1;
						focused_window = &focused_node->wb;
				}
#endif
		}
		return -2;
}

static int
vim_delete_window(int custom_mode)
{
		int count = vim_chain_parse_count(0);
		while(count--) {
				struct window_split_node* new_node = window_node_delete(focused_node);
				while (new_node->mode != WINDOW_SINGULAR)
						new_node = new_node->node1;
				focused_node = new_node;
				focused_window = &focused_node->wb;
		}
		return -2;
}

static int
vim_kill_buffer(int custom_mode)
{
		int count = vim_chain_parse_count(0);
		while(count--)
				destroy_fb_entry(focused_node, &root_node);
		return -2;
}

static int
vim_save_buffer(int custom_mode)
{
		fb_write_to_filepath(get_fb(focused_window));
		return -2;
}

static int
vim_zoomabs(int custom_mode)
{
		xunloadfonts();
		xloadfonts(fontconfig, custom_mode);
		cresize(0, 0);
		xhints();
		return -2;
}

static int
vim_zoom(int custom_mode)
{
		vim_zoomabs(usedfontsize + custom_mode);
		return -2;
}

static int
vim_zoomreset(int custom_mode)
{
		if (defaultfontsize > 0)
				vim_zoomabs(defaultfontsize);
		return -2;
}

static int
vim_zero(int custom_mode)
{
		if (vim_chain_parse_count_raw())
				return 0;
		return 2;
}

static int
vim_yank(int custom_mode)
{
		struct file_buffer* fb = get_fb(focused_window);
		char* buf;
		int len;
		if (custom_mode == VIM_CURRENT_SELECTION || fb->mode & FB_SELECTION_ON) {
				buf = fb_get_selection(fb, &len);
				wb_move_cursor_to_selection_start(focused_window);
				fb->mode &= ~FB_SELECT_MASK;
				vim_change_mode(VIM_NORMAL);
		} else {
				int start, end;
				if (!vim_get_delimiter(custom_mode, focused_window->cursor_offset, &start, &end)) {
						writef_to_status_bar("unable to yank area");
						return -1;
				}
				buf = fb_get_string_between_offsets(fb, start, end);
				len = end - start;
		}
		set_clipboard_copy(buf, len);
		writef_to_status_bar("area yanked");
		return -2;
}

static int paste_behind = 0;

static int
vim_paste(int custom_mode)
{
		custom_mode = paste_behind;
		execute_clipbaord_event();
		return -1;
}

static int
vim_auto_indent_current_line(int custom_mode)
{
		struct file_buffer* fb = get_fb(focused_window);
		focused_window->cursor_offset = fb_seek_char_backwards(fb, focused_window->cursor_offset, '\n');
		int indent_size = fb_auto_indent(fb, focused_window->cursor_offset);

		focused_window->cursor_offset = MIN(fb_seek_not_whitespace(fb, focused_window->cursor_offset),
											fb_seek_char(fb, focused_window->cursor_offset, '\n'));

		window_node_move_all_cursors_on_same_fb(&root_node, focused_node, focused_window->fb_index, focused_window->cursor_offset,
												wb_move_offset_relative, indent_size, CURSOR_COMMAND_MOVEMENT);
		return -2;
}

static int
vim_open_file_browser(int custom_mode)
{
		int last_fb = focused_window->fb_index;
		struct file_buffer* fb = get_fb(focused_window);

		char* path = file_path_get_path(fb->file_path);
		*focused_window = wb_new(fb_new_entry(path));
		focused_window->cursor_col = last_fb;
		free(path);
		return -1;
}

int
vim_search_for_buffer(int custom_mode)
{
		if (focused_window->mode != WB_NORMAL)
				return -2;
		*focused_node->search = 0;
		focused_node->selected = 0;
		focused_window->mode = WB_SEARCH_FOR_BUFFERS;
		return -2;
}

int
vim_search_keyword_in_buffers(int custom_mode)
{
		if (focused_window->mode != WB_NORMAL)
				return -2;
		*focused_node->search = 0;
		focused_node->selected = 0;
		focused_window->mode = WB_SEARCH_KEYWORD_ALL_BUFFERS;
		return -2;
}

static int
vim_delete(int custom_mode)
{
		int count = vim_chain_parse_count(custom_mode);
		while (count--) {
				int start, end;
				if (!vim_get_delimiter(custom_mode, focused_window->cursor_offset, &start, &end)) {
						writef_to_status_bar("unable to find section to delete");
						return -1;
				}
				if (start - end == 0)
						return -2;
				struct file_buffer* fb = get_fb(focused_window);
				fb_remove(fb, start, end - start, 1, 1);
				call_extension(fb_contents_updated, fb, focused_window->cursor_offset, FB_CONTENT_BIG_CHANGE);
				wb_move_to_offset(focused_window, start, CURSOR_COMMAND_MOVEMENT);
				window_node_move_all_cursors_on_same_fb(&root_node, focused_node, focused_window->fb_index, end,
														wb_move_offset_relative, end - start, CURSOR_COMMAND_MOVEMENT);
				if (custom_mode == VIM_CURRENT_SELECTION) {
						vim_change_mode(VIM_NORMAL);
						break;
				}
		}
		return -1;
}

static int
vim_change(int custom_mode)
{
		int count = vim_chain_parse_count(custom_mode);
		while (count--) {
				int start, end;
				if (!vim_get_delimiter(custom_mode, focused_window->cursor_offset, &start, &end)) {
						writef_to_status_bar("unable to find section to change");
						return -1;
				}
				if (start - end == 0)
						return -2;
				struct file_buffer* fb = get_fb(focused_window);
				fb_remove(fb, start, end - start, 1, 1);
				call_extension(fb_contents_updated, fb, focused_window->cursor_offset, FB_CONTENT_BIG_CHANGE);
				wb_move_to_offset(focused_window, start, CURSOR_COMMAND_MOVEMENT);
				window_node_move_all_cursors_on_same_fb(&root_node, focused_node, focused_window->fb_index, end,
														wb_move_offset_relative, end - start, CURSOR_COMMAND_MOVEMENT);
				if (custom_mode == VIM_CURRENT_SELECTION)
						break;

		}
		vim_change_mode(VIM_INSERT);
		return -1;
}

static int
vim_visual_delimit(int custom_mode)
{
		int start, end;
		if (!vim_get_delimiter(custom_mode, focused_window->cursor_offset, &start, &end)) {
				writef_to_status_bar("unable to find section to highlight");
				return -1;
		}

		end--;
		struct file_buffer* fb = get_fb(focused_window);
		fb->mode |= FB_SELECTION_ON;
		fb->mode &= ~FB_SELECT_MODE_MASK;
		writef_to_status_bar("-- VISUAL --");

		if (end == focused_window->cursor_offset) {
				fb->s1o = end;
				fb->s2o = start;

				focused_window->cursor_offset = start;
				return -1;
		}

		fb->s1o = start;
		fb->s2o = end;

		focused_window->cursor_offset = end;
		return -2;
}

static int vim_enter(int custom_mode) {return 1;}

static int
vim_append(int custom_mode)
{
		vim_change_mode(VIM_INSERT);
		wb_move_on_line(focused_window, 1, CURSOR_COMMAND_MOVEMENT);
		return -1;
}

static int
vim_insert_start(int custom_mode)
{
		vim_change_mode(VIM_INSERT);
		int start, tmp;
		if (vim_get_delimiter(VIM_TO_END_OF_INDENT, focused_window->cursor_offset, &start, &tmp))
				wb_move_to_offset(focused_window, start, CURSOR_COMMAND_MOVEMENT);
		return -1;
}

static int
vim_insert_end(int custom_mode)
{
		vim_change_mode(VIM_INSERT);
		int end, tmp;
		if (vim_get_delimiter(VIM_TO_END_OF_LINE, focused_window->cursor_offset, &tmp, &end))
				wb_move_to_offset(focused_window, end, CURSOR_COMMAND_MOVEMENT);
		return -1;
}

static int
vim_insert_new_line(int custom_mode)
{
		vim_change_mode(VIM_INSERT);
		struct file_buffer* fb = get_fb(focused_window);

		int offset = focused_window->cursor_offset;
		offset = fb_seek_char(fb, offset, '\n');
		if (offset < 0)
				offset = fb->len-1;
		focused_window->cursor_offset = offset;
		wb_move_offset_relative(focused_window, 1, CURSOR_COMMAND_MOVEMENT);

		fb_insert(fb, "\n", 1, offset, 0);
		window_node_move_all_cursors_on_same_fb(&root_node, focused_node, focused_window->fb_index, focused_window->cursor_offset,
												wb_move_offset_relative, 1, CURSOR_COMMAND_MOVEMENT);
		vim_auto_indent_current_line(0);

		return -1;
}

static int
vim_remove_newline_at_end(int custom_mode)
{
		int count = vim_chain_parse_count(0);
		struct file_buffer* fb = get_fb(focused_window);
		while (count--) {
				int offset = fb_seek_char(fb, focused_window->cursor_offset, '\n');
				fb_remove(fb, offset, 1, 1, 0);
				wb_move_to_offset(focused_window, offset, CURSOR_COMMAND_MOVEMENT);
				window_node_move_all_cursors_on_same_fb(&root_node, focused_node, focused_window->fb_index, offset,
														wb_move_offset_relative, -1, CURSOR_COMMAND_MOVEMENT);

				LIMIT(offset, 0, fb->len);
				if (isspace(fb->contents[offset])) {
						int start, end;
						int not_whitespace = fb_seek_not_whitespace_backwards(fb, offset);
						if (not_whitespace >= 0)
								not_whitespace += 1;
						int line_start = fb_seek_char_backwards(fb, offset, '\n');
						start = MAX(not_whitespace, line_start);

						not_whitespace = fb_seek_not_whitespace(fb, offset);
						line_start = fb_seek_char(fb, offset, '\n');
						end = MIN(not_whitespace, line_start);
						if (end < 0)
								end = MAX(not_whitespace, line_start);

						if (end < 0 || start < 0)
								return -1;

						if (fb->contents[offset] != '\n') {
								fb_remove(fb, start, end - start, 1, 0);
								fb_insert(fb, " ", 1, start, 1);
								start++;
								wb_move_to_offset(focused_window, start, CURSOR_COMMAND_MOVEMENT);
								window_node_move_all_cursors_on_same_fb(&root_node, focused_node, focused_window->fb_index, end,
																		wb_move_offset_relative, end - start, CURSOR_COMMAND_MOVEMENT);
						}
				} else {
						fb_insert(fb, " ", 1, offset, 1);
				}
		}
		return -1;
}

static int
vim_undo(int custom_mode)
{
		int count = vim_chain_parse_count(0);
		while (count--)
				fb_undo(get_fb(focused_window));
		return -2;
}

static int
vim_redo(int custom_mode)
{
		int count = vim_chain_parse_count(0);
		while (count--)
				fb_redo(get_fb(focused_window));
		return -2;
}

static int search_reversed = 0;
static int vim_next(int custom_mode);
static int vim_prev(int custom_mode);

int
vim_next(int custom_mode)
{
		if (!custom_mode && search_reversed)
				return vim_prev(1);

		get_fb(focused_window)->mode |= FB_SEARCH_BLOCKING_IDLE;
		int new_offset = wb_seek_string_wrap(focused_window, focused_window->cursor_offset+1,
											 get_fb(focused_window)->search_term);
		if (new_offset < 0) {
				writef_to_status_bar("no results for \"%s\"", get_fb(focused_window)->search_term);
				return -1;
		} else if (focused_window->cursor_offset > new_offset) {
				writef_to_status_bar("search wrapped");
		}
		focused_window->cursor_offset = new_offset;
		return -2;
}

int
vim_prev(int custom_mode)
{
		if (!custom_mode && search_reversed)
				return vim_next(1);

		get_fb(focused_window)->mode |= FB_SEARCH_BLOCKING_IDLE;
		int new_offset = wb_seek_string_wrap_backwards(focused_window, focused_window->cursor_offset-1,
													   get_fb(focused_window)->search_term);
		if (new_offset < 0) {
				writef_to_status_bar("no results for \"%s\"", get_fb(focused_window)->search_term);
				return -1;
		} else if (focused_window->cursor_offset < new_offset) {
				writef_to_status_bar("search wrapped");
		}
		focused_window->cursor_offset = new_offset;
		return -2;
}

static int
vim_search(int custom_mode)
{
		search_reversed = custom_mode;
		get_fb(focused_window)->mode &= ~FB_SEARCH_BLOCKING_IDLE;
		get_fb(focused_window)->mode |= FB_SEARCH_BLOCKING;
		writef_to_status_bar("search: %s", get_fb(focused_window)->search_term);
		return -2;
}

static int
vim_home(int custom_mode)
{
		struct file_buffer* fb = get_fb(focused_window);
		int new_offset = fb_seek_char_backwards(fb, focused_window->cursor_offset, '\n');
		if (new_offset < 0)
				new_offset = 0;
		wb_move_to_offset(focused_window, new_offset, CURSOR_COMMAND_MOVEMENT);
		return -2;
}

static int
vim_end(int custom_mode)
{
		struct file_buffer* fb = get_fb(focused_window);
		int new_offset = fb_seek_char(fb, focused_window->cursor_offset, '\n');
		if (new_offset < 0)
				new_offset = fb->len-1;
		wb_move_to_offset(focused_window, new_offset, CURSOR_COMMAND_MOVEMENT);
		return -2;
}

static int
vim_move_down_one_screen_devided_by(int custom_mode)
{
		soft_assert(custom_mode, return -2;);
		int count = vim_chain_parse_count(0);

		wb_move_lines(focused_window, ((focused_node->maxy - focused_node->miny) / custom_mode) * count, 0);
		wb_move_to_x(focused_window, focused_window->cursor_col, CURSOR_UP_DOWN_MOVEMENT);
		focused_window->y_scroll += ((focused_node->maxy - focused_node->miny) / custom_mode) * count;
		return -2;
}

static int
vim_move_scroll(int custom_mode)
{
		int count = vim_chain_parse_count(0);
		focused_window->y_scroll += custom_mode * count;
		return -2;
}

static int
vim_center_scroll(int custom_mode)
{
		struct file_buffer* fb = get_fb(focused_window);
		int tmp, y;
		fb_offset_to_xy(fb, focused_window->cursor_offset, focused_node->maxx, focused_window->y_scroll, &tmp, &y, &tmp);
		focused_window->y_scroll += y - ((focused_node->maxy - focused_node->miny) / 2);
		return -2;
}

// TODO: make (x/delete) remove something to the on it when it's next to new line
// instead of doing nothing
static int
vim_remove_one_char(int custom_mode)
{
		struct window_split_node* excluded = (custom_mode == 0) ? focused_node : NULL;

		int old_offset = focused_window->cursor_offset;
		if (custom_mode)
				wb_move_on_line(focused_window, custom_mode, CURSOR_DO_NOT_CALLBACK);
		int offset = focused_window->cursor_offset;
		focused_window->cursor_offset = old_offset;

		int times = vim_chain_parse_count(0);
		struct file_buffer* fb = get_fb(focused_window);
		while (times-- && fb->contents[offset] != '\n') {
				int len = fb_remove(fb, offset, 1, 0, 0);
				window_node_move_all_cursors_on_same_fb(&root_node, excluded, focused_window->fb_index, offset,
														wb_move_offset_relative, -len, CURSOR_COMMAND_MOVEMENT);
		}
		return -1;
}

static int
vim_backspace(int custom_mode)
{
		struct file_buffer* fb = get_fb(focused_window);
		int offset = focused_window->cursor_offset-1;
		if (offset <= 0 || offset >= fb->len)
				return -1;
		if (fb->contents[offset] == '\n') {
				fb_remove(fb, offset, 1, 1, 0);
				window_node_move_all_cursors_on_same_fb(&root_node, NULL, focused_window->fb_index, offset,
														wb_move_offset_relative, -1, CURSOR_COMMAND_MOVEMENT);
		} else {
				vim_remove_one_char(-1);
		}
		return -1;
}

static int
vim_insert_return(int custom_mode)
{
		struct file_buffer* fb = get_fb(focused_window);
		int offset = focused_window->cursor_offset;

		fb_insert(fb, "\n", 1, offset, 0);
		int indent_size = fb_auto_indent(fb, offset);
		window_node_move_all_cursors_on_same_fb(&root_node, NULL, focused_window->fb_index, offset,
												wb_move_offset_relative, indent_size + 1, 0);
		indent_size = fb_auto_indent(fb, offset + 1 + indent_size);
		window_node_move_all_cursors_on_same_fb(&root_node, NULL, focused_window->fb_index, offset,
												wb_move_offset_relative, indent_size, CURSOR_COMMAND_MOVEMENT);
		window_node_move_all_yscrolls(&root_node, focused_node, focused_window->fb_index, focused_window->cursor_offset, 1);
		return -1;
}

static int
vim_insert_tab(int custom_mode)
{
		int offset = focused_window->cursor_offset;
		struct file_buffer* fb = get_fb(focused_window);

		fb_insert(fb, "\t", 1, offset, 0);
		window_node_move_all_cursors_on_same_fb(&root_node, NULL, focused_window->fb_index, offset,
												wb_move_on_line, 1, CURSOR_COMMAND_MOVEMENT);
		return -1;
}

static int
vim_move(int custom_mode)
{
		int count = vim_chain_parse_count(custom_mode);
		struct file_buffer* fb = get_fb(focused_window);
		while(count--) {
				int offset = focused_window->cursor_offset;
				if (custom_mode != VIM_PREV_STRING_START && custom_mode != VIM_PREV_WORD_START && offset + 1 < fb->len)
						offset++;
				int start, end;
				if(!vim_get_delimiter(custom_mode, offset, &start, &end))
						return -1;
				int other;
				if (abs(offset - start) > abs(offset - end))
						other = start;
				else
						other = end;
				wb_move_to_offset(focused_window, other, CURSOR_COMMAND_MOVEMENT);
		}
		return -2;
}

static int vim_repeat_last_command(int custom_mode);

#define numbers()								\
		{0, XK_0, vim_zero},					\
	{0, XK_1},									\
	{0, XK_2},									\
	{0, XK_3},									\
	{0, XK_4},									\
	{0, XK_5},									\
	{0, XK_6},									\
	{0, XK_7},									\
	{0, XK_8},									\
	{0, XK_9}
// count = 10

#define glue(_start, _end) _start##_end

#define VIM_DELIMITER_REGIONS(_func, _inside_around)					\
		{XK_ANY_MOD, XK_w,            _func, VIM_CURRENT_WORD},			\
	{XK_ANY_MOD, XK_parenleft,    _func, glue(VIM_, _inside_around##_ROUND_BRACKETS)}, \
	{XK_ANY_MOD, XK_parenright,   _func, glue(VIM_, _inside_around##_ROUND_BRACKETS)}, \
	{XK_ANY_MOD, XK_b,            _func, glue(VIM_, _inside_around##_ROUND_BRACKETS)}, \
	{XK_ANY_MOD, XK_bracketleft,  _func, glue(VIM_, _inside_around##_SQUARE_BRACKETS)}, \
	{XK_ANY_MOD, XK_bracketright, _func, glue(VIM_, _inside_around##_SQUARE_BRACKETS)}, \
	{XK_ANY_MOD, XK_braceleft,    _func, glue(VIM_, _inside_around##_CURLY_BRACKETS)}, \
	{XK_ANY_MOD, XK_braceright,   _func, glue(VIM_, _inside_around##_CURLY_BRACKETS)}, \
	{XK_ANY_MOD, XK_B,            _func, glue(VIM_, _inside_around##_CURLY_BRACKETS)}, \
	{XK_ANY_MOD, XK_less,         _func, glue(VIM_, _inside_around##_ANGLE_BRACKET)}, \
	{XK_ANY_MOD, XK_greater,      _func, glue(VIM_, _inside_around##_ANGLE_BRACKET)}, \
	{XK_ANY_MOD, XK_t,            _func, glue(VIM_, _inside_around##_ANGLE_BRACKET)}, \
	{XK_ANY_MOD, XK_quotedbl,     _func, glue(VIM_, _inside_around##_DOUBLE_QUOTES)}, \
	{XK_ANY_MOD, XK_apostrophe,   _func, glue(VIM_, _inside_around##_SINGLE_QUOTES)}, \
	{XK_ANY_MOD, XK_p,            _func, glue(VIM_, _inside_around##_PARAGRAPH)}, \
	{XK_ANY_MOD, XK_grave,        _func, glue(VIM_, _inside_around##_BACK_QUOTES)}
// count = 16

#define CHAIN_COUNT(_x) (_x)

struct chained_keybind normal_mode_keybinds[]  = {
		numbers(),
		// se specific keybinds, all followed by SPC, inspired by Emacs evil-mode
		{XK_ANY_MOD, XK_space, vim_enter, 0, "se commands [...]", (struct chained_keybind[]) {
						{0, XK_h, vim_change_window, MOVE_LEFT},
						{0, XK_j, vim_change_window, MOVE_DOWN},
						{0, XK_k, vim_change_window, MOVE_UP},
						{0, XK_l, vim_change_window, MOVE_RIGHT},
						{XK_ANY_MOD, XK_h, vim_resize_window, MOVE_LEFT},
						{XK_ANY_MOD, XK_j, vim_resize_window, MOVE_DOWN},
						{XK_ANY_MOD, XK_k, vim_resize_window, MOVE_UP},
						{XK_ANY_MOD, XK_l, vim_resize_window, MOVE_RIGHT},
						{XK_ANY_MOD, XK_H, vim_resize_window, MOVE_LEFT},
						{XK_ANY_MOD, XK_J, vim_resize_window, MOVE_DOWN},
						{XK_ANY_MOD, XK_K, vim_resize_window, MOVE_UP},
						{XK_ANY_MOD, XK_L, vim_resize_window, MOVE_RIGHT},
						{ShiftMask, XK_W, vim_save_buffer},
						{ControlMask, XK_s, vim_save_buffer},
						{0, XK_s, vim_split_window, WINDOW_VERTICAL},
						{0, XK_v, vim_split_window, WINDOW_HORISONTAL},
						{0, XK_d, vim_delete_window},
						{0, XK_Tab, vim_swap_to_next_file_buffer},
						{XK_ANY_MOD, XK_D, vim_kill_buffer},
						{0, XK_b, vim_enter, 0, "buffer [...]", (struct chained_keybind[]) {
										{0, XK_k, vim_kill_buffer},
										{0, XK_s, vim_search_for_buffer},
										{0, XK_w, vim_save_buffer},
										{0, XK_space, vim_search_for_buffer},
										{XK_ANY_MOD, XK_slash, vim_search_keyword_in_buffers},
								}, CHAIN_COUNT(5),
						},
						{0, XK_space, vim_open_file_browser},
						{ControlMask, XK_space, vim_search_for_buffer},
						{0, XK_p, vim_search_for_buffer},
						{XK_ANY_MOD, XK_slash, vim_search_keyword_in_buffers},
						{XK_ANY_MOD, XK_plus, vim_zoom,  +1},
						{XK_ANY_MOD, XK_minus, vim_zoom, -1},
						{XK_ANY_MOD, XK_Home, vim_zoomreset},
						numbers(),
				}, CHAIN_COUNT(36),
		},

		// movement
		{0, XK_h, vim_move_on_line, -1},
		{0, XK_j, vim_move_lines,   +1},
		{0, XK_k, vim_move_lines,   -1},
		{0, XK_l, vim_move_on_line, +1},
		{0, XK_Left, vim_move_on_line, -1},
		{0, XK_Down, vim_move_lines,   +1},
		{0, XK_Up, vim_move_lines,   -1},
		{0, XK_Right, vim_move_on_line, +1},
		{0, XK_BackSpace, vim_move_on_line, -1},
		{0, XK_Home, vim_home},
		{0, XK_End,  vim_end},
		{0, XK_0, vim_move, VIM_TO_START_OF_LINE},
		{ControlMask, XK_Right, vim_move, VIM_TO_END_OF_STRING},
		{ControlMask, XK_Left, vim_move, VIM_PREV_STRING_START},
		{0, XK_w, vim_move, VIM_TO_START_OF_WORD},
		{ShiftMask, XK_W, vim_move, VIM_TO_START_OF_STRING},
		{0, XK_e, vim_move, VIM_TO_END_OF_WORD},
		{ShiftMask, XK_E, vim_move, VIM_TO_END_OF_STRING},
		{0, XK_b, vim_move, VIM_PREV_WORD_START},
		{ShiftMask, XK_B, vim_move, VIM_PREV_STRING_START},
		{XK_ANY_MOD, XK_dollar, vim_move, VIM_TO_END_OF_LINE},
		{XK_ANY_MOD, XK_G, vim_move, VIM_TO_END_OF_FILE},
		{0, XK_g, vim_enter, 0, "goto", (struct chained_keybind[]) {
						{0, XK_g, vim_move, VIM_TO_START_OF_FILE},
				}, CHAIN_COUNT(1),
		},

		// scroll
		{0, XK_Page_Down, vim_move_down_one_screen_devided_by, 1},
		{0, XK_Page_Up,   vim_move_down_one_screen_devided_by,  -1},
		{ControlMask, XK_d, vim_move_down_one_screen_devided_by, 2},
		{ControlMask, XK_u, vim_move_down_one_screen_devided_by,  -2},
		{ControlMask, XK_f, vim_move_down_one_screen_devided_by, 1},
		{ControlMask, XK_b, vim_move_down_one_screen_devided_by,  -1},
		{ControlMask, XK_e, vim_move_scroll, 1},
		{ControlMask, XK_y, vim_move_scroll,  -1},
		{0, XK_z, vim_enter, 0, NULL, (struct chained_keybind[]) {
						{0, XK_z, vim_center_scroll},
				}, CHAIN_COUNT(1),
		},

		// misc
		{XK_ANY_MOD, XK_Escape, vim_change_mode, VIM_NORMAL},
		{0, XK_Tab, vim_auto_indent_current_line},
		{0, XK_q, vim_exit},
		{0, XK_u, vim_undo},
		{0, XK_period, vim_repeat_last_command},
		{ControlMask, XK_r, vim_redo},
		{XK_ANY_MOD, XK_slash, vim_search},
		{XK_ANY_MOD, XK_question, vim_search, 1},
		{0, XK_n, vim_next},
		{ShiftMask, XK_N, vim_prev},
		// copy / yank
		{0, XK_p, vim_paste},
		{0, XK_y, vim_enter, 0, "yank", (struct chained_keybind[]) {
						{0, XK_y, vim_yank, VIM_CURRENT_LINE},
						{XK_ANY_MOD, XK_I, vim_yank, VIM_TO_END_OF_INDENT},
						{XK_ANY_MOD, XK_0, vim_yank, VIM_TO_START_OF_LINE},
						{XK_ANY_MOD, XK_A, vim_yank, VIM_TO_END_OF_LINE},
						{XK_ANY_MOD, XK_G, vim_yank, VIM_TO_END_OF_FILE},
						{XK_ANY_MOD, XK_w, vim_yank, VIM_TO_START_OF_WORD},
						{XK_ANY_MOD, XK_W, vim_yank, VIM_TO_START_OF_STRING},
						{XK_ANY_MOD, XK_e, vim_yank, VIM_TO_END_OF_WORD},
						{XK_ANY_MOD, XK_E, vim_yank, VIM_TO_END_OF_STRING},
						{XK_ANY_MOD, XK_b, vim_yank, VIM_PREV_WORD_START},
						{XK_ANY_MOD, XK_B, vim_yank, VIM_PREV_STRING_START},
						{XK_ANY_MOD, XK_dollar, vim_yank, VIM_TO_END_OF_LINE},
						{0, XK_g, vim_enter, 0, NULL, (struct chained_keybind[]) {
										{0, XK_g, vim_yank, VIM_TO_START_OF_FILE},
								}, CHAIN_COUNT(1),
						},
						{0, XK_i, vim_enter, 0, "inside", (struct chained_keybind[]) {
										VIM_DELIMITER_REGIONS(vim_yank, INSIDE)
								}, CHAIN_COUNT(16),
						},
						{0, XK_a, vim_enter, 0, "around", (struct chained_keybind[]) {
										VIM_DELIMITER_REGIONS(vim_yank, AROUND)
								}, CHAIN_COUNT(16),
						},
						numbers(),
				}, CHAIN_COUNT(25)
		},

		// insert
		{0, XK_i, vim_change_mode, VIM_INSERT},
		{XK_ANY_MOD, XK_I, vim_insert_start},
		{0, XK_a, vim_append},
		{XK_ANY_MOD, XK_A, vim_insert_end},
		{0, XK_o, vim_insert_new_line},

		// visual
		{0, XK_v, vim_change_mode, VIM_VISUAL},
		{ShiftMask, XK_V, vim_change_mode, VIM_VISUAL_LINE},
		//{ControlMask, XK_v, vim_change_mode, VIM_VISUAL},

		// deleting
		{XK_ANY_MOD, XK_J, vim_remove_newline_at_end},
		{0, XK_x, vim_remove_one_char},
		{XK_ANY_MOD, XK_X, vim_remove_one_char, -1},
		{0, XK_Delete, vim_remove_one_char, +1},
		{ControlMask, XK_BackSpace, vim_delete, VIM_PREV_STRING_START},
		{ControlMask, XK_Delete, vim_delete, VIM_TO_END_OF_STRING},
		{XK_ANY_MOD, XK_D, vim_delete, VIM_TO_END_OF_LINE},
		{0, XK_d, vim_enter, 0, "delete", (struct chained_keybind[]) {
						{0, XK_d, vim_delete, VIM_CURRENT_LINE},
						{0, XK_l, vim_remove_one_char},
						{0, XK_h, vim_remove_one_char, -1},
						{XK_ANY_MOD, XK_I, vim_delete, VIM_TO_END_OF_INDENT},
						{XK_ANY_MOD, XK_A, vim_delete, VIM_TO_END_OF_LINE},
						{XK_ANY_MOD, XK_G, vim_delete, VIM_TO_END_OF_FILE},
						{XK_ANY_MOD, XK_w, vim_delete, VIM_TO_START_OF_WORD},
						{XK_ANY_MOD, XK_W, vim_delete, VIM_TO_START_OF_STRING},
						{XK_ANY_MOD, XK_e, vim_delete, VIM_TO_END_OF_WORD},
						{XK_ANY_MOD, XK_E, vim_delete, VIM_TO_END_OF_STRING},
						{XK_ANY_MOD, XK_b, vim_delete, VIM_PREV_WORD_START},
						{XK_ANY_MOD, XK_B, vim_delete, VIM_PREV_STRING_START},
						{XK_ANY_MOD, XK_dollar, vim_delete, VIM_TO_END_OF_LINE},
						{0, XK_g, vim_enter, 0, NULL, (struct chained_keybind[]) {
										{0, XK_g, vim_delete, VIM_TO_START_OF_FILE},
								}, CHAIN_COUNT(1),
						},
						{0, XK_i, vim_enter, 0, "inside", (struct chained_keybind[]) {
										VIM_DELIMITER_REGIONS(vim_delete, INSIDE)
								}, CHAIN_COUNT(16),
						},
						{0, XK_a, vim_enter, 0, "around", (struct chained_keybind[]) {
										VIM_DELIMITER_REGIONS(vim_delete, AROUND)
								}, CHAIN_COUNT(16),
						},
						numbers(),
						{XK_ANY_MOD, XK_0, vim_delete, VIM_TO_START_OF_LINE},
				}, CHAIN_COUNT(27),
		},
		// change
		{XK_ANY_MOD, XK_C, vim_change, VIM_TO_END_OF_LINE},
		{0, XK_c, vim_enter, 0, "change", (struct chained_keybind[]) {
						{0, XK_c, vim_change, VIM_CURRENT_LINE},
						//{0, XK_l, vim_remove_one_char},
						//{0, XK_h, vim_remove_one_char, -1},
						{XK_ANY_MOD, XK_I, vim_change, VIM_TO_END_OF_INDENT},
						{XK_ANY_MOD, XK_A, vim_change, VIM_TO_END_OF_LINE},
						{XK_ANY_MOD, XK_G, vim_change, VIM_TO_END_OF_FILE},
						{XK_ANY_MOD, XK_w, vim_change, VIM_TO_START_OF_WORD},
						{XK_ANY_MOD, XK_W, vim_change, VIM_TO_START_OF_STRING},
						{XK_ANY_MOD, XK_e, vim_change, VIM_TO_END_OF_WORD},
						{XK_ANY_MOD, XK_E, vim_change, VIM_TO_END_OF_STRING},
						{XK_ANY_MOD, XK_b, vim_change, VIM_PREV_WORD_START},
						{XK_ANY_MOD, XK_B, vim_change, VIM_PREV_STRING_START},
						{XK_ANY_MOD, XK_dollar, vim_change, VIM_TO_END_OF_LINE},
						{0, XK_g, vim_enter, 0, NULL, (struct chained_keybind[]) {
										{0, XK_g, vim_change, VIM_TO_START_OF_FILE},
								}, CHAIN_COUNT(1),
						},
						{0, XK_i, vim_enter, 0, "inside", (struct chained_keybind[]) {
										VIM_DELIMITER_REGIONS(vim_change, INSIDE)
								}, CHAIN_COUNT(16),
						},
						{0, XK_a, vim_enter, 0, "around", (struct chained_keybind[]) {
										VIM_DELIMITER_REGIONS(vim_change, AROUND)
								}, CHAIN_COUNT(16),
						},
						numbers(),
						{XK_ANY_MOD, XK_0, vim_change, VIM_TO_START_OF_LINE},
				}, CHAIN_COUNT(27-2),
		},
};

struct chained_keybind visual_mode_keybinds[]  = {
		numbers(),
		{XK_ANY_MOD, XK_Escape, vim_change_mode, VIM_NORMAL},
		{0, XK_v, vim_change_mode, VIM_NORMAL},

		{0, XK_h, vim_move_on_line, -1},
		{0, XK_j, vim_move_lines,   +1},
		{0, XK_k, vim_move_lines,   -1},
		{0, XK_l, vim_move_on_line, +1},
		{0, XK_Left, vim_move_on_line, -1},
		{0, XK_Down, vim_move_lines,   +1},
		{0, XK_Up, vim_move_lines,   -1},
		{0, XK_Right, vim_move_on_line, +1},
		{0, XK_BackSpace, vim_move_on_line, -1},
		{0, XK_Home, vim_home},
		{0, XK_End,  vim_end},
		{ControlMask, XK_Right, vim_move, VIM_TO_END_OF_STRING},
		{ControlMask, XK_Left, vim_move, VIM_PREV_STRING_START},
		{0, XK_w, vim_move, VIM_TO_START_OF_WORD},
		{ShiftMask, XK_W, vim_move, VIM_TO_START_OF_STRING},
		{0, XK_e, vim_move, VIM_TO_END_OF_WORD},
		{ShiftMask, XK_E, vim_move, VIM_TO_END_OF_STRING},
		{0, XK_b, vim_move, VIM_PREV_WORD_START},
		{ShiftMask, XK_B, vim_move, VIM_PREV_STRING_START},

		{XK_ANY_MOD, XK_0, vim_move, VIM_TO_START_OF_LINE},
		{XK_ANY_MOD, XK_A, vim_move, VIM_TO_END_OF_LINE},
		{XK_ANY_MOD, XK_G, vim_move, VIM_TO_END_OF_FILE},
		{XK_ANY_MOD, XK_w, vim_move, VIM_TO_START_OF_WORD},
		{XK_ANY_MOD, XK_W, vim_move, VIM_TO_START_OF_STRING},
		{XK_ANY_MOD, XK_e, vim_move, VIM_TO_END_OF_WORD},
		{XK_ANY_MOD, XK_E, vim_move, VIM_TO_END_OF_STRING},
		{XK_ANY_MOD, XK_b, vim_move, VIM_PREV_WORD_START},
		{XK_ANY_MOD, XK_B, vim_move, VIM_PREV_STRING_START},
		{XK_ANY_MOD, XK_dollar, vim_move, VIM_TO_END_OF_LINE},
		{0, XK_i, vim_enter, 0, "inside", (struct chained_keybind[]) {
						VIM_DELIMITER_REGIONS(vim_visual_delimit, INSIDE)
				}, CHAIN_COUNT(16),
		},
		{0, XK_a, vim_enter, 0, "around", (struct chained_keybind[]) {
						VIM_DELIMITER_REGIONS(vim_visual_delimit, AROUND)
				}, CHAIN_COUNT(16),
		},

		{0, XK_d, vim_delete, VIM_CURRENT_SELECTION},
		{0, XK_x, vim_delete, VIM_CURRENT_SELECTION},
		{XK_ANY_MOD, XK_X, vim_delete, VIM_CURRENT_SELECTION},
		{0, XK_y, vim_yank, VIM_CURRENT_SELECTION},
};

struct chained_keybind insert_mode_keybinds[]  = {
		{XK_ANY_MOD, XK_Escape, vim_change_mode, VIM_NORMAL},
		{0, XK_Page_Down, vim_move_down_one_screen_devided_by, 1},
		{0, XK_Page_Up,   vim_move_down_one_screen_devided_by,  -1},
		{0, XK_Home, vim_home},
		{0, XK_End,  vim_end},
		{0, XK_Left, vim_move_on_line, -1},
		{0, XK_Down, vim_move_lines,   +1},
		{0, XK_Up, vim_move_lines,   -1},
		{0, XK_Right, vim_move_on_line, +1},
		{ControlMask, XK_Right, vim_move, VIM_TO_END_OF_STRING},
		{ControlMask, XK_Left, vim_move, VIM_PREV_STRING_START},

		{0, XK_BackSpace, vim_backspace, -1},
		{0, XK_Delete,    vim_remove_one_char},
		{ControlMask, XK_BackSpace, vim_delete, VIM_PREV_STRING_START},
		{ControlMask, XK_Delete, vim_delete, VIM_TO_END_OF_STRING},
		{0, XK_Return,    vim_insert_return},
		{0, XK_Tab,       vim_insert_tab},
};

static void
push_keypress_to_log(KeySym ksym, unsigned int modkey, const char* buf, int len)
{
		if (chain_len+1 <= CHAINED_KEYBIND_LEN) {
				current_chain[chain_len].ksym = ksym;
				current_chain[chain_len].modkey = modkey;
				if (len != 0)
						current_chain[chain_len].buf = xrealloc(current_chain[chain_len].buf, len);
				memcpy(current_chain[chain_len].buf, buf, len);
				current_chain[chain_len].len = len;
				chain_len++;
		}
}

static int
do_chained_keybinds(struct chained_keybind** keybinds, int* keybind_count, KeySym ksym, unsigned int modkey, const char* buf, int len)
{
		// remove modifiers
		switch(ksym) {
		case XK_Shift_L:   case XK_Shift_R:
		case XK_Alt_L:     case XK_Alt_R:
		case XK_Control_L: case XK_Control_R:
		case XK_Super_L:   case XK_Super_R:
		case XK_Meta_L:    case XK_Meta_R:
		case XK_ISO_Group_Latch:  case XK_ISO_Group_Shift: case XK_ISO_Group_Lock:
		case XK_ISO_Level3_Latch: case XK_ISO_Level3_Lock: case XK_ISO_Level3_Shift:
		case XK_ISO_Level5_Latch: case XK_ISO_Level5_Lock: case XK_ISO_Level5_Shift:
		case XK_ISO_Level2_Latch:
				return 3;
		}
		push_keypress_to_log(ksym, modkey, buf, len);
		for (int i = 0; i < *keybind_count; i++) {
				struct chained_keybind keybind = (*keybinds)[i];
				if (ksym == keybind.keysym && match(keybind.mod, modkey)) {
						if (!keybind.func)
								return 1;

						int res = keybind.func(keybind.custom_mode);

						// TODO: add enums for random values
						if (!res)
								return 1;
						if (res == 2)
								continue;

						if (res < 0) {
								*keybinds = NULL;
								*keybind_count = 0;
								if (res == -2)
										return 4;
								return 2;
						}
						if (res == 1) {
								soft_assert(keybind.next_keybinds && keybind.keybind_count,
											*keybinds = NULL;
											return 0;
										);
								*keybinds = keybind.next_keybinds;
								*keybind_count = keybind.keybind_count;
								return 1;
						}
				}
		}
		*keybinds = NULL;
		return 0;
}

static const char*
get_ksym_string_no_xk(KeySym ksym, unsigned int modkey)
{
		static char str[1024];
		*str = 0;
		char* ksname;
		if (!(ksname = XKeysymToString(ksym)))
				ksname = "(no name)";
		else if (strcmp(ksname, "XK_") == 0)
				ksname += 3;

		if (modkey != XK_ANY_MOD) {
				if (modkey & ControlMask)
						strcat(str, "C-");
				if (modkey & MODKEY)
						strcat(str, "M-");
		}
		strncat(str, ksname, sizeof(str) - strlen(str) - 1);

		return str;
}

static const char*
current_key_chain_string(int dont_add_minus_end)
{
		if (!chain_len)
				return NULL;

		static char buf[STATUS_BAR_MAX_LEN];
		*buf = 0;
		int len = 0;

		for(int i = 0; i < chain_len; i++) {
				const char* str = get_ksym_string_no_xk(current_chain[i].ksym, current_chain[i].modkey);
				int new_len = strlen(str);
				if (new_len + len < STATUS_BAR_MAX_LEN) {
						strcat(buf, str);
						len += new_len;
				}

				if (len + 1 < STATUS_BAR_MAX_LEN) {
						if (i + 1 >= chain_len) {
								if (!dont_add_minus_end)
										strcat(buf, "-");
						} else {
								strcat(buf, " ");
						}
						len++;
				}
		}

		return buf;
}

static const char*
current_keybind_options(struct chained_keybind* keybinds, int keybind_len)
{
		if (!keybind_len || !keybinds)
				return "[no options]";
		static char buf[STATUS_BAR_MAX_LEN];
		strcpy(buf, "[");
		int len = 0;

		for(int i = 0; i < keybind_len; i++) {
				switch(keybinds[i].keysym) {
				case XK_0: case XK_1: case XK_2:
				case XK_3: case XK_4: case XK_5:
				case XK_6: case XK_7: case XK_8:
				case XK_9:
						if (!keybinds[i].description)
								continue;
				}

				const char* str = get_ksym_string_no_xk(keybinds[i].keysym, keybinds[i].mod);
				int new_len = strlen(str);
				if (new_len + len < STATUS_BAR_MAX_LEN) {
						strcat(buf, str);
						len += new_len;
				}
				if (keybinds[i].description) {
						new_len = strlen(keybinds[i].description) + sizeof(" →  ") - 1;
						if (new_len + len < STATUS_BAR_MAX_LEN) {
								strcat(buf, " →  ");
								strcat(buf, keybinds[i].description);
								len += new_len;
						}
				}

				if (i + 1 < keybind_len && len + 2 < STATUS_BAR_MAX_LEN) {
						strcat(buf, ", ");
						len += 2;
				}
		}
		if (len + 1 < STATUS_BAR_MAX_LEN)
				strcat(buf, "]");
		return buf;
}

/////////////////////////////////////////////////
// callbacks
//

#include "extensions/default_status_bar.h"
#include "extensions/window_modes/choose_one_of_selection.h"
#include "extensions/undo.h"
#include "extensions/keep_cursor_col.h"
#include "extensions/move_selection_with_cursor.h"
#include "extensions/startup_message.h"

static int keypress_actions(KeySym keycode, int modkey, const char* buf, int len);
static int vim_paste_callback(struct file_buffer* fb, char* data, int len);
static const struct extension vim = {
		.keypress = keypress_actions,
		.fb_paste = vim_paste_callback,
};

struct extension_meta config_extensions[] = {
		{.e = startup_message, .enabled = 1},
		{.e = file_browser, .enabled = 1},
		{.e = search_open_fb, .enabled = 1},
		{.e = search_keywords_open_fb, .enabled = 1},

		{.e = syntax_e, .enabled = 1},

		{.e = undo, .enabled = 1},
		{.e = keep_cursor_col, .enabled = 1},
		{.e = move_selection_with_cursor, .enabled = 1},
		{.e = vim, .enabled = 1},

		{.e = default_status_bar, .enabled = 1},

		{.end = 1}
};

struct extension_meta* extensions = config_extensions;

static int
vim_paste_callback(struct file_buffer* fb, char* data, int len)
{
		int offset = focused_window->fb_index;
		if (data[len-1] == '\n') {
				if (paste_behind) {
						offset = fb_seek_char_backwards(fb, offset, '\n');
						offset = MAX(offset, 0);
				} else {
						offset = fb_seek_char(fb, offset, '\n');
						if (offset < 0)
								offset = fb->len-1;
						else
								offset++;
				}
		} else if (paste_behind) {
				offset = MAX(offset - 1, 0);
		}
		fb_insert(fb, data, len, offset, 1);
		window_node_move_all_cursors_on_same_fb(&root_node, (paste_behind) ? NULL : focused_node, focused_window->fb_index, offset,
												wb_move_offset_relative, len, CURSOR_COMMAND_MOVEMENT);
		paste_behind = 0;
		return 0;
}

static int
vim_repeat_last_command(int custom_mode)
{
		static int recursioin_prevention = 0;
		if (recursioin_prevention || !previous_chain_len)
				return -2;
		recursioin_prevention = 1;
		vim_change_mode(VIM_NORMAL);

		int count = vim_chain_parse_count(0);
		while(count--) {
				chain_len = 0;
				last_used_command_index = 0;
				for(int i = 0; i < previous_chain_len; i++) {
						struct keypress_logg pc = previous_chain[i];
						keypress_actions(pc.ksym, pc.modkey, pc.buf, pc.len);
				}
		}

		vim_change_mode(VIM_NORMAL);
		recursioin_prevention = 0;
		writef_to_status_bar("repeated %d inputs", previous_chain_len);
		return -2;
}

void insert_string(const char* buf, int len)
{
		struct file_buffer* fb = get_fb(focused_window);

		if (buf[0] >= 32 || len > 1) {
				fb_delete_selection(fb);
				fb_insert(fb, buf, len, focused_window->cursor_offset, 0);
				window_node_move_all_cursors_on_same_fb(&root_node, NULL, focused_window->fb_index, focused_window->cursor_offset,
														wb_move_offset_relative, len, CURSOR_COMMAND_MOVEMENT);
		} else {
				writef_to_status_bar("unhandled control character 0x%x\n", buf[0]);
		}
}

int
keypress_actions(KeySym keycode, int modkey, const char* buf, int len)
{
		// current keybind that is set
		static struct chained_keybind* keybinds;
		static int keybind_len;
		if (vim_mode == VIM_NORMAL) {
				if (!keybinds) {
						keybinds = normal_mode_keybinds;
						keybind_len = LEN(normal_mode_keybinds);
				}
		} else if (vim_mode == VIM_INSERT) {
				if (!keybinds) {
						keybinds = insert_mode_keybinds;
						keybind_len = LEN(insert_mode_keybinds);
				}
		} else if (vim_mode == VIM_VISUAL) {
				if (!keybinds) {
						keybinds = visual_mode_keybinds;
						keybind_len = LEN(visual_mode_keybinds);
				}
		}

		char tmp_status_bar[STATUS_BAR_MAX_LEN];
		memcpy(tmp_status_bar, status_bar_contents, STATUS_BAR_MAX_LEN);

		int last_mode = vim_mode;
		int res = do_chained_keybinds(&keybinds, &keybind_len, keycode, modkey, buf, len);

		int status_bar_changed = strcmp(tmp_status_bar, status_bar_contents) != 0;

		if (res == 3)
				return 0;
		if (res == 2)
				last_used_command_index = MAX(0, chain_len - 1);
		if (!res) {
				if (vim_mode == VIM_INSERT) {
						insert_string(buf, len);
				} else  {
						if (vim_mode == VIM_NORMAL) {
								const char* chain_str = current_key_chain_string(1);
								writef_to_status_bar("keybind \"%s\" does not exist", chain_str);

								// reset chain
								chain_len = 0;
								last_used_command_index = 0;
						} else {
								writef_to_status_bar("keybind does not exist");
						}
				}
		} else if ((res == 2 || res == 4) && vim_mode == VIM_NORMAL) {
				// reset chain
				if (chain_len > 1 && !status_bar_changed && last_mode == vim_mode) {
						const char* chain_str = current_key_chain_string(1);
						if (chain_str)
								writef_to_status_bar("%s", chain_str);
				}
				if (res != 4) {
						vim_copy_log(&previous_chain, previous_chain_len, current_chain, chain_len);
						previous_chain_len = chain_len;
				}
				chain_len = 0;
				last_used_command_index = 0;
		} else if (vim_mode == VIM_NORMAL) {
				const char* chain_str = current_key_chain_string(0);
				const char* options = current_keybind_options(keybinds, keybind_len);
				if (chain_str && !status_bar_changed)
						writef_to_status_bar("%s %s", chain_str, options);
		} else {
				const char* options = current_keybind_options(keybinds, keybind_len);

				if (chain_len > 0 && strcmp(options, "[no options]") != 0) {
						const char* latest_keypress = get_ksym_string_no_xk(current_chain[chain_len - 1].ksym, current_chain[chain_len - 1].modkey);
						writef_to_status_bar("%s- %s", latest_keypress, options);
				}
		}
		return 0;
}

// TODO: gg goes to line but not G
