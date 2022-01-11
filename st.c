/* See LICENSE for license details. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

#include "st.h"
#include "win.h"

/* Arbitrary sizes */
#define UTF_INVALID   0xFFFD
#define UTF_SIZ       4

Term term;
extern struct window_buffer* focused_window;
extern int cursor_x, cursor_y;

static void colour_selection(Glyph* letter);

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
tnew(int col, int row)
{
	term = (Term){0};
	tresize(col, row);
	tsetregion(0, 0, term.col-1, term.row-1, ' ');
}

int
buffer_seek_char(const struct file_buffer* buf, int offset, char byte)
{
	LIMIT(offset, 0, buf->len-1);
	char* new_buf = memchr(buf->contents + offset, byte, buf->len - offset);
	if (!new_buf) return -1;
	return new_buf - buf->contents;
}
int
buffer_seek_char_backwards(const struct file_buffer* buf, int offset, char byte)
{
	LIMIT(offset, 0, buf->len-1);
	for (int n = offset-1; n >= 0; n--) {
		if (buf->contents[n] == byte) {
			return n+1;
		}
	}
	return -1;
}

int
buffer_seek_string(const struct file_buffer* buf, int offset, const char* string)
{
	LIMIT(offset, 0, buf->len-1);
	int str_len = strlen(string);

	for (int n = offset; n < buf->len - str_len; n++) {
		if (!memcmp(buf->contents + n, string, str_len)) {
			printf("n = %d\n", n);
			return n;
		}
	}
	return -1;
}
int
buffer_seek_string_backwards(const struct file_buffer* buf, int offset, const char* string)
{
	int str_len = strlen(string);
	offset += str_len;
	LIMIT(offset, 0, buf->len-1);

	for (int n = offset - str_len; n >= 0; n--) {
		if (!memcmp(buf->contents + n, string, str_len)) {
			printf("n = %d\n", n);
			return n;
		}
	}
	return -1;
}

void
buffer_move_on_line(struct window_buffer* buf, int amount, enum cursor_reason callback_reason)
{
	const struct file_buffer* fb = get_file_buffer((buf));

	if (amount < 0) {
		/*
		 * we cant get the size of a utf8 char backwards
		 * therefore we move all the way to the start of the line,
		 * then a seeker will try to find the cursor pos
		 * the follower will then be *amount* steps behind,
		 * when the seeker reaches the cursor
		 * the follower will be the new cursor position
		 */

		int line_start = buffer_seek_char_backwards(fb, buf->cursor_offset, '\n');
		amount = abs(MAX(line_start - buf->cursor_offset, amount));
		assert(amount < 2048);

		char moves[amount];
		int seek_pos = line_start, follower_pos = line_start;
		int n = 0;
		while (seek_pos < buf->cursor_offset) {
			Rune u;
			int charsize = t_decode_utf8_buffer(fb->contents + seek_pos, fb->len - seek_pos, &u);
			seek_pos += charsize;
			if (n < amount) {
				moves[n++] = charsize;
			} else {
				follower_pos += moves[0];
				memmove(moves, moves + 1, amount - 1);
				moves[amount - 1] = charsize;
			}
		}
		buf->cursor_offset = follower_pos;

		LIMIT(buf->cursor_offset, 0, fb->len-1);
	} else if (amount > 0) {
		for (int charsize = 0;
			 buf->cursor_offset < fb->len && amount > 0 && fb->contents[buf->cursor_offset + charsize] != '\n';
			 buf->cursor_offset += charsize, amount--) {
			Rune u;
			charsize = t_decode_utf8_buffer(fb->contents + buf->cursor_offset, fb->len - buf->cursor_offset, &u);
			if (u != '\n' && u != '\t')
				if (wcwidth(u) <= 0)
					amount++;
			if (buf->cursor_offset + charsize >= fb->len)
				break;
		}
	}

	if (callback_reason && cursor_movement_callback)
		cursor_movement_callback(buf, callback_reason);
}

void
buffer_move_lines(struct window_buffer* buf, int amount, enum cursor_reason callback_reason)
{
	const struct file_buffer* fb = get_file_buffer((buf));
	int offset = buf->cursor_offset;
	if (amount > 0) {
		while (amount-- && offset >= 0)
			offset = buffer_seek_char(fb, offset, '\n')+1;
		if (offset < 0)
			offset = fb->len-1;
	} else if (amount < 0) {
		while (amount++ && offset >= 0)
			offset = buffer_seek_char_backwards(fb, offset, '\n')-1;
	}
	buffer_move_to_offset(buf, offset, callback_reason);
}

void
buffer_move_to_offset(struct window_buffer* buf, int offset, enum cursor_reason callback_reason)
{
	const struct file_buffer* fb = get_file_buffer((buf));
	LIMIT(offset, 0, fb->len-1);
	buf->cursor_offset = offset;

	if (callback_reason && cursor_movement_callback)
		cursor_movement_callback(buf, callback_reason);
}

void
buffer_move_to_x(struct window_buffer* buf, int x, enum cursor_reason callback_reason)
{
	assert(buf);
	struct file_buffer* fb = get_file_buffer(buf);

	int offset = buffer_seek_char_backwards(fb, buf->cursor_offset, '\n');
	if (offset < 0)
		offset = 0;
	buffer_move_to_offset(buf, offset, 0);

	int x_counter = 0;

	while (offset < fb->len) {
		if (fb->contents[offset] == '\t') {
			offset++;
			if (x_counter <= 0) x_counter += 1;
			while (x_counter % tabspaces != 0) x_counter += 1;
			x_counter += 1;
			continue;
		} else if (fb->contents[offset] == '\n') {
			break;
		}
		Rune u = 0;
		int charsize = t_decode_utf8_buffer(fb->contents + offset, fb->len - offset, &u);
		x_counter += wcwidth(u);
		if (x_counter <= x) {
			offset += charsize;
			if (x_counter == x)
				break;
		} else {
			break;
		}
	}
	buffer_move_to_offset(buf, offset, callback_reason);
}

void
window_buffer_split(struct window_split_node* parent, float ratio, enum window_split_mode mode)
{
	assert(parent);
	assert(parent->mode == WINDOW_SINGULAR);
	assert(mode != WINDOW_SINGULAR);

	parent->node1 = xmalloc(sizeof(struct window_split_node));
	parent->node2 = xmalloc(sizeof(struct window_split_node));

	*parent->node1 = *parent;
	*parent->node2 = *parent;
	parent->node1->parent = parent;
	parent->node2->parent = parent;

	// set the new childrens children to NULL for good measure
	parent->node1->node1 = NULL;
	parent->node1->node2 = NULL;
	parent->node2->node1 = NULL;
	parent->node2->node2 = NULL;

	parent->mode = mode;
	parent->ratio = ratio;
	parent->window = (struct window_buffer){0};
}

void
window_write_tree_to_screen(struct window_split_node* root, int minx, int miny, int maxx, int maxy)
{
	assert(root);

	if (root->mode == WINDOW_SINGULAR) {
		buffer_write_to_screen(&root->window, minx, miny, maxx, maxy);
	} else if (IS_HORISONTAL(root->mode)) {
		int middlex = ((float)(maxx - minx) * root->ratio) + minx;

		// print seperator
		tsetregion(middlex+1, miny, middlex+1, maxy, L'│');

		window_write_tree_to_screen(root->node1, minx, miny, middlex, maxy);
		window_write_tree_to_screen(root->node2, middlex+2, miny, maxx, maxy);

		// print connecting borders
		if (middlex+1 >= term.col)
			return;
		if (miny-1 >= 0 && miny-1 < term.row) {
			if (term.line[miny-1][middlex+1].u == L'┬' ||
				term.line[miny-1][middlex+1].u == L'┴' ||
				term.line[miny-1][middlex+1].u == L'├' ||
				term.line[miny-1][middlex+1].u == L'┤')
				tsetchar(L'┼', middlex+1, miny-1);
			if (term.line[miny-1][middlex+1].u == L'─')
				tsetchar(L'┬', middlex+1, miny-1);
		}
		if (maxy+1 >= 0 && maxy+1 < term.row) {
			if (term.line[maxy+1][middlex+1].u == L'┬' ||
				term.line[maxy+1][middlex+1].u == L'┴' ||
				term.line[maxy+1][middlex+1].u == L'├' ||
				term.line[maxy+1][middlex+1].u == L'┤')
				tsetchar(L'┼', middlex+1, maxy+1);
			if (term.line[maxy+1][middlex+1].u == L'─')
				tsetchar(L'┴', middlex+1, maxy+1);
		}
	} else if (root->mode == WINDOW_VERTICAL) {
		int middley = ((float)(maxy - miny) * root->ratio) + miny;

		// print seperator
		tsetregion(minx, middley+1, maxx, middley+1, L'─');

		window_write_tree_to_screen(root->node1, minx, miny, maxx, middley);
		window_write_tree_to_screen(root->node2, minx, middley+2, maxx, maxy);

		// print connecting borders
		if (middley+1 >= term.row)
			return;
		if (minx-1 >= 0 && minx-1 < term.col) {
			if (term.line[middley+1][minx-1].u == L'┬' ||
				term.line[middley+1][minx-1].u == L'┴' ||
				term.line[middley+1][minx-1].u == L'├' ||
				term.line[middley+1][minx-1].u == L'┤')
				tsetchar(L'┼', minx-1, middley+1);
			if (term.line[middley+1][minx-1].u == L'│')
				tsetchar(L'├', minx-1, middley+1);
		}
		if (maxx+1 >= 0 && maxx+1 < term.col) {
			if (term.line[middley+1][maxx+1].u == L'┤')
			if (term.line[middley+1][maxx+1].u == L'┬' ||
				term.line[middley+1][maxx+1].u == L'┴' ||
				term.line[middley+1][maxx+1].u == L'├' ||
				term.line[middley+1][maxx+1].u == L'┤')
				tsetchar(L'┼', maxx+1, middley+1);
			if (term.line[middley+1][maxx+1].u == L'│')
				tsetchar(L'┤', maxx+1, middley+1);
		}
	}
}

int
is_correct_mode(enum window_split_mode mode, enum move_directons move)
{
	if (move == MOVE_RIGHT || move == MOVE_LEFT)
		return IS_HORISONTAL(mode);
	if (move == MOVE_UP || move == MOVE_DOWN)
		return (mode == WINDOW_VERTICAL);
	return 0;
}

struct window_split_node*
window_switch_to_window(struct window_split_node* current, enum move_directons move)
{
	assert(current);
	if (!current->parent) return current;
	assert(current->mode == WINDOW_SINGULAR);
	struct window_split_node* old_current = current;

	if (move == MOVE_RIGHT || move == MOVE_DOWN) {
		// traverse up the tree to the right
		for (; current->parent; current = current->parent) {
			if (is_correct_mode(current->parent->mode, move) && current->parent->node1 == current) {
				// traverse down until a screen is found
				current = current->parent->node2;
				while(current->mode != WINDOW_SINGULAR)
					current = current->node1;

				return current;
			}
		}
	} else if (move == MOVE_LEFT || move == MOVE_UP) {
		// traverse up the tree to the left
		for (; current->parent; current = current->parent) {
			if (is_correct_mode(current->parent->mode, move) && current->parent->node2 == current) {
				// traverse down until a screen is found
				current = current->parent->node1;
				while(current->mode != WINDOW_SINGULAR)
					current = current->node2;

				return current;
			}
		}
	}

	return old_current;
}

struct file_buffer
buffer_new(char* file_path)
{
	if (!file_path) {
		die("creating new buffers not implemented\n");
	}
	struct file_buffer buffer = {0};
	buffer.file_path = file_path;

	FILE *file = fopen(file_path, "rb");
	if (!file) {
		fprintf(stderr, "---error reading file \"%s\"---\n", file_path);
		die("");
		return buffer;
	}

	fseek(file, 0L, SEEK_END);
	long readsize = ftell(file);
	rewind(file);

	assert(readsize);

	if (readsize > (long)1.048576e+7) {
		fclose(file);
		die("you are opening a huge file(>10MiB), not allowed");
	}

	buffer.len = readsize;
	buffer.capacity = readsize + 100;

	buffer.contents = xmalloc(buffer.capacity);

	char bom[4] = {0};
	fread(bom, 1, 3, file);
	if (strcmp(bom, "\xEF\xBB\xBF"))
		rewind(file);
	else
		buffer.mode |= BUFFER_UTF8_SIGNED;
	fread(buffer.contents, 1, readsize, file);
	fclose(file);

	buffer.ub = xmalloc(sizeof(struct undo_buffer) * UNDO_BUFFERS_COUNT);
	memset(buffer.ub, 0, sizeof(struct undo_buffer) * UNDO_BUFFERS_COUNT);

	if (buffer_contents_updated)
		buffer_contents_updated(&buffer, 0, BUFFER_CONTENT_INIT);

	return buffer;
}

void
buffer_insert(struct file_buffer* buf, const char* new_content, const int len, const int offset, int do_not_callback)
{
	assert(buf->contents);
	if (offset > buf->len)
		die("writing past buf %s\n", buf->file_path);

	if (buf->len + len >= buf->capacity) {
		buf->capacity = buf->len + len + 256;
		buf->contents = xrealloc(buf->contents, buf->capacity);
	}
	if (offset < buf->len)
		memmove(buf->contents+offset+len, buf->contents+offset, buf->len-offset);
	buf->len += len;

	memcpy(buf->contents+offset, new_content, len);
	if (buffer_contents_updated && !do_not_callback)
		buffer_contents_updated(buf, offset, BUFFER_CONTENT_NORMAL_EDIT);
}

void
buffer_change(struct file_buffer* buf, const char* new_content, const int len, const int offset, int do_not_callback)
{
	assert(buf->contents);
	if (offset > buf->len)
		die("writing past buf %s\n", buf->file_path);

	if (offset + len > buf->len) {
		buf->len = offset + len;
		if (buf->len >= buf->capacity) {
			buf->capacity = buf->len + len + 256;
			buf->contents = xrealloc(buf->contents, buf->capacity);
		}
	}

	memcpy(buf->contents+offset, new_content, len);
	if (buffer_contents_updated && !do_not_callback)
		buffer_contents_updated(buf, offset, BUFFER_CONTENT_NORMAL_EDIT);
}

void
buffer_remove(struct file_buffer* buf, const int offset, int len, int do_not_calculate_charsize, int do_not_callback)
{
	assert(buf->contents);
	if (offset > buf->len)
		die("deleting past buffer (offset is %d len is %d)\n", offset, buf->len);

	int removed_len = 0;
	if (do_not_calculate_charsize) {
		removed_len = len;
	} else {
		while (len--) {
			int charsize = t_decode_utf8_buffer(buf->contents + offset, buf->len - offset, NULL);
			if (buf->len - charsize < 0)
				return;
			removed_len += charsize;
		}
	}
	buf->len -= removed_len;
	memmove(buf->contents+offset, buf->contents+offset+removed_len, buf->len-offset);
	if (buffer_contents_updated && !do_not_callback)
		buffer_contents_updated(buf, offset, BUFFER_CONTENT_NORMAL_EDIT);
}

int
buffer_get_charsize(Rune u, int cur_x_pos)
{
	if (u == '\t')
		return 8 - (cur_x_pos % tabspaces);
	if (u == '\n')
		return 0;
	return wcwidth(u);

}

void
buffer_offset_to_xy(struct window_buffer* buf, int offset, int maxx, int* cx, int* cy)
{
	assert(buf);
	struct file_buffer* fb = get_file_buffer(buf);

	LIMIT(offset, 0, fb->len-1);
	*cx = *cy = 0;

	char* repl = fb->contents;
	char* last = repl + offset;

	char* new_repl;
	if (WRAP_BUFFER && maxx > 0) {
		int yscroll = 0;
		while ((new_repl = memchr(repl, '\n', last - repl))) {
			if (++yscroll >= buf->y_scroll)
				break;
			repl = new_repl+1;
		}
		*cy = yscroll - buf->y_scroll;
	} else {
		while ((new_repl = memchr(repl, '\n', last - repl))) {
			repl = new_repl+1;
			*cy += 1;
		}
		*cy -= buf->y_scroll;
	}

	while (repl < last) {
#if WRAP_BUFFER
		if (maxx > 0 && (*repl == '\n' || *cx >= maxx)) {
			*cy += 1;
			*cx = 0;
			repl++;
			continue;
		}
#endif // WRAP_BUFFER
		if (*repl == '\t') {
			repl++;
			if (*cx <= 0) *cx += 1;
			while (*cx % tabspaces != 0) *cx += 1;
			*cx += 1;
			continue;
		}
		Rune u;
		repl += t_decode_utf8_buffer(repl, last - repl, &u);
		*cx += wcwidth(u);
	}
}

void
buffer_write_to_screen(struct window_buffer* buf, int minx, int miny, int maxx, int maxy)
{
	assert(buf);
	struct file_buffer* fb = get_file_buffer(buf);

	LIMIT(maxx, 0, term.col-1);
	LIMIT(maxy, 0, term.row-1);
	LIMIT(minx, 0, maxx-1);
	LIMIT(miny, 0, maxy-1);

	int x = minx, y = miny;
	tsetregion(minx, miny, maxx, maxy, ' ');

	// force the screen in a place where the cursor is visable
	int ox, oy;
	buffer_offset_to_xy(buf, buf->cursor_offset, maxx - minx, &ox, &oy);
	ox += minx - maxx;
	int xscroll = 0;
	if (ox > 0)
		xscroll = ox;
	if (oy < 0) {
		buf->y_scroll += oy;
	} else {
		oy += miny - maxy;
		if (oy > 0)
			buf->y_scroll += oy;
	}
	if (buf->y_scroll < 0)
		buf->y_scroll = 0;

	// move to y_scroll
	char* repl = fb->contents;
	char* last = repl + fb->len;
	char* new_repl;
	int line = buf->y_scroll;
	while ((new_repl = memchr(repl, '\n', last - repl))) {
		if (--line < 0)
			break;
		else if (new_repl+1 < last)
			repl = new_repl+1;
		else
			return;
	}

	// actually write to the screen
	int once = 0;
	for (int charsize = 1; repl < last && charsize; repl += charsize) {
		// if the buffer being drawn is focused, set the cursor position global
		if (!once && buf == focused_window && repl - fb->contents >= buf->cursor_offset) {
			once = 1;
			cursor_x = x;
			cursor_y = y;
			LIMIT(cursor_x, minx, maxx);
			LIMIT(cursor_y, miny, maxy);
		}


#if WRAP_BUFFER
		xscroll = 0;
		if (*repl == '\n' || x >= maxx) {
#else
		if (x - xscroll > maxx)
			repl = memchr(repl, '\n', last - repl);

		if (*repl == '\n') {
#endif // WRAP_BUFFER

			x = minx;
			if (++y > maxy)
				break;

#if WRAP_BUFFER
			if (*repl == '\n') {
				charsize = 1;
				continue;
			}
#else
			charsize = 1;
			continue;
#endif // WRAP_BUFFER

		} else if (*repl == '\t') {
			charsize = 1;
			if (x <= 0) {
				x += tsetchar(' ', x - xscroll, y);
				if (x >= maxx)
					continue;
			}
			while (x % tabspaces != 0 && x - xscroll <= maxx)
				x += tsetchar(' ', x - xscroll, y);

			if (x - xscroll <= maxx)
				x += tsetchar(' ', x, y);
			continue;
		}

		Rune u;
		charsize = t_decode_utf8_buffer(repl, last - repl, &u);
		int width;
		if (x - xscroll >= minx)
			width = tsetchar(u, x - xscroll, y);
		else
			width = wcwidth(u);
		x += width;
	}

	buffer_write_selection(buf, minx, miny, maxx, maxy);
}

void
colour_selection(Glyph* letter)
{
	int fg = letter->fg;
	letter->fg = letter->bg;
	letter->bg = fg;
}


int
buffer_is_selection_start_top_left(const struct file_buffer* buf)
{
	return (buf->s1o <= buf->s2o) ? 1 : 0;
}

void
buffer_move_cursor_to_selection_start(struct window_buffer* buf)
{
	const struct file_buffer* fb = get_file_buffer(buf);
	if (buffer_is_selection_start_top_left(fb))
		buffer_move_to_offset(buf, fb->s1o, CURSOR_SNAPPED);
	else
		buffer_move_to_offset(buf, fb->s2o, CURSOR_SNAPPED);
}

void
buffer_write_selection(struct window_buffer* buf, int minx, int miny, int maxx, int maxy)
{
	assert(buf);
	struct file_buffer* fb = get_file_buffer(buf);

	LIMIT(maxx, 0, term.col-1);
	LIMIT(maxy, 0, term.row-1);
	LIMIT(minx, 0, maxx-1);
	LIMIT(miny, 0, maxy-1);

	//TODO: implement alternative selection modes
	if (!(fb->mode & BUFFER_SELECTION_ON))
		return;

	int x, y, x2, y2;
	if (buffer_is_selection_start_top_left(fb)) {
		buffer_offset_to_xy(buf, fb->s1o, maxx - minx, &x, &y);
		buffer_offset_to_xy(buf, fb->s2o, maxx - minx, &x2, &y2);
	} else {
		buffer_offset_to_xy(buf, fb->s2o, maxx - minx, &x, &y);
		buffer_offset_to_xy(buf, fb->s1o, maxx - minx, &x2, &y2);
	}
	x += minx, x2 += minx + 1;
	y += miny, y2 += miny;


	for(; y < y2; y++) {
		for(; x < maxx; x++)
			colour_selection(&term.line[y][x]);
		x = 0;
	}
	for(; x < x2; x++)
		colour_selection(&term.line[y][x]);
}

char* buffer_get_selection(struct file_buffer* buffer, int* selection_len)
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
buffer_remove_selection(struct file_buffer* buffer)
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
		buffer_contents_updated(buffer, start, BUFFER_CONTENT_BIG_CHANGE);
}

void
buffer_write_to_filepath(const struct file_buffer* buffer)
{
	if (!buffer->file_path)
		return;
	assert(buffer->contents);
	FILE* file = fopen(buffer->file_path, "w");
	assert(file);

	if (buffer->mode & BUFFER_UTF8_SIGNED)
		fwrite("\xEF\xBB\xBF", 1, 3, file);
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

	/* process a complete utf8 char */
	charsize = utf8decode(buffer, u, buflen);

	return charsize;
}

int
tsetchar(Rune u, int x, int y)
{
	Glyph attr = default_attributes;
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

	term.line[y][x] = attr;
	term.line[y][x].u = u;

	return width;
}

void
tsetregion(int x1, int y1, int x2, int y2, Rune u)
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
		for (x = x1; x <= x2; x++) {
			term.line[y][x].fg = default_attributes.fg;
			term.line[y][x].bg = default_attributes.bg;
			term.line[y][x].mode = 0;
			term.line[y][x].u = u;
		}
	}
}

void
tresize(int col, int row)
{
	int i;
	int minrow = MIN(row, term.row);
	int mincol = MIN(col, term.col);

	if (col < 1 || row < 1) {
		fprintf(stderr,
		        "tresize: error resizing to %dx%d\n", col, row);
		return;
	}

	/* resize to new height */
	if (row < term.row)
		for (i = row; i < term.row; i++)
			free(term.line[i]);

	term.line = xrealloc(term.line, row * sizeof(Line));

	if (row > term.row)
		for (i = term.row; i < row; i++)
			term.line[i] = NULL;

	/* resize each row to new width, zero-pad if needed */
	for (i = 0; i < row; i++)
		term.line[i] = xrealloc(term.line[i], col * sizeof(Glyph));

	/* update terminal size */
	term.col = col;
	term.row = row;

	/* Clear screen */
	if (mincol < col && 0 < minrow)
		tsetregion(mincol, 0, col - 1, minrow - 1, ' ');
	if (0 < col && minrow < row)
		tsetregion(0, minrow, col - 1, row - 1, ' ');
}


void
draw(int cursor_x, int cursor_y)
{
	LIMIT(cursor_x, 0, term.col-1);
	LIMIT(cursor_y, 0, term.row-1);

	if (!xstartdraw())
		return;
	if (term.line[cursor_y][cursor_x].mode & ATTR_WDUMMY)
		cursor_x--;

	for (int y = 0; y < term.row; y++)
		xdrawline(term.line[y], 0, y, term.col);

	xdrawcursor(cursor_x, cursor_y, term.line[cursor_y][cursor_x]);

	xfinishdraw();
}
