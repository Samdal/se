/* See LICENSE for license details. */

/*
** This file mainly contains the functionality of handling the
** "buffer". There should a good amount of customisation you
** can do wihtout tuching this file, but adding or changing
** functionality to fit your needs shouldn't be too hard.
*/

#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <dirent.h>

#include "se.h"
#include "x.h"
#include "config.h"
#include "extension.h"

// se.c globals

/////////////////////////////////////////////
// Internal functions
//

static int  writef_string(int y, int x1, int x2, const char* fmt, ...);
static void color_selection(struct glyph* letter);

////////////////////////////////////////////
// function implementations
//

int
writef_string(int y, int x1, int x2, const char* fmt, ...)
{
	char string[STATUS_BAR_MAX_LEN];

	va_list args;
	va_start(args, fmt);
	vsnprintf(string, STATUS_BAR_MAX_LEN, fmt, args);
	va_end(args);

	return write_string(string, y, x1, x2);
}

char status_bar_contents[STATUS_BAR_MAX_LEN] = {0};
static int status_bar_end;
uint32_t status_bar_bg;
void
writef_to_status_bar(const char* fmt, ...)
{
	if (fmt) {
		if (status_bar_bg == error_color   ||
			status_bar_bg == warning_color ||
			status_bar_bg == ok_color)
			return;

		va_list args;
		va_start(args, fmt);
		vsnprintf(status_bar_contents, STATUS_BAR_MAX_LEN, fmt, args);
		va_end(args);

		status_bar_bg = alternate_bg_dark;
		return;
	}

	global_attr = default_attributes;
	global_attr.bg = status_bar_bg;

	status_bar_end = write_string(status_bar_contents, screen.row-1, 0, screen.col);
	screen_set_region(status_bar_end, screen.row-1, screen.col-1, screen.row-1, ' ');

	global_attr = default_attributes;
}

void
draw_status_bar()
{
	writef_to_status_bar(NULL);
	xdrawline(0, screen.row-1, screen.col);
	draw_horisontal_line(screen.row-2, 0, screen.col-1);
	if (get_fb(focused_window)->mode & FB_SEARCH_BLOCKING)
		xdrawcursor(status_bar_end, screen.row-1, 1);
	status_bar_bg = alternate_bg_dark;
}

void
window_node_draw_to_screen(struct window_split_node* wn)
{
	struct window_buffer* wb = &wn->wb;
	struct file_buffer* fb = get_fb(wb);
	int minx = wn->minx, miny = wn->miny,
		maxx = wn->maxx, maxy = wn->maxy;

	LIMIT(wb->cursor_offset, 0, fb->len);
	screen_set_region(minx, miny, maxx, maxy, ' ');
	int focused = wb == focused_window && !(fb->mode & FB_SEARCH_BLOCKING);

	int x = minx, y = miny;
	global_attr = default_attributes;

	// force the screen in a place where the cursor is visable
	int ox, oy, xscroll;
	fb_offset_to_xy(fb, wb->cursor_offset, maxx - minx, wb->y_scroll, &ox, &oy, &xscroll);
	if (oy < 0) {
		wb->y_scroll += oy;
	} else {
		oy += miny - maxy+2;
		if (oy > 0)
			wb->y_scroll += oy;
	}
	if (wb->y_scroll < 0)
		wb->y_scroll = 0;

	if (wrap_buffer)
		xscroll = 0;

	// move to y_scroll
	char* repl = fb->contents;
	char* last = repl + fb->len;
	char* new_repl;
	int line = wb->y_scroll;
	while ((new_repl = memchr(repl, '\n', last - repl))) {
		if (--line < 0)
			break;
		else if (new_repl+1 < last)
			repl = new_repl+1;
		else
			return;
	}
	int offset_start = repl - fb->contents - 1;
	int cursor_x = 0, cursor_y = 0;

	// actually write to the screen
	int once = 0;
	int search_found = 0;
	int non_blocking_search_found = 0;

	// TODO: verify that last - repl is the same as offset_last - offset_start
	int move_buffer_len = last - repl + 2;
	uint8_t* move_buffer = xmalloc(move_buffer_len);
	memset(move_buffer, 0, move_buffer_len);
	move_buffer[0] = 0;
	int lastx = x, lasty = y;
	int move_buffer_index = 0;

	// TODO: write max string len of 127
	// TODO: make the write thing similar to syntax?
	char* new_line_start = NULL;
	call_extension(wb_new_line_draw, &new_line_start, wb, y - miny, maxy - y, minx, maxx, &global_attr);
	if (new_line_start) {
		struct glyph old_attr = global_attr;
		global_attr = default_attributes;
		x = write_string(new_line_start, y, minx, maxx+1);
		global_attr = old_attr;
	}

	int tmp = 0;
	call_extension(wb_write_status_bar, &tmp, NULL, 0, 0, 0, 0, NULL, NULL);

	for (int charsize = 1; repl < last && charsize; repl += charsize) {
		if (y > lasty) {
			move_buffer[move_buffer_index] = x - minx;
			move_buffer[move_buffer_index] |= 1<<7;
		} else {
			move_buffer[move_buffer_index] = x - lastx;
		}
		move_buffer_index++;
		lastx = x, lasty = y;

		if (!once && repl - fb->contents >= wb->cursor_offset) {
			// if the buffer being drawn is focused, set the cursor position global
			once = 1;
			cursor_x = x - xscroll;
			cursor_y = y;
			LIMIT(cursor_x, minx, maxx);
			LIMIT(cursor_y, miny, maxy);
		}

		if (!wrap_buffer && x - xscroll > maxx && *repl != '\n') {
			charsize = 1;
			x++;
			continue;
		}

		if (*repl == '\n' || (wrap_buffer && x >= maxx)) {
			x = minx;
			if (++y >= maxy-1)
				break;
			if (wrap_buffer && *repl != '\n')
				continue;
			charsize = 1;
			char* new_line_start = NULL;
			call_extension(wb_new_line_draw, &new_line_start, wb, y - miny, maxy - y, minx, maxx, &global_attr);
			if (new_line_start) {
				struct glyph old_attr = global_attr;
				global_attr = default_attributes;
				x = write_string(new_line_start, y, minx, maxx+1);
				global_attr = old_attr;
			}
			continue;
		} else if (*repl == '\t') {
			charsize = 1;
			if ((x - minx) <= 0) {
				x += screen_set_char(' ', x - xscroll, y);
				if (x >= maxx)
					continue;
			}
			while ((x - minx) % tabspaces != 0 && x - xscroll <= maxx)
				x += screen_set_char(' ', x - xscroll, y);

			if (x - xscroll <= maxx)
				x += screen_set_char(' ', x, y);
			continue;
		}

		rune_t u;
		charsize = utf8_decode_buffer(repl, last - repl, &u);

		int width;
		if (x - xscroll >= minx)
			width = screen_set_char(u, x - xscroll, y);
		else
			width = wcwidth(u);

		// drawing search highlight
		if (fb->mode & FB_SEARCH_BLOCKING_MASK) {
			if (!search_found && fb_offset_starts_with(fb, repl - fb->contents, fb->search_term))
				search_found = strlen(fb->search_term);
			if (search_found) {
				screen_set_attr(x - xscroll, y)->bg = highlight_color;
				screen_set_attr(x - xscroll, y)->fg = default_attributes.bg;
				search_found--;
			}
		}
		if (fb->mode & FB_SEARCH_NON_BLOCKING) {
			if (!non_blocking_search_found && fb_offset_starts_with(fb, repl - fb->contents, fb->non_blocking_search_term))
				non_blocking_search_found = strlen(fb->search_term);
			if (non_blocking_search_found) {
				screen_set_attr(x - xscroll, y)->fg = highlight_color;
				screen_set_attr(x - xscroll, y)->mode |= ATTR_UNDERLINE;
				non_blocking_search_found--;
			}
		}

		x += width;
	}
	int offset_end = repl - fb->contents;
	global_attr = default_attributes;

	if (wb->cursor_offset >= fb->len) {
		cursor_x = x - xscroll;
		cursor_y = MIN(y, maxy);
	}

	call_extension(window_written_to_screen, wn, offset_start, offset_end, move_buffer, move_buffer_len);

	int status_end = minx;
	int write_again;
	do {
		write_again = 0;
		char bar[LINE_MAX_LEN];
		*bar = 0;

		call_extension(wb_write_status_bar, &write_again, wb, status_end, maxx+1, cursor_x - minx + xscroll, cursor_y - miny + wb->y_scroll, bar, &global_attr);
		status_end = write_string(bar, maxy-1, status_end, maxx+1);

		global_attr = default_attributes;
	} while (write_again);

	if (fb->mode & FB_SELECTION_ON) {
		int y1, y2, tmp;
		fb_offset_to_xy(fb, fb->s1o, 0, wb->y_scroll, &tmp, &y1, &tmp);
		fb_offset_to_xy(fb, fb->s2o, 0, wb->y_scroll, &tmp, &y2, &tmp);
		writef_string(maxy-1, status_end, maxx, " %dL", abs(y1-y2));
	}

	if (focused) {
		for (int i = minx; i < maxx+1; i++) {
			if (!(fb->mode & FB_SELECTION_ON)) {
				if (screen_set_attr(i, cursor_y)->bg == default_attributes.bg)
					screen_set_attr(i, cursor_y)->bg = mouse_line_bg;
			}
			screen_set_attr(i, maxy-1)->bg = alternate_bg_bright;
		}
	}

	wb_write_selection(wb, minx, miny, maxx, maxy);
	//do_syntax_scheme(NULL, &(struct syntax_scheme){0}, 0);

	for (int i = miny; i < maxy; i++)
		xdrawline(minx, i, maxx+1);

	draw_horisontal_line(maxy-1, minx, maxx);

	xdrawcursor(cursor_x, cursor_y, focused);
	free(move_buffer);
}

int
write_string(const char* string, int y, int minx, int maxx)
{
	if (!string)
		return 0;
	LIMIT(maxx, 0, screen.col);
	LIMIT(minx, 0, maxx);

	int offset = 0;
	int len = strlen(string);
	while(minx < maxx && offset < len) {
		rune_t u;
		int charsize = utf8_decode_buffer(string + offset, len - offset, &u);
		offset += charsize;
		if (charsize == 1 && u <= 32)
			u = ' ';
		minx += screen_set_char(u, minx, y);
	}
	return minx;
}

void
color_selection(struct glyph* letter)
{
	if (letter->bg == default_attributes.bg)
		letter->bg = selection_bg;
}

void
wb_move_cursor_to_selection_start(struct window_buffer* wb)
{
	const struct file_buffer* fb = get_fb(wb);
	if (fb_is_selection_start_top_left(fb))
		wb_move_to_offset(wb, fb->s1o, CURSOR_SNAPPED);
	else
		wb_move_to_offset(wb, fb->s2o, CURSOR_SNAPPED);
}

void
wb_write_selection(struct window_buffer* wb, int minx, int miny, int maxx, int maxy)
{
	soft_assert(wb, return;);
	struct file_buffer* fb = get_fb(wb);

	LIMIT(maxx, 0, screen.col-1);
	LIMIT(maxy, 0, screen.row-1);
	LIMIT(minx, 0, maxx);
	LIMIT(miny, 0, maxy);

	if (!(fb->mode & FB_SELECTION_ON))
		return;

	int x, y, x2, y2, tmp, xscroll;
	if (fb_is_selection_start_top_left(fb)) {
		fb_offset_to_xy(fb, fb->s1o, maxx - minx, wb->y_scroll, &x, &y, &tmp);
		fb_offset_to_xy(fb, fb->s2o, maxx - minx, wb->y_scroll, &x2, &y2, &xscroll);
	} else {
		fb_offset_to_xy(fb, fb->s2o, maxx - minx, wb->y_scroll, &x, &y, &xscroll);
		fb_offset_to_xy(fb, fb->s1o, maxx - minx, wb->y_scroll, &x2, &y2, &tmp);
	}
	x += minx, x2 += minx + 1;
	y += miny, y2 += miny;
	if (!wrap_buffer) {
		x -= xscroll;
		x2 -= xscroll;
	}


	for(; y < y2; y++) {
		for(; x <= maxx; x++)
			color_selection(screen_set_attr(x, y));
		x = 0;
	}
	for(; x < x2; x++)
		color_selection(screen_set_attr(x, y));
}
