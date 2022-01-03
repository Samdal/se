/* See LICENSE for license details. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "st.h"
#include "win.h"

/* Arbitrary sizes */
#define UTF_INVALID   0xFFFD
#define UTF_SIZ       4

/* macros */
#define IS_SET(flag)		((term.mode & (flag)) != 0)

Term term;
static void tclearregion(int, int, int, int);
static void treset(void);
static void tsetdirt(int, int);
static void tswapscreen(void);
static void tfulldirt(void);

static void drawregion(int, int, int, int);

static void colour_selection(Glyph* letter);
static void set_selection_start_end(int* x1, int* x2, int* y1, int* y2, Buffer* buffer);

static int t_decode_utf8_buffer(const char* buffer, const int buflen, Rune* u);

static const uchar utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const uchar utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static size_t utf8decode(const char *, Rune *, size_t);
static Rune utf8decodebyte(char, size_t *);
static char utf8encodebyte(Rune, size_t);
static size_t utf8validate(Rune *, size_t);

void *
xmalloc(size_t len)
{
	void *p;

	if (!(p = malloc(len)))
		die("malloc: %s\n", strerror(errno));

	return p;
}

void *
xrealloc(void *p, size_t len)
{
	if ((p = realloc(p, len)) == NULL)
		die("realloc: %s\n", strerror(errno));

	return p;
}

size_t
utf8decode(const char *c, Rune *u, size_t clen)
{
	size_t i, j, len, type;
	Rune udecoded;

	*u = UTF_INVALID;
	if (!clen)
		return 0;
	udecoded = utf8decodebyte(c[0], &len);
	if (!BETWEEN(len, 1, UTF_SIZ))
		return 1;
	for (i = 1, j = 1; i < clen && j < len; ++i, ++j) {
		udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
		if (type != 0)
			return j;
	}
	if (j < len)
		return 0;
	*u = udecoded;
	utf8validate(u, len);

	return len;
}

Rune
utf8decodebyte(char c, size_t *i)
{
	for (*i = 0; *i < LEN(utfmask); ++(*i))
		if (((uchar)c & utfmask[*i]) == utfbyte[*i])
			return (uchar)c & ~utfmask[*i];
	return 0;
}

size_t
utf8encode(Rune u, char *c)
{
	size_t len, i;

	len = utf8validate(&u, 0);
	if (len > UTF_SIZ)
		return 0;

	for (i = len - 1; i != 0; --i) {
		c[i] = utf8encodebyte(u, 0);
		u >>= 6;
	}
	c[0] = utf8encodebyte(u, len);

	return len;
}

char
utf8encodebyte(Rune u, size_t i)
{
	return utfbyte[i] | (u & ~utfmask[i]);
}

size_t
utf8validate(Rune *u, size_t i)
{
	const Rune utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
	const Rune utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

	if (!BETWEEN(*u, utfmin[i], utfmax[i]) || BETWEEN(*u, 0xD800, 0xDFFF))
		*u = UTF_INVALID;
	for (i = 1; *u > utfmax[i]; ++i)
		;

	return i;
}

void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

int
tattrset(int attr)
{
	for (int i = 0; i < term.row-1; i++)
		for (int j = 0; j < term.col-1; j++)
			if (term.line[i][j].mode & attr)
				return 1;
	return 0;
}

void
tsetdirt(int top, int bot)
{
	int i;

	LIMIT(top, 0, term.row-1);
	LIMIT(bot, 0, term.row-1);

	for (i = top; i <= bot; i++)
		term.dirty[i] = 1;
}

int
tisdirty()
{
	for (int i = 0; i < term.row-1; i++)
		if (term.dirty[i])
			return 1;
	return 0;
}

void
tsetdirtattr(int attr)
{
	int i, j;

	for (i = 0; i < term.row-1; i++) {
		for (j = 0; j < term.col-1; j++) {
			if (term.line[i][j].mode & attr) {
				tsetdirt(i, i);
				break;
			}
		}
	}
}

void
tfulldirt(void)
{
	tsetdirt(0, term.row-1);
}

void
treset(void)
{
	uint i;

	term.c = (TCursor){{
		.mode = ATTR_NULL,
		.fg = defaultfg,
		.bg = defaultbg
	}, .x = 0, .y = 0};

	memset(term.tabs, 0, term.col * sizeof(*term.tabs));
	for (i = tabspaces; i < term.col; i += tabspaces)
		term.tabs[i] = 1;
	term.mode = default_mode;

	for (i = 0; i < 2; i++) {
		tclearregion(0, 0, term.col-1, term.row-1);
		tswapscreen();
	}
}

void
tnew(int col, int row)
{
	term = (Term){ .c = { .attr = { .fg = defaultfg, .bg = defaultbg } } };
	tresize(col, row);
	treset();
}

void
tswapscreen(void)
{
	Line *tmp = term.line;

	term.line = term.alt;
	term.alt = tmp;
	term.mode ^= MODE_ALTSCREEN;
	tfulldirt();
}

void
buffer_scroll(Buffer* buffer, int xscroll, int yscroll)
{
	if (yscroll) {
		buffer->y_scroll += yscroll;
		// prevent you from scrolling outside of the screen
		if (buffer->y_scroll < 0)
			buffer->y_scroll = 0;
		else
			buffer_snap_cursor(buffer, 0);
	}

	if (xscroll) {
		buffer->x_scroll += xscroll;
		if (buffer->x_scroll < 0)
			buffer->x_scroll = 0;
	}

	while(buffer->x_scroll > 0 && term.c.x+1 < term.col) {
		term.c.x++;
		buffer->x_scroll--;
	}
}

void
buffer_move_cursor(Buffer* buffer, int x, int y, enum cursor_reason callback_reason)
{
	term.c.y = (y <= 0) ? 0 : (y >= term.row) ? term.row-1 : y;
	int ydiff = y - term.c.y;

	term.c.x = (x <= 0) ? 0 : (x >= term.col-1) ? term.col-1 : x;
	if (term.c.x > 0 && term.line[term.c.y][term.c.x].mode & ATTR_WDUMMY)
		term.c.x--;

	int xdiff = x - term.c.x;

	if (callback_reason) {
		buffer_scroll(buffer, xdiff, ydiff);
		if (cursor_movement_callback && callback_reason != CURSOR_SCROLL_ONLY) {
			cursor_movement_callback(term.c.x, term.c.y, callback_reason);
		}
	}
}

void
buffer_move_cursor_relative(Buffer* buffer, int x, int y, enum cursor_reason callback_reason)
{
	int xmoves = x;
	x = term.c.x;
	y += term.c.y;

	int y_prev = term.c.y, x_prev = term.c.x;

	if (xmoves > 0) {
		buffer_move_cursor(buffer, term.c.x, y, 0);
		char* repl = &(buffer->contents[buffer_snap_cursor(buffer, 1)]);
		char* last = &(buffer->contents[buffer->len]);
		for (int charsize;
			 repl < last && xmoves && *repl != '\n';
			 repl += charsize, xmoves--) {
			Rune u;
			charsize = t_decode_utf8_buffer(repl, last - repl, &u);

			if (*repl == '\t') {
				if (x == 0)
					x++;
				while (x % tabspaces != 0)
					x++;
				x++;
			} else {
				x += wcwidth(u);
			}
		}
	} else if (xmoves < 0) {
		while(xmoves++ < 0 && term.c.x > 0)
			buffer_move_cursor(buffer, term.c.x-1, term.c.y, 0);
		x = term.c.x;
	}

	buffer_move_cursor(buffer, x_prev, y_prev, 0);
	buffer_move_cursor(buffer, x, y, callback_reason);
}

int
buffer_new(Buffer* buffer, char* file_path)
{
	assert(buffer);
	if (!file_path) {
		die("creating new buffers not implemented\n");
	}
	memset(buffer, 0, sizeof(Buffer));
	buffer->file_path = file_path;

	FILE *file = fopen(file_path, "rb");
	if (!file) {
		fprintf(stderr, "---error reading file \"%s\"---\n", file_path);
		return -1;
	}

	fseek(file, 0L, SEEK_END);
	long readsize = ftell(file);
	rewind(file);

	assert(readsize);

	if (readsize > (long)1.048576e+7) {
		fclose(file);
		die("you are opening a huge file(>10MiB), not allowed");
	}

	buffer->len = readsize;
	buffer->capacity = readsize + 100;

	buffer->contents = xmalloc(buffer->capacity);

	fread(buffer->contents, 1, readsize, file);
	fclose(file);

	buffer->undo_buffer_len = undo_buffers;
	buffer->ub = xmalloc(sizeof(struct undo_buffer) * undo_buffers);
	memset(buffer->ub, 0, sizeof(struct undo_buffer) * undo_buffers);
	buffer->available_redo_buffers = 0;
	buffer->current_undo_buffer = 0;

	if (buffer_contents_updated)
		buffer_contents_updated(buffer, term.c.x, term.c.y, BUFFER_CONTENT_INIT);

	return buffer->len;
}

void
buffer_insert(Buffer* buffer, const char* new_content, const int len, const int offset, int do_not_callback)
{
	assert(buffer->contents);
	if (offset > buffer->len)
		die("writing past buffer %s\n", buffer->file_path);

	if (buffer->len + len >= buffer->capacity) {
		buffer->capacity = buffer->len + len + 256;
		buffer->contents = xrealloc(buffer->contents, buffer->capacity);
	}
	if (offset < buffer->len)
		memmove(buffer->contents+offset+len, buffer->contents+offset, buffer->len-offset);
	buffer->len += len;

	memcpy(buffer->contents+offset, new_content, len);
	if (buffer_contents_updated && !do_not_callback)
		buffer_contents_updated(buffer, term.c.x, term.c.y, BUFFER_CONTENT_NORMAL_EDIT);
}

void
buffer_change(Buffer* buffer, const char* new_content, const int len, const int offset, int do_not_callback)
{
	assert(buffer->contents);
	if (offset > buffer->len)
		die("writing past buffer %s\n", buffer->file_path);

	if (offset + len > buffer->len) {
		buffer->len = offset + len;
		if (buffer->len >= buffer->capacity) {
			buffer->capacity = buffer->len + len + 256;
			buffer->contents = xrealloc(buffer->contents, buffer->capacity);
		}
	}

	memcpy(buffer->contents+offset, new_content, len);
	if (buffer_contents_updated && !do_not_callback)
		buffer_contents_updated(buffer, term.c.x, term.c.y, BUFFER_CONTENT_NORMAL_EDIT);
}

void
buffer_remove(Buffer* buffer, const int offset, int len, int do_not_calculate_charsize, int do_not_callback)
{
	assert(buffer->contents);
	if (offset > buffer->len)
		die("deleting past buffer %s\n", buffer->file_path);

	int removed_len = 0;
	if (do_not_calculate_charsize) {
		removed_len = len;
	} else {
		while (len--) {
			int charsize = t_decode_utf8_buffer(buffer->contents + offset, buffer->len - offset, NULL);
			if (buffer->len - charsize < 0)
				return;
			removed_len += charsize;
		}
	}
	buffer->len -= removed_len;
	memmove(buffer->contents+offset, buffer->contents+offset+removed_len, buffer->len-offset);
	if (buffer_contents_updated && !do_not_callback)
		buffer_contents_updated(buffer, term.c.x, term.c.y, BUFFER_CONTENT_NORMAL_EDIT);
}

void
buffer_x_scroll(Buffer* buffer, const int amount)
{
	buffer->x_scroll += amount;
	if (buffer->x_scroll < 0) {
		buffer->x_scroll = 0;
		return;
	}

	// prevent you from scrolling outside of the screen
	buffer_snap_cursor(buffer, 0);
}

void
buffer_write_to_screen(Buffer* buffer)
{
	//TODO: render tabs
	Glyph attr = term.c.attr;

	int line = buffer->y_scroll;
	const int xscroll = buffer->x_scroll;
	int x = 0, y = 0;
	tclearregion(0, 0, term.col-1, term.row-1);

	char* repl = buffer->contents;
	char* last = repl + buffer->len;

	char* new_repl;
	while ((new_repl = memchr(repl, '\n', last - repl))) {
		if (--line < 0)
			break;
		if (new_repl+1 < last) {
			repl = new_repl+1;
		} else {
			return;
		}
	}

	for (int charsize = 1; repl < last && charsize; repl += charsize) {
		if (x - xscroll >= term.col)
			repl = memchr(repl, '\n', last - repl);

		if (*repl == '\n') {
			x = 0;
			if (++y >= term.row)
				break;
			charsize = 1;
			continue;
		} else if (*repl == '\t') {
			charsize = 1;
			if (x <= 0) {
				x += tsetchar(' ', attr, x - xscroll, y);
				if (x >= term.col)
					continue;
			}
			while (abs(x) % tabspaces != 0 && x - xscroll < term.col)
				x += tsetchar(' ', attr, x - xscroll, y);

			if (x - xscroll >= term.col)
				continue;
			x += tsetchar(' ', attr, x, y);
			continue;
		}

		Rune u;
		charsize = t_decode_utf8_buffer(repl, last - repl, &u);
		int width = 1;
		if (x - xscroll >= 0)
			width = tsetchar(u, attr, x - xscroll, y);
		x += width;
	}

}

void
colour_selection(Glyph* letter)
{
	int fg = letter->fg;
	letter->fg = letter->bg;
	letter->bg = fg;
}


int
buffer_is_selection_start_top_left(Buffer* buffer)
{
	return (buffer->s1o <= buffer->s2o) ? 1 : 0;
}

void
buffer_move_cursor_to_selection_start(Buffer* buffer)
{
	if (buffer_is_selection_start_top_left(buffer))
		buffer_move_cursor_to_offset(buffer, buffer->s1o, 0);
	else
		buffer_move_cursor_to_offset(buffer, buffer->s2o, 0);
}

void
set_selection_start_end(int* x1, int* x2, int* y1, int* y2, Buffer* buffer)
{
	int prevx = term.c.x, prevy = term.c.y;
	int prevxscroll = buffer->x_scroll, prevyscroll = buffer->y_scroll;
	buffer_move_cursor_to_offset(buffer, buffer->s1o, 1);
	int s1xscroll = buffer->x_scroll, s1yscroll = buffer->y_scroll;
	if (buffer_is_selection_start_top_left(buffer)) {
		*x1 = term.c.x + (s1xscroll - prevxscroll);
		*y1 = term.c.y + (s1yscroll - prevyscroll);
		*x2 = prevx+1;
		*y2 = prevy;
	} else {
		*x1 = prevx;
		*y1 = prevy;
		*x2 = term.c.x+1 + (s1xscroll - prevxscroll);
		*y2 = term.c.y + (s1yscroll - prevyscroll);
	}
	buffer_move_cursor(buffer, prevx, prevy, 0);
	buffer->x_scroll = prevxscroll;
	buffer->y_scroll = prevyscroll;
	LIMIT(*x1, 0, term.col-1);
	LIMIT(*x2, 0, term.col-1);
	LIMIT(*y1, 0, term.row-1);
	LIMIT(*y2, 0, term.row-1);
}

void
buffer_write_selection(Buffer* buffer)
{
	if (!(buffer->mode & BUFFER_SELECTION_ON))
		return;

	// colour selected area
	int x, y, x2, y2;

	//TODO: implement alternative selection modes
	set_selection_start_end(&x, &x2, &y, &y2, buffer);

	for(; y < y2; y++) {
		for(; x < term.col; x++) {
			colour_selection(&term.line[y][x]);
		}
		x = 0;
	}
	for(; x < x2; x++) {
		colour_selection(&term.line[y][x]);
	}
}

char* buffer_get_selection(Buffer* buffer, int* selection_len)
{
	if (!(buffer->mode & BUFFER_SELECTION_ON))
		return NULL;

	int start, end, len;
	if (buffer_is_selection_start_top_left(buffer)) {
		start = buffer->s1o;
		end = buffer->s2o+1;
	} else {
		start = buffer->s2o;
		end = buffer->s1o+1;
	}
	len = end - start;
	if (selection_len)
		*selection_len = len;

	char* returned_selction = xmalloc(len + 1);
	memcpy(returned_selction, buffer->contents+start, len);
	returned_selction[len] = 0;
	return returned_selction;
}

void
buffer_remove_selection(Buffer* buffer)
{
	if (!(buffer->mode & BUFFER_SELECTION_ON))
		return;

	int start, end, len;
	if (buffer_is_selection_start_top_left(buffer)) {
		start = buffer->s1o;
		end = buffer->s2o+1;
	} else {
		start = buffer->s2o;
		end = buffer->s1o+1;
	}
	len = end - start;
	buffer_remove(buffer, start, len, 1, 1);
	if (buffer_contents_updated)
		buffer_contents_updated(buffer, term.c.x, term.c.y, BUFFER_CONTENT_BIG_CHANGE);
}

void
buffer_write_to_filepath(const Buffer* buffer)
{
	if (!buffer->file_path)
		return;
	assert(buffer->contents);
	FILE* file = fopen(buffer->file_path, "w");
	assert(file);

	fwrite(buffer->contents, sizeof(char), buffer->len, file);

	fclose(file);
}

int
t_decode_utf8_buffer(const char* buffer, const int buflen, Rune* u)
{
	if (!buflen) return 0;

	Rune u_tmp;
	int charsize;
	if (!u)
		u = &u_tmp;

	if (IS_SET(MODE_UTF8)) {
		/* process a complete utf8 char */
		charsize = utf8decode(buffer, u, buflen);
	} else {
		*u = buffer[0] & 0xFF;
		charsize = 1;
	}

	return charsize;
}

int
buffer_snap_cursor(Buffer* buffer, int do_not_callback)
{
	char* repl = buffer->contents;
	char* last = repl + buffer->len;
	int line = buffer->y_scroll + term.c.y;
	const int xscroll = buffer->x_scroll;
	int x = 0, y = 0;

	char* new_repl;
	while ((new_repl = memchr(repl, '\n', last - repl))) {
		if (line-- <= 0)
			break;
		else if (line - term.c.y < 0)
			y++;

		if (new_repl+1 < last) {
			repl = new_repl+1;
		} else {
			buffer_move_cursor_to_offset(buffer, buffer->len, 0);
			return buffer->len;
		}
	}

	for (int charsize = 1; repl < last; repl += charsize) {
		if (x - xscroll >= term.c.x || *repl == '\n') {
			break;
		} else if (*repl == '\t') {
			int prevx = x;
			if (x - xscroll + 1 == 0)
				x++;
			while ((x+1) % tabspaces != 0)
				x++;
			x += 2;
			if (x - xscroll > term.c.x) {
				x = prevx;
				break;
			}
			charsize = 1;
			continue;
		}
		Rune u;
		if (!(charsize = t_decode_utf8_buffer(repl, last - repl, &u)))
			return -1;
		x += wcwidth(u);
	}
	x -= xscroll;
	if (term.c.x != x || term.c.y != y)
		buffer_move_cursor(buffer, x, y, (do_not_callback) ? 0 : CURSOR_SNAPPED);
	return repl - buffer->contents;
}

void
buffer_move_cursor_to_offset(Buffer* buffer, int offset, int do_not_callback)
{
	if (offset > buffer->len-1)
		offset = buffer->len-1;
	char* repl = buffer->contents;
	char* last = repl + offset;
	int x = 0, y = 0;

	char* new_repl;
	while ((new_repl = memchr(repl, '\n', last - repl))) {
		y++;
		repl = new_repl+1;
	}
	for (int charsize; repl < last; repl += charsize) {
		Rune u;
		if(!(charsize = t_decode_utf8_buffer(repl, last - repl, &u)))
			return;
		if (*repl == '\t') {
			if (x == 0)
				x++;
			while (x % tabspaces != 0)
				x++;
			x++;
		} else {
			x += wcwidth(u);
		}
	}

	buffer->x_scroll = buffer->y_scroll = 0;
	buffer_move_cursor(buffer, x, y, (do_not_callback) ? 0 : CURSOR_SNAPPED);
}

int
tsetchar(Rune u, Glyph attr, int x, int y)
{
	if (y >= term.row || x >= term.col ||
		y < 0         || x < 0)
		return 1;

	int width = wcwidth(u);
	if (width == -1)
		width = 1;
	else if (width > 1)
		attr.mode |= ATTR_WIDE;

	if (term.line[y][x].mode & ATTR_WIDE || attr.mode & ATTR_WIDE) {
		if (x+1 < term.col) {
			term.line[y][x+1].u = 0;
			term.line[y][x+1].mode |= ATTR_WDUMMY;
		}
	} else if (term.line[y][x].mode & ATTR_WDUMMY) {
		term.line[y][x-1].u = ' ';
		term.line[y][x-1].mode &= ~ATTR_WIDE;
	}

	term.dirty[y] = 1;
	term.line[y][x] = attr;
	term.line[y][x].u = u;

	return width;
}

void
tclearregion(int x1, int y1, int x2, int y2)
{
	int x, y, temp;

	if (x1 > x2)
		temp = x1, x1 = x2, x2 = temp;
	if (y1 > y2)
		temp = y1, y1 = y2, y2 = temp;

	LIMIT(x1, 0, term.col-1);
	LIMIT(x2, 0, term.col-1);
	LIMIT(y1, 0, term.row-1);
	LIMIT(y2, 0, term.row-1);

	for (y = y1; y <= y2; y++) {
		term.dirty[y] = 1;
		for (x = x1; x <= x2; x++) {
			term.line[y][x].fg = term.c.attr.fg;
			term.line[y][x].bg = term.c.attr.bg;
			term.line[y][x].mode = 0;
			term.line[y][x].u = ' ';
		}
	}
}

void
tresize(int col, int row)
{
	int i;
	int minrow = MIN(row, term.row);
	int mincol = MIN(col, term.col);
	TCursor c;

	if (col < 1 || row < 1) {
		fprintf(stderr,
		        "tresize: error resizing to %dx%d\n", col, row);
		return;
	}

	/*
	 * slide screen to keep cursor where we expect it -
	 * tscrollup would work here, but we can optimize to
	 * memmove because we're freeing the earlier lines
	 */
	for (i = 0; i <= term.c.y - row; i++) {
		free(term.line[i]);
		free(term.alt[i]);
	}
	/* ensure that both src and dst are not NULL */
	if (i > 0) {
		memmove(term.line, term.line + i, row * sizeof(Line));
		memmove(term.alt, term.alt + i, row * sizeof(Line));
	}
	for (i += row; i < term.row; i++) {
		free(term.line[i]);
		free(term.alt[i]);
	}

	/* resize to new height */
	term.line = xrealloc(term.line, row * sizeof(Line));
	term.alt  = xrealloc(term.alt,  row * sizeof(Line));
	term.dirty = xrealloc(term.dirty, row * sizeof(*term.dirty));
	term.tabs = xrealloc(term.tabs, col * sizeof(*term.tabs));

	/* resize each row to new width, zero-pad if needed */
	for (i = 0; i < minrow; i++) {
		term.line[i] = xrealloc(term.line[i], col * sizeof(Glyph));
		term.alt[i]  = xrealloc(term.alt[i],  col * sizeof(Glyph));
	}

	/* allocate any new rows */
	for (/* i = minrow */; i < row; i++) {
		term.line[i] = xmalloc(col * sizeof(Glyph));
		term.alt[i] = xmalloc(col * sizeof(Glyph));
	}
	if (col > term.col) {
		int* bp = term.tabs + term.col;

		memset(bp, 0, sizeof(*term.tabs) * (col - term.col));
		while (--bp > term.tabs && !*bp)
			/* nothing */ ;
		for (bp += tabspaces; bp < term.tabs + col; bp += tabspaces)
			*bp = 1;
	}
	/* update terminal size */
	term.col = col;
	term.row = row;
	/* move cursor */
	term.c.x = MIN(term.c.x, term.col-1);
	term.c.y = MIN(term.c.y, term.row-1);
	if (cursor_movement_callback)
		cursor_movement_callback(term.c.x, term.c.y, CURSOR_WINDOW_RESIZED);
	/* Clearing both screens (it makes dirty all lines) */
	c = term.c;
	for (i = 0; i < 2; i++) {
		if (mincol < col && 0 < minrow) {
			tclearregion(mincol, 0, col - 1, minrow - 1);
		}
		if (0 < col && minrow < row) {
			tclearregion(0, minrow, col - 1, row - 1);
		}
		tswapscreen();
	}
	term.c = c;
}

void
resettitle(void)
{
	xsettitle(NULL);
}

void
drawregion(int x1, int y1, int x2, int y2)
{
	int y;

	for (y = y1; y < y2; y++) {
		if (!term.dirty[y])
			continue;

		term.dirty[y] = 0;
		xdrawline(term.line[y], x1, y, x2);
	}
}

void
draw(void)
{
	if (!xstartdraw())
		return;
	if (term.line[term.c.y][term.c.x].mode & ATTR_WDUMMY)
		term.c.x--;

	drawregion(0, 0, term.col, term.row);
	xdrawcursor(term.c.x, term.c.y, term.line[term.c.y][term.c.x]);

	xfinishdraw();
}

void
redraw(void)
{
	tfulldirt();
	draw();
}
