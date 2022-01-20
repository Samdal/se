/* See LICENSE for license details. */

/*
** This file mainly contains the functionality of handling the
** "buffer". There should a good amount of customisation you
** cand do wihtout tuching this file, but adding or changing
** functionality to fit your needs shouldn't be too hard.
*/

#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>

#include "se.h"
#include "x.h"

///////////////////////////////////////////
// config.c variables and globals
//

// default colors
extern Glyph default_attributes;
extern unsigned int alternate_bg_bright;
extern unsigned int alternate_bg_dark;

extern unsigned int cursor_fg;
extern unsigned int cursor_bg;
extern unsigned int mouse_line_bg;

extern unsigned int selection_bg;
extern unsigned int highlight_color;
extern unsigned int path_color;

extern unsigned int error_color;
extern unsigned int warning_color;
extern unsigned int ok_color;

// other
extern unsigned int tabspaces;
extern int wrap_buffer;
extern const struct color_scheme color_schemes[];

// x.c globals
extern struct window_buffer* focused_window;

// se.c globals
Term term;
static Glyph global_attr;
static const uchar utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const uchar utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};

// callbacks
extern void(*cursor_movement_callback)(struct window_buffer*, enum cursor_reason); // buffer, current_pos, reason
extern void(*buffer_contents_updated)(struct file_buffer*, int, enum buffer_content_reason); // modified buffer, current_pos
extern void(*buffer_written_to_screen_callback)(struct window_buffer*, int, int,  int, int, int, int); // drawn buffer, offset start & end, min x & y, max x & y
// TODO: planned callbacks:
// buffer written

/////////////////////////////////////////////
// Internal functions
//

// path must be freed
static void recursive_mkdir(char* path);
static void draw_dir(const char* path, const char* search, int* sel, int minx, int miny, int maxx, int maxy, int focused);
static void color_selection(Glyph* letter);
static void do_color_scheme(struct file_buffer* fb, struct color_scheme cs, int offset);
static int  write_string(const char* string, int y, int minx, int maxx);
static int  str_contains_char(const char* string, char check);
static int  t_decode_utf8_buffer(const char* buffer, const int buflen, Rune* u);
static size_t utf8decode(const char *, Rune *, size_t);
static Rune utf8decodebyte(char, size_t *);
static char utf8encodebyte(Rune, size_t);
static size_t utf8validate(Rune *, size_t);
static int is_correct_mode(enum window_split_mode mode, enum move_directons move);

////////////////////////////////////////////
// function implementations
//

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
	global_attr = default_attributes;

	term = (Term){0};
	tresize(col, row);
	tsetregion(0, 0, term.col-1, term.row-1, ' ');
}


struct window_buffer
window_buffer_new(int buffer_index)
{
	struct window_buffer wb = {0};
	wb.buffer_index = buffer_index;
	if (path_is_folder(get_file_buffer(&wb)->file_path))
		wb.mode = WINDOW_BUFFER_FILE_BROWSER;

	return wb;
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

	for (int n = offset; n < buf->len - str_len; n++)
		if (!memcmp(buf->contents + n, string, str_len))
			return n;
	return -1;
}

int
buffer_seek_string_backwards(const struct file_buffer* buf, int offset, const char* string)
{
	int str_len = strlen(string);
	offset += str_len;
	LIMIT(offset, 0, buf->len-1);

	for (int n = offset - str_len; n >= 0; n--)
		if (!memcmp(buf->contents + n, string, str_len))
			return n;
	return -1;
}

void
buffer_move_on_line(struct window_buffer* buf, int amount, enum cursor_reason callback_reason)
{
	const struct file_buffer* fb = get_file_buffer((buf));
	if (fb->len <= 0)
		return;

	if (amount < 0) {
		/*
		** we cant get the size of a utf8 char backwards
		** therefore we move all the way to the start of the line,
		** then a seeker will try to find the cursor pos
		** the follower will then be *amount* steps behind,
		** when the seeker reaches the cursor
		** the follower will be the new cursor position
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

		LIMIT(buf->cursor_offset, 0, fb->len);
	} else if (amount > 0) {
		for (int charsize = 0;
			 buf->cursor_offset < fb->len && amount > 0 && fb->contents[buf->cursor_offset + charsize] != '\n';
			 buf->cursor_offset += charsize, amount--) {
			Rune u;
			charsize = t_decode_utf8_buffer(fb->contents + buf->cursor_offset, fb->len - buf->cursor_offset, &u);
			if (u != '\n' && u != '\t')
				if (wcwidth(u) <= 0)
					amount++;
			if (buf->cursor_offset + charsize > fb->len)
				break;
		}
	}

	if (callback_reason && cursor_movement_callback)
		cursor_movement_callback(buf, callback_reason);
}

void
buffer_move_offset_relative(struct window_buffer* buf, int amount, enum cursor_reason callback_reason)
{
	//NOTE: this does not check if the character on this offset is the start of a valid utf8 char
	const struct file_buffer* fb = get_file_buffer((buf));
	if (fb->len <= 0)
		return;
	buf->cursor_offset += amount;
	LIMIT(buf->cursor_offset, 0, fb->len);

	if (callback_reason && cursor_movement_callback)
		cursor_movement_callback(buf, callback_reason);
}

void
buffer_move_lines(struct window_buffer* buf, int amount, enum cursor_reason callback_reason)
{
	const struct file_buffer* fb = get_file_buffer((buf));
	if (fb->len <= 0)
		return;
	int offset = buf->cursor_offset;
	if (amount > 0) {
		while (amount-- && offset >= 0) {
			int new_offset = buffer_seek_char(fb, offset, '\n');
			if (new_offset < 0) {
				offset = fb->len;
				break;
			}
			offset = new_offset+1;
		}
	} else if (amount < 0) {
		while (amount++ && offset >= 0)
			offset = buffer_seek_char_backwards(fb, offset, '\n')-1;
	}
	buffer_move_to_offset(buf, offset, callback_reason);
}

void
buffer_move_to_offset(struct window_buffer* buf, int offset, enum cursor_reason callback_reason)
{
	//NOTE: this does not check if the character on this offset is the start of a valid utf8 char
	const struct file_buffer* fb = get_file_buffer((buf));
	if (fb->len <= 0)
		return;
	LIMIT(offset, 0, fb->len);
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
window_node_split(struct window_split_node* parent, float ratio, enum window_split_mode mode)
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

	parent->node1->node1 = NULL;
	parent->node1->node2 = NULL;
	parent->node2->node1 = NULL;
	parent->node2->node2 = NULL;

	parent->mode = mode;
	parent->ratio = ratio;
	parent->window = (struct window_buffer){0};
}

struct window_split_node*
window_node_delete(struct window_split_node* node)
{
	if (!node->parent)
		return node;
	struct window_split_node* old = node;
	node = node->parent;
	struct window_split_node* other = (node->node1 == old) ? node->node2 : node->node1;
	free(old);

	struct window_split_node* parent = node->parent;
	*node = *other;
	if (other->mode != WINDOW_SINGULAR) {
		other->node1->parent = node;
		other->node2->parent = node;
	}
	free(other);
	node->parent = parent;

	return node;
}

void
window_draw_tree_to_screen(struct window_split_node* root, int minx, int miny, int maxx, int maxy)
{
	assert(root);

	if (root->mode == WINDOW_SINGULAR) {
		buffer_draw_to_screen(&root->window, minx, miny, maxx, maxy);
	} else if (root->mode == WINDOW_HORISONTAL) {
		int middlex = ((float)(maxx - minx) * root->ratio) + minx;

		// print seperator
		tsetregion(middlex+1, miny, middlex+1, maxy, L'│');

		window_draw_tree_to_screen(root->node1, minx, miny, middlex, maxy);
		window_draw_tree_to_screen(root->node2, middlex+2, miny, maxx, maxy);

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
		for (int y = miny; y < maxy+1; y++)
			xdrawline(term.line[y], middlex+1, y, middlex+2);
	} else if (root->mode == WINDOW_VERTICAL) {
		int middley = ((float)(maxy - miny) * root->ratio) + miny;

		// print seperator
		tsetregion(minx, middley+1, maxx, middley+1, L'─');
		//write_string(get_file_buffer(&root->window)->file_path, middley+1, minx, maxx);

		window_draw_tree_to_screen(root->node1, minx, miny, maxx, middley);
		window_draw_tree_to_screen(root->node2, minx, middley+2, maxx, maxy);

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
		for (int y = middley+1; y < middley+2; y++)
			xdrawline(term.line[y], minx, y, maxx+1);
	}
}

void
window_move_all_cursors_on_same_buf(struct window_split_node* root, struct window_split_node* excluded, int buf_index, int offset,
									void(movement)(struct window_buffer*, int, enum cursor_reason),
									int move, enum cursor_reason reason)
{
	if (root->mode == WINDOW_SINGULAR) {
		if (root->window.buffer_index == buf_index && root->window.cursor_offset >= offset && root != excluded)
			movement(&root->window, move, reason);
	} else {
		window_move_all_cursors_on_same_buf(root->node1, excluded, buf_index, offset, movement, move, reason);
		window_move_all_cursors_on_same_buf(root->node2, excluded, buf_index, offset, movement, move, reason);
	}
}

void
window_move_all_yscrolls(struct window_split_node* root, struct window_split_node* excluded, int buf_index, int offset, int move)
{
	if (root->mode == WINDOW_SINGULAR) {
		if (root->window.buffer_index == buf_index && root->window.cursor_offset >= offset && root != excluded)
			root->window.y_scroll += move;
	} else {
		window_move_all_yscrolls(root->node1, excluded, buf_index, offset, move);
		window_move_all_yscrolls(root->node2, excluded, buf_index, offset, move);
	}
}

int
window_other_nodes_contain_file_buffer(struct window_split_node* node, struct window_split_node* root)
{
	if (root->mode == WINDOW_SINGULAR)
		return (root->window.buffer_index == node->window.buffer_index && root != node);

	return (window_other_nodes_contain_file_buffer(node, root->node1) ||
			window_other_nodes_contain_file_buffer(node, root->node2));
}

int
is_correct_mode(enum window_split_mode mode, enum move_directons move)
{
	if (move == MOVE_RIGHT || move == MOVE_LEFT)
		return (mode == WINDOW_HORISONTAL);
	if (move == MOVE_UP || move == MOVE_DOWN)
		return (mode == WINDOW_VERTICAL);
	return 0;
}

struct window_split_node*
window_switch_to_window(struct window_split_node* node, enum move_directons move)
{
	assert(node);
	if (!node->parent) return node;
	assert(node->mode == WINDOW_SINGULAR);
	struct window_split_node* old_node = node;

	if (move == MOVE_RIGHT || move == MOVE_DOWN) {
		// traverse up the tree to the right
		for (; node->parent; node = node->parent) {
			if (is_correct_mode(node->parent->mode, move) && node->parent->node1 == node) {
				// traverse down until a screen is found
				node = node->parent->node2;
				while(node->mode != WINDOW_SINGULAR)
					node = node->node1;

				return node;
			}
		}
	} else if (move == MOVE_LEFT || move == MOVE_UP) {
		// traverse up the tree to the left
		for (; node->parent; node = node->parent) {
			if (is_correct_mode(node->parent->mode, move) && node->parent->node2 == node) {
				// traverse down until a screen is found
				node = node->parent->node1;
				while(node->mode != WINDOW_SINGULAR)
					node = node->node2;

				return node;
			}
		}
	}

	return old_node;
}

void
window_node_resize(struct window_split_node* node, enum move_directons move)
{
	for (; node; node = node->parent) {
		if (is_correct_mode(node->mode, move)) {
			float amount = (move == MOVE_RIGHT || move == MOVE_LEFT) ? 0.1f : 0.05f;
			if (move == MOVE_RIGHT || move == MOVE_DOWN) amount = -amount;
			node->ratio -= amount;
			LIMIT(node->ratio, 0.001f, 0.95f);
			return;
		}
	}
}

int
path_is_folder(const char* path) {
   struct stat statbuf;
   if (stat(path, &statbuf) != 0)
       return 0;
   return S_ISDIR(statbuf.st_mode);
}

// folder and file must be freed
char*
file_path_get_path(const char* path)
{
	assert(path);

	const char* folder_start = strrchr(path, '/');
	if (!folder_start)
		folder_start = path;
	else
		folder_start++;
	int folder_len = folder_start - path;
	char* folder = xmalloc(folder_len + 1);

	memcpy(folder, path, folder_len);
	folder[folder_len] = '\0';

	return folder;
}

int
file_browser_next_item(DIR* dir, const char* path, const char* search, char* full_path, char* filename)
{
	assert(path);
	assert(dir);
	assert(strlen(path) < PATH_MAX+1);
	int len = strlen(search);

    struct dirent *folder;
    while((folder = readdir(dir))) {
		strcpy(filename, folder->d_name);
		strcpy(full_path, path);
		strcat(full_path, filename);
		if (path_is_folder(full_path))
			strcat(filename, "/");

		if (memcmp(filename, search, len) == 0) {
			if (search[0] != '.' && folder->d_name[0] == '.')
				continue;
			if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0)
				continue;
			return 1;
		}
	}
	*filename = *full_path = 0;
	return 0;
}

void
draw_dir(const char* path, const char* search, int* sel, int minx, int miny, int maxx, int maxy, int focused)
{
	static char full_path[PATH_MAX];
	static char filename[PATH_MAX];
	assert(path);
	assert(sel);

	global_attr.bg = alternate_bg_dark;
	tsetregion(minx, miny+1, maxx, maxy, ' ');
	global_attr = default_attributes;

	int len = strlen(search);
    DIR *dir = opendir(path);

	int folder_lines = maxy - miny - 1;
	int folders = 0;
    while(file_browser_next_item(dir, path, search, full_path, filename))
		folders++;

	rewinddir(dir);
	*sel = MIN(*sel, folders-1);
	int sel_local = *sel;
	char count[256];
	if (sel_local > folder_lines)
		snprintf(count, 256, "^[%2d] ", folders);
	else if (folders > folder_lines)
		snprintf(count, 256, "ˇ[%2d] ", folders);
	else
		snprintf(count, 256, " [%2d] ", folders);

	global_attr.fg = path_color;
	int new_x = write_string(count, miny, minx, maxx+1);
	new_x = write_string(path, miny, new_x, maxx+1);

	global_attr = default_attributes;
	new_x = write_string(search, miny, new_x, maxx+1);

	global_attr = default_attributes;
	global_attr.bg = alternate_bg_dark;

	int start_miny = miny;
	folders--;
	miny++;
    while(miny < maxy && file_browser_next_item(dir, path, search, full_path, filename)) {
		if (path_is_folder(full_path))
			global_attr.fg = path_color;
		else
			global_attr.fg = default_attributes.fg;

		if (folders > folder_lines && sel_local > folder_lines) {
			folders--;
			sel_local--;
			continue;
		}

		write_string(filename, miny, minx, maxx+1);
		for (int i = minx; i < minx + len; i++)
			term.line[miny][i].fg = highlight_color;
		if (miny - start_miny - 1 == sel_local)
			for (int i = minx; i < maxx+1; i++)
				term.line[miny][i].bg = selection_bg;
		miny++;
	}
	miny = MIN(miny, maxy);
	closedir(dir);


	if (folders < 0) {
		global_attr = default_attributes;
		if (search[strlen(search)-1] == '/')
			global_attr.fg = error_color;
		else
			global_attr.fg = warning_color;
		write_string("  [Create New File]", start_miny, new_x, maxx+1);
	}

	for (int y = start_miny; y < maxy+1; y++)
		xdrawline(term.line[y], minx, y, maxx+1);

	xdrawcursor(new_x, start_miny, focused);

	global_attr = default_attributes;
}

void recursive_mkdir(char *path) {
    if (!path || !strlen(path))
        return;
    char *sep = strrchr(path, '/');
    if(sep) {
        *sep = '\0';
        recursive_mkdir(path);
        *sep = '/';
    }
    if(mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO) && errno != EEXIST)
        fprintf(stderr, "error while trying to create '%s'\n%s\n", path, strerror(errno));
}

struct file_buffer
buffer_new(const char* file_path)
{
	struct file_buffer buffer = {0};
	buffer.file_path = xmalloc(PATH_MAX);

	char* res = realpath(file_path, buffer.file_path);
	if (!res) {
		char* path = file_path_get_path(file_path);
		recursive_mkdir(path);
		free(path);

		FILE *new_file = fopen(file_path, "wb");
		fclose(new_file);

		realpath(file_path, buffer.file_path);
	}

	if (path_is_folder(buffer.file_path)) {
		int len = strlen(buffer.file_path);
		if (buffer.file_path[len-1] != '/' && len < PATH_MAX-1) {
			buffer.file_path[len] = '/';
			buffer.file_path[len+1] = '\0';
		}
		buffer.len = 0;
		buffer.capacity = 100;
		buffer.contents = xmalloc(buffer.capacity);
	} else {
		FILE *file = fopen(buffer.file_path, "rb");
		fseek(file, 0L, SEEK_END);
		long readsize = ftell(file);
		rewind(file);

		if (readsize > (long)1.048576e+7) {
			fclose(file);
			die("you are opening a huge file(>10MiB), not allowed");
		}

		buffer.len = readsize;
		buffer.capacity = readsize + 100;

		buffer.contents = xmalloc(buffer.capacity);
		buffer.contents[0] = 0;

		char bom[4] = {0};
		fread(bom, 1, 3, file);
		if (strcmp(bom, "\xEF\xBB\xBF"))
			rewind(file);
		else
			buffer.mode |= BUFFER_UTF8_SIGNED;
		fread(buffer.contents, 1, readsize, file);
		fclose(file);
	}

	buffer.ub = xmalloc(sizeof(struct undo_buffer) * UNDO_BUFFERS_COUNT);
	memset(buffer.ub, 0, sizeof(struct undo_buffer) * UNDO_BUFFERS_COUNT);

	if (buffer_contents_updated)
		buffer_contents_updated(&buffer, 0, BUFFER_CONTENT_INIT);

	return buffer;
}

void
buffer_destroy(struct file_buffer* fb)
{
	free(fb->ub);
	free(fb->contents);
	free(fb->file_path);
	*fb = (struct file_buffer){0};
}

void
buffer_insert(struct file_buffer* buf, const char* new_content, const int len, const int offset, int do_not_callback)
{
	assert(buf->contents);
	if (offset > buf->len || offset < 0) {
		fprintf(stderr, "writing past buf %s\n", buf->file_path);
		return;
	}

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

int
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
				return 0;
			removed_len += charsize;
		}
	}
	buf->len -= removed_len;
	memmove(buf->contents+offset, buf->contents+offset+removed_len, buf->len-offset);
	if (buffer_contents_updated && !do_not_callback)
		buffer_contents_updated(buf, offset, BUFFER_CONTENT_NORMAL_EDIT);
	return removed_len;
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

	*cx = *cy = 0;
	if (fb->len <= 0)
		return;
	LIMIT(offset, 0, fb->len);

	char* repl = fb->contents;
	char* last = repl + offset;

	char* new_repl;
	if (wrap_buffer && maxx > 0) {
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
		if (wrap_buffer && maxx > 0 && (*repl == '\n' || *cx >= maxx)) {
			*cy += 1;
			*cx = 0;
			repl++;
			continue;
		}
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
buffer_draw_to_screen(struct window_buffer* buf, int minx, int miny, int maxx, int maxy)
{
	assert(buf);
	struct file_buffer* fb = get_file_buffer(buf);

	LIMIT(maxx, 0, term.col-1);
	LIMIT(maxy, 0, term.row-1);
	LIMIT(minx, 0, maxx-1);
	LIMIT(miny, 0, maxy-1);
	LIMIT(buf->cursor_offset, 0, fb->len);
	tsetregion(minx, miny, maxx, maxy, ' ');

	if (buf->mode == WINDOW_BUFFER_FILE_BROWSER) {
		char* folder = file_path_get_path(fb->file_path);

		buffer_change(fb, "\0", 1, fb->len, 1);
		if (fb->len > 0) fb->len--;

		draw_dir(folder, fb->contents, &buf->y_scroll, minx, miny, maxx, maxy, buf == focused_window);

		free(folder);
		return;
	}

	int color_scheme_available = -1;
	for (int i = 0; color_schemes[i].file_ending; i++) {
		if (is_file_type(fb->file_path, color_schemes[i].file_ending)) {
			color_scheme_available = i;
			break;
		}
	}

	int x = minx, y = miny;
	global_attr = default_attributes;
	do_color_scheme(NULL, (struct color_scheme){0}, 0);

	// force the screen in a place where the cursor is visable
	int ox, oy;
	buffer_offset_to_xy(buf, buf->cursor_offset, maxx - minx, &ox, &oy);
	ox += minx - (maxx-3);
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

	if (wrap_buffer)
		xscroll = 0;

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
	int offset_start = repl - fb->contents;
	int cursor_x = 0, cursor_y = 0;

	// search backwards to find multi-line syntax highlighting
	if (color_scheme_available >= 0) {
		for (int i = 0; i < color_schemes[color_scheme_available].entry_count; i++) {
			const struct color_scheme_entry cs = color_schemes[color_scheme_available].entries[i];
			if (cs.mode == COLOR_AROUND || cs.mode == COLOR_INSIDE) {
				int offset = 0;
				int count = 0;
				int start_len = strlen(cs.arg.start);
				while((offset = buffer_seek_string(fb, offset, cs.arg.start)) >= 0) {
					offset += start_len;
					if (offset >= offset_start)
						break;
					count++;
				}

				if (strcmp(cs.arg.start, cs.arg.end) != 0) {
					int end_len = strlen(cs.arg.end);
					offset = 0;
					while((offset = buffer_seek_string(fb, offset, cs.arg.end)) >= 0) {
						offset += end_len;
						if (offset >= offset_start)
							break;
						count--;
					}
				}
				if (count > 0) {
					offset = buffer_seek_string_backwards(fb, offset_start, cs.arg.start);
					do_color_scheme(fb, color_schemes[color_scheme_available], offset);
					break;
				}
			}
		}
	}

	// actually write to the screen
	int once = 0;
	for (int charsize = 1; repl < last && charsize; repl += charsize) {
		if (!once && repl - fb->contents >= buf->cursor_offset) {
			// if the buffer being drawn is focused, set the cursor position global
			once = 1;
			cursor_x = x - xscroll;
			cursor_y = y;
			LIMIT(cursor_x, minx, maxx);
			LIMIT(cursor_y, miny, maxy);
		}

		if (color_scheme_available >= 0)
			do_color_scheme(fb, color_schemes[color_scheme_available], repl - fb->contents);

		if (!wrap_buffer && x - xscroll > maxx && *repl != '\n') {
			charsize = 1;
			x++;
			continue;
		}

		if (*repl == '\n' || (wrap_buffer && x >= maxx)) {
			x = minx;
			if (++y > maxy)
				break;
			if (wrap_buffer && *repl != '\n')
				continue;
			charsize = 1;
			continue;
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
	int offset_end = repl - fb->contents;

	if (buf->cursor_offset >= fb->len) {
		cursor_x = x - xscroll;
		cursor_y = y;
	}

	if(buffer_written_to_screen_callback)
		buffer_written_to_screen_callback(buf, offset_start, offset_end, minx, miny, maxx, maxy);

	if (buf == focused_window)
		for (int i = minx; i < maxx+1; i++)
			term.line[cursor_y][i].bg = mouse_line_bg;

	buffer_write_selection(buf, minx, miny, maxx, maxy);
	do_color_scheme(NULL, (struct color_scheme){0}, 0);

	for (int i = miny; i < maxy+1; i++)
		xdrawline(term.line[i], minx, i, maxx+1);

	xdrawcursor(cursor_x, cursor_y, buf == focused_window);
}

void
do_color_scheme(struct file_buffer* fb, struct color_scheme cs, int offset)
{
	static int end_at_whitespace = 0;
	static const char* end_condition;
	static int end_condition_len;
	static Glyph next_word_attr;
	static int color_next_word = 0;
	static int around = 0;

	if (!fb) {
		// reset
		end_at_whitespace = 0;
		end_condition_len = 0;
		around = 0;
		color_next_word = 0;
		end_condition = NULL;
		global_attr = default_attributes;
		return;
	}

	char* buf = fb->contents;
	int buflen = fb->len;

	if (end_condition && !color_next_word) {
		if (buflen - offset <= end_condition_len
			|| (offset-1 >= 0 && buf[offset-1] == '\\'))
			return;
		if (end_at_whitespace && buf[offset] == '\n') {
			// *_TO_LINE reached end of line
			end_condition_len = 0;
			end_condition = NULL;
			end_at_whitespace = 0;
			global_attr = default_attributes;
		} else if (memcmp(buf + offset, end_condition, end_condition_len) == 0) {
			// end word mathces
			if (isspace(end_condition[end_condition_len-1])) {
				end_condition_len--;
				if (end_condition_len <= 0)
					global_attr = default_attributes;
			}
			// if it's around not inside, don't reset color
			if (around)
				around = 0;
			 else
				global_attr = default_attributes;

			end_condition = NULL;
			end_at_whitespace = 0;
		}
		return;
	} else if (end_at_whitespace) {
		if (str_contains_char(cs.word_seperators, buf[offset])) {
			end_at_whitespace = 0;
			global_attr = default_attributes;
		} else {
			return;
		}
	} else if (color_next_word) {
		// check if new word encountered
		if (str_contains_char(cs.word_seperators, buf[offset]))
			return;
		global_attr = next_word_attr;
		color_next_word = 0;
		end_at_whitespace = 1;
		return;
	} else if (end_condition_len > 0) {
        // wait for the word/sequence to finish
        // NOTE: does not work with utf8 chars
        if (--end_condition_len <= 0)
			global_attr = default_attributes;
		else
			return;
	}

	for (int i = 0; i < cs.entry_count; i++) {
		struct color_scheme_entry entry = cs.entries[i];
		enum color_scheme_mode mode = entry.mode;

		if (mode == COLOR_UPPER_CASE_WORD) {
			// check if this is a new word
			if (str_contains_char(cs.word_seperators, buf[offset])) continue;

			// check if it's upper case
			int end_len = 0;
			while (offset + end_len < fb->len && !str_contains_char(cs.word_seperators, buf[offset + end_len])) {
				if (!isupper(buf[offset + end_len]) && buf[offset + end_len] != '_'
					&& (!end_len || (buf[offset + end_len] < '0' || buf[offset + end_len] > '9')))
					goto not_upper_case;
				end_len++;
			}
			// upper case words must be longer than x chars
			if (end_len < 3) continue;

			global_attr = entry.attr;
			end_condition_len = end_len;
			return;

		not_upper_case:
			continue;
		}

		int len = strlen(entry.arg.start);

		if (mode == COLOR_WORD_BEFORE_STR || mode == COLOR_WORD_BEFORE_STR_STR || mode == COLOR_WORD_ENDING_WITH_STR) {
			// check if this is a new word
			if (str_contains_char(cs.word_seperators, buf[offset])) continue;

			int offset_tmp = offset;
			// find new word twice if it's BEFORE_STR_STR
			int times = mode == COLOR_WORD_BEFORE_STR_STR ? 2 : 1;
			int first_word_len = 0;
			int first_time = 1;
			while (times--) {
				// seek end of word
				int chars = 0;
				while (offset_tmp < fb->len && !str_contains_char(cs.word_seperators, buf[offset_tmp])) {
					if (buf[offset_tmp] != '*')
						chars++;
					offset_tmp++;
				}
				if (!chars && mode == COLOR_WORD_BEFORE_STR_STR)
					goto exit_word_before_str_str;
				if (first_time)
					first_word_len = chars;

				// seek start of word
				if (mode != COLOR_WORD_ENDING_WITH_STR) {
					int whiespaces = 0;
					while (offset_tmp < fb->len && (isspace(buf[offset_tmp]) || buf[offset_tmp] == '*')) {
						offset_tmp++;
						whiespaces++;
					}
				}
				first_time = 0;
			}
			if (mode == COLOR_WORD_ENDING_WITH_STR) {
				offset_tmp -= len;
				if (offset_tmp < 0)
					continue;
			}
			// check if string matches
			if (memcmp(buf + offset_tmp, entry.arg.start, len) == 0) {
				global_attr = entry.attr;
				end_condition_len = first_word_len;
				return;
			}
		exit_word_before_str_str:
			continue;
		}

		if (mode == COLOR_INSIDE || mode == COLOR_INSIDE_TO_LINE || mode == COLOR_WORD_INSIDE) {
			if (offset - len < 0)
				continue;
			// check the if what's behind the cursor is the first string
			if (memcmp(buf + offset - len, entry.arg.start, len) == 0) {
				assert(entry.arg.end);
				int end_len = strlen(entry.arg.end);
				if (offset < fb->len && memcmp(buf + offset, entry.arg.end, end_len) == 0)
					continue;

				if (mode == COLOR_WORD_INSIDE) {
					// verify that only one word exists inside
					int offset_tmp = offset;
					while (offset_tmp < fb->len && isspace(buf[offset_tmp])) offset_tmp++;
					while (offset_tmp < fb->len && !str_contains_char(cs.word_seperators, buf[offset_tmp])) offset_tmp++;
					while (offset_tmp < fb->len && isspace(buf[offset_tmp])) offset_tmp++;
					if (memcmp(buf + offset_tmp, entry.arg.end, end_len) != 0
						|| offset_tmp - offset <= 1)
						continue;
				}

				end_condition = entry.arg.end;
				end_condition_len = end_len;
				global_attr = entry.attr;
				around = 0;
				if (entry.mode == COLOR_INSIDE_TO_LINE)
					end_at_whitespace = 1;
				return;
			}
			continue;
		}

		// the rest of the conditions all check if the first string matches
		if (buflen - offset <= len)
			continue;
		if (memcmp(buf + offset, entry.arg.start, len) == 0) {
			if (mode == COLOR_AROUND || mode == COLOR_AROUND_TO_LINE) {
				assert(entry.arg.end);
				end_condition = entry.arg.end;
				end_condition_len = strlen(entry.arg.end);
				around = 1;
				if (entry.mode == COLOR_AROUND_TO_LINE)
					end_at_whitespace = 1;
			} else if (mode == COLOR_WORD || mode == COLOR_STR_AFTER_WORD ||
					   mode == COLOR_WORD_STR || mode == COLOR_WORD_STARTING_WITH_STR) {

				// check if this is the start of a new word that matches word exactly(except for WORD_STARTING_WITH_STR)
				if ((offset > 0 && !str_contains_char(cs.word_seperators, buf[offset-1])) ||
					(buflen - (offset+len) > len && !str_contains_char(cs.word_seperators, buf[offset+len])
						&& mode != COLOR_WORD_STARTING_WITH_STR))
					continue;

				if (mode == COLOR_WORD_STR) {
					assert(entry.arg.end);
					int offset_tmp = offset + len;
					// move to next string
					while (offset_tmp < fb->len && isspace(fb->contents[offset_tmp]))
						offset_tmp++;

					int end_len = strlen(entry.arg.end);
					if (offset_tmp + end_len >= fb->len ||
						memcmp(buf + offset_tmp, entry.arg.end, end_len) != 0)
						continue;
					end_condition_len = offset_tmp - offset;
				} else {
					end_at_whitespace = 1;
				}
				if (mode == COLOR_STR_AFTER_WORD) {
					next_word_attr = entry.attr;
					color_next_word = 1;
					continue;
				}
			} else if (mode == COLOR_STR) {
				end_condition_len = len;
			}

			global_attr= entry.attr;
			return;
		}
	}
}


int
write_string(const char* string, int y, int minx, int maxx)
{
	LIMIT(maxx, 0, term.col);
	LIMIT(minx, 0, maxx-1);

	int offset = 0;
	int len = strlen(string);
	while(minx < maxx && offset < len) {
		Rune u;
		int charsize = t_decode_utf8_buffer(string + offset, len - offset, &u);
		offset += charsize;
		minx += tsetchar(u, minx, y);
	}
	return minx;
}

int
str_contains_char(const char* string, char check)
{
	int len = strlen(string);
	for (int i = 0; i < len; i++)
		if (string[i] == check)
			return 1;
	return 0;
}

void
color_selection(Glyph* letter)
{
	letter->bg = selection_bg;
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
			color_selection(&term.line[y][x]);
		x = 0;
	}
	for(; x < x2; x++)
		color_selection(&term.line[y][x]);
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
	Glyph attr = global_attr;
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

	assert(term.line);
	assert(term.line[0]);

	for (y = y1; y <= y2; y++) {
		for (x = x1; x <= x2; x++) {
			term.line[y][x] = global_attr;
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

int
is_file_type(const char* file_path, const char* file_type)
{
	int ftlen = strlen(file_type);
	int offset = strlen(file_path) - ftlen;
	if(offset > 0 && memcmp(file_path + offset, file_type, ftlen) == 0)
		return 1;
	return 0;
}
