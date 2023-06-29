#include "buffer.h"

#include "config.h"
#include "se.h"
#include "extension.h"

#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

// TODO: mark buffers as dirty and only redraw windows that have been changed

////////////////////////////////////////////////
// Globals
//

static char root_node_search[SEARCH_TERM_MAX_LEN];
struct window_split_node root_node = {.mode = WINDOW_SINGULAR, .search = root_node_search};
struct window_split_node* focused_node = &root_node;
struct window_buffer* focused_window = &root_node.wb;

static struct file_buffer* file_buffers;
static int available_buffer_slots = 0;

/////////////////////////////////////////////////
// Function implementations
//

////////////////////////////////////////////////
// File buffer
//

static void
recursive_mkdir(char *path) {
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


// TODO: file open callback, implement as plugin
static int open_seproj(struct file_buffer fb);
int
open_seproj(struct file_buffer fb)
{
		int first = -1;

		char* path = file_path_get_path(fb.file_path);
		chdir(path);
		int offset = -1;

		while((offset = fb_seek_char(&fb, offset+1, ' ')) >= 0)
				fb_change(&fb, "\n", 1, offset, 0);

		offset = -1;
		while((offset = fb_seek_char(&fb, offset+1, '\n')) >= 0) {
				char* line = fb_get_line_at_offset(&fb, offset);
				if (strlen(line) && !is_file_type(line, ".seproj")) {
						if (first < 0)
								first = fb_new_entry(line);
						else
								fb_new_entry(line);
				}
				free(line);
		}

		if (first < 0)
				first = fb_new_entry(NULL);
		writef_to_status_bar("opened project %s", path);
		free(path);

		fb_destroy(&fb);
		return first;
}

void
fb_write_to_filepath(struct file_buffer* fb)
{
		if (!fb->file_path)
				return;
		soft_assert(fb->contents, return;);
		FILE* file = fopen(fb->file_path, "w");
		soft_assert(file, return;);

		if (fb->mode & FB_UTF8_SIGNED)
				fwrite("\xEF\xBB\xBF", 1, 3, file);
		fwrite(fb->contents, sizeof(char), fb->len, file);
		writef_to_status_bar("saved buffer to %s", fb->file_path);

		fclose(file);
		call_extension(fb_written_to_file, fb);
}


int
destroy_fb_entry(struct window_split_node* node, struct window_split_node* root)
{
		// do not allow deletion of the lst file buffer
		int n = 0;
		for(; n < available_buffer_slots; n++)
				if (file_buffers[n].contents && n != node->wb.fb_index)
						break;
		if (n >= available_buffer_slots) {
				writef_to_status_bar("can't delete last buffer");
				return 0;
		}

		if (window_other_nodes_contain_fb(node, root)) {
				node->wb.fb_index++;
				node->wb = wb_new(node->wb.fb_index);
				writef_to_status_bar("swapped buffer");
				return 0;
		}
		fb_destroy(get_fb(&node->wb));

		node->wb = wb_new(node->wb.fb_index);

		return 1;
}



struct file_buffer*
get_fb(struct window_buffer* wb)
{
		soft_assert(wb, wb = focused_window;);
		soft_assert(file_buffers, fb_new_entry(NULL););

		if (wb->fb_index < 0)
				wb->fb_index = available_buffer_slots-1;
		else if (wb->fb_index >= available_buffer_slots)
				wb->fb_index = 0;

		if (!file_buffers[wb->fb_index].contents) {
				for(int n = wb->fb_index; n < available_buffer_slots; n++) {
						if (file_buffers[n].contents) {
								wb->fb_index = n;
								return &file_buffers[n];
						}
				}
				for(int n = 0; n < available_buffer_slots; n++) {
						if (file_buffers[n].contents) {
								wb->fb_index = n;
								return &file_buffers[n];
						}
				}
		} else {
				soft_assert(file_buffers[wb->fb_index].contents, );
				return &file_buffers[wb->fb_index];
		}

		wb->fb_index = fb_new_entry(NULL);
		writef_to_status_bar("all buffers were somehow deleted, creating new one");
		status_bar_bg = warning_color;
		return get_fb(wb);
}

int
fb_delete_selection(struct file_buffer* fb)
{
		if (fb->mode & FB_SELECTION_ON) {
				fb_remove_selection(fb);
				wb_move_cursor_to_selection_start(focused_window);
				fb->mode &= ~(FB_SELECTION_ON);
				return 1;
		}
		return 0;
}

struct file_buffer
fb_new(const char* file_path)
{
		struct file_buffer fb = {0};
		fb.file_path = xmalloc(PATH_MAX);

		char* res = realpath(file_path, fb.file_path);
		if (!res) {
				char* path = file_path_get_path(file_path);
				recursive_mkdir(path);
				free(path);

				FILE *new_file = fopen(file_path, "wb");
				fclose(new_file);
				realpath(file_path, fb.file_path);
				remove(file_path);

				writef_to_status_bar("opened new file %s", fb.file_path);
		} else if (path_is_folder(fb.file_path)) {
				int len = strlen(fb.file_path);
				if (fb.file_path[len-1] != '/' && len < PATH_MAX-1) {
						fb.file_path[len] = '/';
						fb.file_path[len+1] = '\0';
				}
		} else {
				FILE *file = fopen(fb.file_path, "rb");
				if (file) {
						fseek(file, 0L, SEEK_END);
						long readsize = ftell(file);
						rewind(file);

						if (readsize > (long)1.048576e+7) {
								fclose(file);
								die("you are opening a huge file(>10MiB), not allowed");
								return fb;
								// TODO: don't crash
						}

						fb.len = readsize;
						fb.capacity = readsize + 100;

						fb.contents = xmalloc(fb.capacity);
						fb.contents[0] = 0;

						char bom[4] = {0};
						fread(bom, 1, 3, file);
						if (strcmp(bom, "\xEF\xBB\xBF"))
								rewind(file);
						else
								fb.mode |= FB_UTF8_SIGNED;
						fread(fb.contents, 1, readsize, file);
						fclose(file);

						fb.syntax_index = -1;
				}
		}

		if (!fb.capacity)
				fb.capacity = 100;
		if (!fb.contents) {
				fb.contents = xmalloc(fb.capacity);
				memset(fb.contents, 0, fb.capacity);
		}
		fb.ub = xmalloc(sizeof(struct undo_buffer) * UNDO_BUFFERS_COUNT);
		fb.search_term = xmalloc(SEARCH_TERM_MAX_LEN);
		fb.non_blocking_search_term = xmalloc(SEARCH_TERM_MAX_LEN);
		memset(fb.ub, 0, sizeof(struct undo_buffer) * UNDO_BUFFERS_COUNT);
		memset(fb.search_term, 0, SEARCH_TERM_MAX_LEN);
		memset(fb.non_blocking_search_term, 0, SEARCH_TERM_MAX_LEN);
		fb.indent_len = default_indent_len;

		// change line endings
		int offset = 0;
		while((offset = fb_seek_string(&fb, offset, "\r\n")) >= 0)
				fb_remove(&fb, offset, 1, 1, 1);
		offset = 0;
		while((offset = fb_seek_char(&fb, offset, '\r')) >= 0)
				fb_change(&fb, "\n", 1, offset, 1);

		call_extension(fb_new_file_opened, &fb);

		call_extension(fb_contents_updated, &fb, 0, FB_CONTENT_INIT);

		if (res)
				writef_to_status_bar("new fb %s", fb.file_path);
		return fb;
}

int
fb_new_entry(const char* file_path)
{
		static char full_path[PATH_MAX];
		if (!file_path)
				file_path = "./";
		soft_assert(strlen(file_path) < PATH_MAX, file_path = "./";);

		char* res = realpath(file_path, full_path);

		if (available_buffer_slots) {
				if (res) {
						for(int n = 0; n < available_buffer_slots; n++) {
								if (file_buffers[n].contents) {
										if (strcmp(file_buffers[n].file_path, full_path) == 0) {
												writef_to_status_bar("buffer exits");
												return n;
										}
								}
						}
				} else {
						strcpy(full_path, file_path);
				}

				for(int n = 0; n < available_buffer_slots; n++) {
						if (!file_buffers[n].contents) {
								if (is_file_type(full_path, ".seproj"))
										return open_seproj(fb_new(full_path));
								file_buffers[n] = fb_new(full_path);
								return n;
						}
				}
		}

		if (is_file_type(full_path, ".seproj"))
				return open_seproj(fb_new(full_path));

		available_buffer_slots++;
		file_buffers = xrealloc(file_buffers, sizeof(struct file_buffer) * available_buffer_slots);
		file_buffers[available_buffer_slots-1] = fb_new(full_path);

		return available_buffer_slots-1;
}

void
fb_destroy(struct file_buffer* fb)
{
		free(fb->ub);
		free(fb->contents);
		free(fb->file_path);
		free(fb->search_term);
		free(fb->non_blocking_search_term);
		*fb = (struct file_buffer){0};
}

void
fb_insert(struct file_buffer* fb, const char* new_content, const int len, const int offset, int do_not_callback)
{
		soft_assert(fb, return;);
		soft_assert(fb->contents, fb->capacity = 0;);
		soft_assert(offset <= fb->len && offset >= 0,
					fprintf(stderr, "writing past fb '%s'\n", fb->file_path);
					return;
				);

		if (fb->len + len >= fb->capacity) {
				fb->capacity = fb->len + len + 256;
				fb->contents = xrealloc(fb->contents, fb->capacity);
		}
		if (offset < fb->len)
				memmove(fb->contents+offset+len, fb->contents+offset, fb->len-offset);
		fb->len += len;

		memcpy(fb->contents+offset, new_content, len);
		if (!do_not_callback)
				call_extension(fb_contents_updated, fb, offset, FB_CONTENT_NORMAL_EDIT);
}

void
fb_change(struct file_buffer* fb, const char* new_content, const int len, const int offset, int do_not_callback)
{
		soft_assert(offset <= fb->len && offset >= 0, return;);

		if (offset + len > fb->len) {
				fb->len = offset + len;
				if (fb->len >= fb->capacity) {
						fb->capacity = fb->len + len + 256;
						fb->contents = xrealloc(fb->contents, fb->capacity);
				}
		}

		memcpy(fb->contents+offset, new_content, len);
		if (!do_not_callback)
				call_extension(fb_contents_updated, fb, offset, FB_CONTENT_NORMAL_EDIT);
}

int
fb_remove(struct file_buffer* fb, int offset, int len, int do_not_calculate_charsize, int do_not_callback)
{
		LIMIT(offset, 0, fb->len-1);
		if (len == 0) return 0;
		soft_assert(fb->contents, return 0;);
		soft_assert(offset + len <= fb->len, return 0;);

		int removed_len = 0;
		if (do_not_calculate_charsize) {
				removed_len = len;
		} else {
				while (len--) {
						int charsize = utf8_decode_buffer(fb->contents + offset, fb->len - offset, NULL);
						if (fb->len - charsize < 0)
								return 0;
						removed_len += charsize;
				}
		}
		fb->len -= removed_len;
		memmove(fb->contents+offset, fb->contents+offset+removed_len, fb->len-offset);
		if (!do_not_callback)
				call_extension(fb_contents_updated, fb, offset, FB_CONTENT_NORMAL_EDIT);
		return removed_len;
}

void
wb_copy_ub_to_current(struct window_buffer* wb)
{
		struct file_buffer* fb = get_fb(wb);
		struct undo_buffer* cub = &fb->ub[fb->current_undo_buffer];

		fb->contents = xrealloc(fb->contents, cub->capacity);
		memcpy(fb->contents, cub->contents, cub->capacity);
		fb->len = cub->len;
		fb->capacity = cub->capacity;

		wb_move_to_offset(wb, cub->cursor_offset, CURSOR_SNAPPED);
		//TODO: remove y_scroll from undo buffer
		wb->y_scroll = cub->y_scroll;
}


void
fb_undo(struct file_buffer* fb)
{
		if (fb->current_undo_buffer == 0) {
				writef_to_status_bar("end of undo buffer");
				return;
		}
		fb->current_undo_buffer--;
		fb->available_redo_buffers++;

		wb_copy_ub_to_current(focused_window);
		writef_to_status_bar("undo");
}

void
fb_redo(struct file_buffer* fb)
{
		if (fb->available_redo_buffers == 0) {
				writef_to_status_bar("end of redo buffer");
				return;
		}
		fb->available_redo_buffers--;
		fb->current_undo_buffer++;

		wb_copy_ub_to_current(focused_window);
		writef_to_status_bar("redo");
}

void
fb_add_to_undo(struct file_buffer* fb, int offset, enum buffer_content_reason reason)
{
		static time_t last_normal_edit;
		static int edits;

		if (reason == FB_CONTENT_CURSOR_MOVE) {
				struct undo_buffer* cub = &fb->ub[fb->current_undo_buffer];
				cub->cursor_offset = offset;
				if (focused_window)
						cub->y_scroll = focused_window->y_scroll;
				else
						cub->y_scroll = 0;
				return;
		}

		if (reason == FB_CONTENT_NORMAL_EDIT) {
				time_t previous_time = last_normal_edit;
				last_normal_edit = time(NULL);

				if (last_normal_edit - previous_time < 2 && edits < 30) {
						edits++;
						goto copy_undo_buffer;
				} else {
						edits = 0;
				}
		} else if (reason == FB_CONTENT_INIT) {
				goto copy_undo_buffer;
		}

		fb->available_redo_buffers = 0;
		if (fb->current_undo_buffer == UNDO_BUFFERS_COUNT-1) {
				char* begin_buffer = fb->ub[0].contents;
				memmove(fb->ub, &(fb->ub[1]), (UNDO_BUFFERS_COUNT-1) * sizeof(struct undo_buffer));
				fb->ub[fb->current_undo_buffer].contents = begin_buffer;
		} else {
				fb->current_undo_buffer++;
		}

copy_undo_buffer: ;
		struct undo_buffer* cub = fb->ub + fb->current_undo_buffer;

		cub->contents = xrealloc(cub->contents, fb->capacity);
		memcpy(cub->contents, fb->contents, fb->capacity);
		cub->len = fb->len;
		cub->capacity = fb->capacity;
		cub->cursor_offset = offset;
		if (focused_window)
				cub->y_scroll = focused_window->y_scroll;
		else
				cub->y_scroll = 0;
}


char*
fb_get_string_between_offsets(struct file_buffer* fb, int start, int end)
{
		int len = end - start;

		char* string = xmalloc(len + 1);
		memcpy(string, fb->contents+start, len);
		string[len] = 0;
		return string;
}

char*
fb_get_selection(struct file_buffer* fb, int* selection_len)
{
		if (!(fb->mode & FB_SELECTION_ON))
				return NULL;

		int start, end;
		if (fb_is_selection_start_top_left(fb)) {
				start = fb->s1o;
				end = fb->s2o+1;
		} else {
				start = fb->s2o;
				end = fb->s1o+1;
		}
		if (selection_len)
				*selection_len = end - start;
		return fb_get_string_between_offsets(fb, start, end);
}


int
fb_is_selection_start_top_left(const struct file_buffer* fb)
{
		return (fb->s1o <= fb->s2o) ? 1 : 0;
}

void
fb_remove_selection(struct file_buffer* buffer)
{
		if (!(buffer->mode & FB_SELECTION_ON))
				return;

		int start, end, len;
		if (fb_is_selection_start_top_left(buffer)) {
				start = buffer->s1o;
				end = buffer->s2o+1;
		} else {
				start = buffer->s2o;
				end = buffer->s1o+1;
		}
		len = end - start;
		fb_remove(buffer, start, len, 1, 1);
		call_extension(fb_contents_updated, buffer, start, FB_CONTENT_BIG_CHANGE);
}

char*
fb_get_line_at_offset(const struct file_buffer* fb, int offset)
{
		int start = fb_seek_char_backwards(fb, offset, '\n');
		if (start < 0) start = 0;
		int end = fb_seek_char(fb, offset, '\n');
		if (end < 0) end = fb->len-1;

		int len = end - start;

		char* res = xmalloc(len + 1);
		if (len > 0)
				memcpy(res, fb->contents+start, len);
		res[len] = 0;
		return res;
}

void
fb_offset_to_xy(struct file_buffer* fb, int offset, int maxx, int y_scroll, int* cx, int* cy, int* xscroll)
{
		*cx = *cy = *xscroll = 0;
		soft_assert(fb, return;);

		if (fb->len <= 0)
				return;
		LIMIT(offset, 0, fb->len);

		char* repl = fb->contents;
		char* last = repl + offset;

		char* new_repl;
		if (wrap_buffer && maxx > 0) {
				int yscroll = 0;
				while ((new_repl = memchr(repl, '\n', last - repl))) {
						if (++yscroll >= y_scroll)
								break;
						repl = new_repl+1;
				}
				*cy = yscroll - y_scroll;
		} else {
				while ((new_repl = memchr(repl, '\n', last - repl))) {
						repl = new_repl+1;
						*cy += 1;
				}
				*cy -= y_scroll;
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
				rune_t u;
				repl += utf8_decode_buffer(repl, last - repl, &u);
				*cx += wcwidth(u);
		}

		// TODO: make customizable
		// -1 = wrap, >= 0 is padding or something like that
		const int padding = 3;

		if ((*cx - maxx) + padding > 0)
				*xscroll = (*cx - maxx) + padding;
}

////////////////////////////////////////////////
// Window buffer
//

struct window_buffer
wb_new(int fb_index)
{
		struct window_buffer wb = {0};
		wb.fb_index = fb_index;
		if (path_is_folder(get_fb(&wb)->file_path)) {
				wb.mode = WB_FILE_BROWSER;
				writef_to_status_bar("opened file browser %s", get_fb(&wb)->file_path);
		}

		return wb;
}

void
wb_move_on_line(struct window_buffer* wb, int amount, enum cursor_reason callback_reason)
{
		const struct file_buffer* fb = get_fb(wb);
		if (fb->len <= 0)
				return;

		if (amount < 0) {
				while (wb->cursor_offset > 0 && fb->contents[wb->cursor_offset - 1] != '\n' && amount < 0) {
						wb->cursor_offset--;
						if ((fb->contents[wb->cursor_offset] & 0xC0) == 0x80) // if byte starts with 0b10
								continue; // byte is UTF-8 extender
						amount++;
				}
				LIMIT(wb->cursor_offset, 0, fb->len);
		} else if (amount > 0) {
				for (int charsize = 0;
					 wb->cursor_offset < fb->len && amount > 0 && fb->contents[wb->cursor_offset + charsize] != '\n';
					 wb->cursor_offset += charsize, amount--) {
						rune_t u;
						charsize = utf8_decode_buffer(fb->contents + wb->cursor_offset, fb->len - wb->cursor_offset, &u);
						if (u != '\n' && u != '\t')
								if (wcwidth(u) <= 0)
										amount++;
						if (wb->cursor_offset + charsize > fb->len)
								break;
				}
		}

		if (callback_reason)
				call_extension(wb_cursor_movement, wb, callback_reason);
}

void
wb_move_offset_relative(struct window_buffer* wb, int amount, enum cursor_reason callback_reason)
{
		//NOTE: this does not check if the character on this offset is the start of a valid utf8 char
		const struct file_buffer* fb = get_fb((wb));
		if (fb->len <= 0)
				return;
		wb->cursor_offset += amount;
		LIMIT(wb->cursor_offset, 0, fb->len);

		if (callback_reason)
				call_extension(wb_cursor_movement, wb, callback_reason);
}

void
wb_move_lines(struct window_buffer* wb, int amount, enum cursor_reason callback_reason)
{
		const struct file_buffer* fb = get_fb((wb));
		if (fb->len <= 0)
				return;
		int offset = wb->cursor_offset;
		if (amount > 0) {
				while (amount-- && offset >= 0) {
						int new_offset = fb_seek_char(fb, offset, '\n');
						if (new_offset < 0) {
								offset = fb->len;
								break;
						}
						offset = new_offset+1;
				}
		} else if (amount < 0) {
				while (amount++ && offset >= 0)
						offset = fb_seek_char_backwards(fb, offset, '\n')-1;
		}
		wb_move_to_offset(wb, offset, callback_reason);
}

void
wb_move_to_offset(struct window_buffer* wb, int offset, enum cursor_reason callback_reason)
{
		//NOTE: this does not check if the character on this offset is the start of a valid utf8 char
		const struct file_buffer* fb = get_fb((wb));
		if (fb->len <= 0)
				return;
		LIMIT(offset, 0, fb->len);
		wb->cursor_offset = offset;

		if (callback_reason)
				call_extension(wb_cursor_movement, wb, callback_reason);
}

void
wb_move_to_x(struct window_buffer* wb, int x, enum cursor_reason callback_reason)
{
		soft_assert(wb, return;);
		struct file_buffer* fb = get_fb(wb);

		int offset = fb_seek_char_backwards(fb, wb->cursor_offset, '\n');
		if (offset < 0)
				offset = 0;
		wb_move_to_offset(wb, offset, 0);

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
				rune_t u = 0;
				int charsize = utf8_decode_buffer(fb->contents + offset, fb->len - offset, &u);
				x_counter += wcwidth(u);
				if (x_counter <= x) {
						offset += charsize;
						if (x_counter == x)
								break;
				} else {
						break;
				}
		}
		wb_move_to_offset(wb, offset, callback_reason);
}

////////////////////////////////////////////////
// Window split node
//

static int
is_correct_mode(enum window_split_mode mode, enum move_directons move)
{
		if (move == MOVE_RIGHT || move == MOVE_LEFT)
				return (mode == WINDOW_HORISONTAL);
		if (move == MOVE_UP || move == MOVE_DOWN)
				return (mode == WINDOW_VERTICAL);
		return 0;
}

void
window_node_split(struct window_split_node* parent, float ratio, enum window_split_mode mode)
{
		soft_assert(parent, return;);
		soft_assert(parent->mode == WINDOW_SINGULAR, return;);
		soft_assert(mode != WINDOW_SINGULAR, return;);

		if ((parent->maxx - parent->minx < MIN_WINDOW_SPLIT_SIZE_HORISONTAL && mode == WINDOW_HORISONTAL)
			|| (parent->maxy - parent->miny < MIN_WINDOW_SPLIT_SIZE_VERTICAL && mode == WINDOW_VERTICAL))
				return;

		parent->node1 = xmalloc(sizeof(struct window_split_node));
		*parent->node1 = *parent;
		parent->node1->search = xmalloc(SEARCH_TERM_MAX_LEN);
		parent->node1->parent = parent;
		parent->node1->node1 = NULL;
		parent->node1->node2 = NULL;


		parent->node2 = xmalloc(sizeof(struct window_split_node));
		*parent->node2 = *parent;
		parent->node2->search = xmalloc(SEARCH_TERM_MAX_LEN);
		parent->node2->parent = parent;
		parent->node2->node1 = NULL;
		parent->node2->node2 = NULL;

		if (parent->mode == WINDOW_HORISONTAL) {
				// NOTE: if the window resizing is changed, change in draw tree function as well
				int middlex = ((float)(parent->maxx - parent->minx) * parent->ratio) + parent->minx;
				parent->node1->minx = parent->minx;
				parent->node1->miny = parent->miny;
				parent->node1->maxx = middlex;
				parent->node1->maxy = parent->maxy;
				parent->node2->minx = middlex+2;
				parent->node2->miny = parent->miny;
				parent->node2->maxx = parent->maxx;
				parent->node2->maxy = parent->maxy;
		} else if (parent->mode == WINDOW_VERTICAL) {
				// NOTE: if the window resizing is changed, change in draw tree function as well
				int middley = ((float)(parent->maxy - parent->miny) * parent->ratio) + parent->miny;
				parent->node1->minx = parent->minx;
				parent->node1->miny = parent->miny;
				parent->node1->maxx = parent->maxx;
				parent->node1->maxy = middley;
				parent->node2->minx = parent->miny;
				parent->node2->miny = middley;
				parent->node2->maxx = parent->maxx;
				parent->node2->maxy = parent->maxy;
		}

		parent->mode = mode;
		parent->ratio = ratio;
		parent->wb = (struct window_buffer){0};
}

struct window_split_node*
window_node_delete(struct window_split_node* node)
{
		if (!node->parent) {
				writef_to_status_bar("can't close root winodw");
				return node;
		}
		struct window_split_node* old = node;
		node = node->parent;
		struct window_split_node* other = (node->node1 == old) ? node->node2 : node->node1;
		free(old->search);
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
window_node_draw_tree_to_screen(struct window_split_node* root, int minx, int miny, int maxx, int maxy)
{
		soft_assert(root, return;);

		if (root->mode == WINDOW_SINGULAR) {
				LIMIT(maxx, 0, screen.col-1);
				LIMIT(maxy, 0, screen.row-1);
				LIMIT(minx, 0, maxx);
				LIMIT(miny, 0, maxy);
				root->minx = minx;
				root->miny = miny;
				root->maxx = maxx;
				root->maxy = maxy;
				if (root->wb.mode != 0) {
						int wn_custom_window_draw_callback_exists = 0;
						extension_callback_exists(wn_custom_window_draw, wn_custom_window_draw_callback_exists = 1;);
						soft_assert(wn_custom_window_draw_callback_exists, return;);

						call_extension(wn_custom_window_draw, root);

						return;
				} else {
						window_node_draw_to_screen(root);
				}
		} else if (root->mode == WINDOW_HORISONTAL) {
				// NOTE: if the window resizing is changed, change in split function as well
				int middlex = ((float)(maxx - minx) * root->ratio) + minx;

				// print seperator
				screen_set_region(middlex+1, miny, middlex+1, maxy, L'│');

				window_node_draw_tree_to_screen(root->node1, minx, miny, middlex, maxy);
				window_node_draw_tree_to_screen(root->node2, middlex+2, miny, maxx, maxy);

				for (int y = miny; y < maxy+1; y++)
						xdrawline(middlex+1, y, middlex+2);
		} else if (root->mode == WINDOW_VERTICAL) {
				// NOTE: if the window resizing is changed, change in split function as well
				int middley = ((float)(maxy - miny) * root->ratio) + miny;

				window_node_draw_tree_to_screen(root->node1, minx, miny, maxx, middley);
				window_node_draw_tree_to_screen(root->node2, minx, middley, maxx, maxy);
		}
}

void
window_node_move_all_cursors_on_same_fb(struct window_split_node* root, struct window_split_node* excluded, int fb_index, int offset,
										void(movement)(struct window_buffer*, int, enum cursor_reason),
										int move, enum cursor_reason reason)
{
		if (root->mode == WINDOW_SINGULAR) {
				if (root->wb.fb_index == fb_index && root->wb.cursor_offset >= offset && root != excluded)
						movement(&root->wb, move, reason);
		} else {
				window_node_move_all_cursors_on_same_fb(root->node1, excluded, fb_index, offset, movement, move, reason);
				window_node_move_all_cursors_on_same_fb(root->node2, excluded, fb_index, offset, movement, move, reason);
		}
}

void
window_node_move_all_yscrolls(struct window_split_node* root, struct window_split_node* excluded, int fb_index, int offset, int move)
{
		if (root->mode == WINDOW_SINGULAR) {
				if (root->wb.fb_index == fb_index && root->wb.cursor_offset >= offset && root != excluded)
						root->wb.y_scroll += move;
		} else {
				window_node_move_all_yscrolls(root->node1, excluded, fb_index, offset, move);
				window_node_move_all_yscrolls(root->node2, excluded, fb_index, offset, move);
		}
}

int
window_other_nodes_contain_fb(struct window_split_node* node, struct window_split_node* root)
{
		if (root->mode == WINDOW_SINGULAR)
				return (root->wb.fb_index == node->wb.fb_index && root != node);

		return (window_other_nodes_contain_fb(node, root->node1) ||
				window_other_nodes_contain_fb(node, root->node2));
}

// TODO: create a distance type function and use that instead (from the current cursor position)?
// struct window_split_node* wincdow_closest(root, node ignore(get maxx/minx etc from this and make sure the it's outside the ignored node), enum move_directions move, int cx, int cy)
struct window_split_node*
window_switch_to_window(struct window_split_node* node, enum move_directons move)
{
		soft_assert(node, return &root_node;);
		if (!node->parent) return node;
		soft_assert(node->mode == WINDOW_SINGULAR,
					while(node->mode != WINDOW_SINGULAR)
							node = node->node1;
					return node;
				);
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
window_node_resize(struct window_split_node* node, enum move_directons move, float amount)
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

void
window_node_resize_absolute(struct window_split_node* node, enum move_directons move, float amount)
{
		for (; node; node = node->parent) {
				if (is_correct_mode(node->mode, move)) {
						node->ratio = amount;
						LIMIT(node->ratio, 0.001f, 0.95f);
						return;
				}
		}
}
