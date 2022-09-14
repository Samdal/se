#ifndef CHOOSE_ONE_OF_SELECTION_H_
#define CHOOSE_ONE_OF_SELECTION_H_

#include "../../config.h"
#include "../../extension.h"
#include <ctype.h>
#include <dirent.h>

static int draw_dir(struct window_split_node* win);
static int draw_search_buffers(struct window_split_node* wn);
static int draw_search_keyword_in_all_buffers(struct window_split_node* wn);

static int file_buffer_keypress_override_callback(int* skip_keypress_callback, struct window_split_node* wn, KeySym ksym, int modkey, const char* buf, int len);
static int choose_one_of_selection_keypress_override_callback(int* skip_keypress_callback, struct window_split_node* wn, KeySym ksym, int modkey, const char* buf, int len);

static const struct extension file_browser = {
	.wn_custom_window_draw = draw_dir,
	.wn_custom_window_keypress_override = file_buffer_keypress_override_callback,
};

static const struct extension search_open_fb = {
	.wn_custom_window_draw = draw_search_buffers,
	.wn_custom_window_keypress_override = choose_one_of_selection_keypress_override_callback
};

static const struct extension search_keywords_open_fb = {
	.wn_custom_window_draw = draw_search_keyword_in_all_buffers,
	.wn_custom_window_keypress_override = choose_one_of_selection_keypress_override_callback
};

struct keyword_pos {
	int offset, fb_index;
};

static void choose_one_of_selection(const char* prefix, const char* search, const char* err,
									const char*(*get_next_element)(const char*, const char*, int* offset, struct glyph* attr, void* data),
									int* selected_line, int minx, int miny, int maxx, int maxy, int focused);

// TODO: system for custom buffers
// and allow for input overrides
// have theese in their own file
static const char* file_browser_next_item(const char* path, const char* search, int* offset, struct glyph* attr, void* data);
// data pointer will give the file buffer of the current item
static const char* buffer_search_next_item(const char* tmp, const char* search, int* offset, struct glyph* attr, void* data);
// data pointer will give the keyword_pos of the current item
static const char* buffers_search_keyword_next_item(const char* tmp, const char* search, int* offset, struct glyph* attr, void* data);

const char*
file_browser_next_item(const char* path, const char* search, int* offset, struct glyph* attr, void* data)
{
	static char filename_search[PATH_MAX];
	static char filename[PATH_MAX];
	static char full_path[PATH_MAX];
	static DIR* dir;
	if (!path || !search) {
		if (dir) {
			closedir(dir);
			dir = NULL;
		}
		return NULL;
	}
	if (!dir)
		dir = opendir(path);
	int len = strlen(search);

    struct dirent *folder;
    while((folder = readdir(dir))) {
		strcpy(filename, folder->d_name);
		strcpy(full_path, path);
		strcat(full_path, filename);
		if (path_is_folder(full_path)) {
			strcat(filename, "/");
			if (attr)
				attr->fg = path_color;
		} else {
			if (attr)
				attr->fg = default_attributes.fg;
		}

		strcpy(filename_search, filename);
		const char* s_repl = search;
		while(!isupper(*s_repl++)) {
			if (*s_repl == 0) {
				for (char* fs = filename_search; *fs; fs++)
					*fs = tolower(*fs);
				break;
			}
		}

		int f_len = strlen(filename_search);
		char* search_start = filename_search;
		if (!*search)
			goto search_match;
		while((search_start = memchr(search_start, *search, f_len))) {
			if (memcmp(search_start, search, len) == 0) {
				search_match:
				if (search[0] != '.' && folder->d_name[0] == '.')
					break;
				if (strcmp(filename, "./") == 0 || strcmp(filename, "../") == 0)
					break;
				if (offset)
					*offset = search_start - filename_search;
				return filename;
			}
			search_start++;
		}
	}
	*filename = *full_path = 0;
	closedir(dir);
	dir = NULL;
	return NULL;
}

const char*
buffer_search_next_item(const char* tmp, const char* search, int* offset, struct glyph* attr, void* data)
{
	static struct window_buffer wb;
	static char file_path[PATH_MAX];
	static int quit_next = 0;
	if (!tmp || !search) {
		wb.fb_index = 0;
		quit_next = 0;
		return NULL;
	}

	int len = strlen(search);
	for(;;) {
		if (quit_next) {
			wb.fb_index = 0;
			quit_next = 0;
			return NULL;
		}
		struct file_buffer* fb = get_fb(&wb);
		int last_fb_index = wb.fb_index;
		wb.fb_index++;
		get_fb(&wb);
		if (wb.fb_index <= last_fb_index)
			quit_next = 1;

		strcpy(file_path, fb->file_path);

		const char* s_repl = search;
		while(!isupper(*s_repl++)) {
			if (*s_repl == 0) {
				for (char* fs = file_path; *fs; fs++)
					*fs = tolower(*fs);
				break;
			}
		}
		char* search_start = file_path;

		if (!len)
			goto search_match;
		while ((search_start = strchr(search_start+1, *search))) {
			if (memcmp(search_start, search, len) == 0) {
			search_match:
				if (offset)
					*offset = search_start - file_path;
				if (data)
					*(int*)data = last_fb_index;
				return fb->file_path;
			}
		}
	}
}

const char*
buffers_search_keyword_next_item(const char* tmp, const char* search, int* offset, struct glyph* attr, void* data)
{
	static char item[LINE_MAX_LEN];
	static struct window_buffer wb;
	static int pos = -1;
	if (!tmp || !search) {
		wb.fb_index = 0;
		pos = -1;
		return NULL;
	}
	int len = strlen(search);
	if (!len)
		return NULL;

	for (;;) {
		struct file_buffer* fb = get_fb(&wb);

		if ((pos = fb_seek_string(fb, pos+1, search)) >= 0) {
			const char* filename = strrchr(fb->file_path, '/')+1;
			if (!filename)
				filename = fb->file_path;

			int tmp, y;
			fb_offset_to_xy(fb, pos, 0, wb.y_scroll, &tmp, &y, &tmp);

			snprintf(item, LINE_MAX_LEN, "%s:%d: ", filename, y);
			int itemlen = strlen(item);

			char* line = fb_get_line_at_offset(fb, pos);
			char* line_no_whitespace = line;
			if (!isspace(*search))
				while(isspace(*line_no_whitespace)) line_no_whitespace++;

			snprintf(item + itemlen, LINE_MAX_LEN - itemlen, "%s", line_no_whitespace);

			int line_start = fb_seek_char_backwards(fb, pos, '\n') + (line_no_whitespace - line);
			free(line);

			if (line_start < 0)
				line_start = 0;
			if (offset)
				*offset = (pos - line_start) + itemlen;
			if (data)
				*(struct keyword_pos*)data = (struct keyword_pos){.offset = pos, .fb_index = wb.fb_index};
			pos = fb_seek_char(fb, pos+1, '\n');
			return item;
		} else {
			int last_fb_index = wb.fb_index;
			wb.fb_index++;
			get_fb(&wb);
			if (wb.fb_index <= last_fb_index)
				break;
			pos = -1;
		}
	}
	wb.fb_index = 0;
	pos = -1;
	return NULL;
}

static void
choose_one_of_selection(const char* prefix, const char* search, const char* err,
						const char*(*get_next_element)(const char*, const char*, int* offset, struct glyph* attr, void* data),
						int* selected_line, int minx, int miny, int maxx, int maxy, int focused)
{
	soft_assert(prefix, prefix = "ERROR";);
	soft_assert(search, search = "ERROR";);
	soft_assert(err, search = "ERROR";);
	soft_assert(selected_line, static int tmp; selected_line = &tmp;);
	soft_assert(get_next_element, die("function choose_one_of_selection REQUIRES get_next_element to be a valid pointer"););

	// change background color
	global_attr.bg = alternate_bg_dark;
	screen_set_region(minx, miny+1, maxx, maxy, ' ');
	global_attr = default_attributes;
	get_next_element(NULL, NULL, NULL, NULL, NULL);

	int len = strlen(search);

	// count folders to get scroll
	int folder_lines = maxy - miny - 2;
	int elements = 0;
	int tmp_offset;
	int limit = MAX(*selected_line, 999);
	while(get_next_element(prefix, search, &tmp_offset, NULL, NULL) && elements <= limit)
		elements++;
	get_next_element(NULL, NULL, NULL, NULL, NULL);
	*selected_line = MIN(*selected_line, elements-1);
	int sel_local = *selected_line;

	// print num of files
	char count[256];
	if (elements >= 1000)
		snprintf(count, sizeof(count), "[>999:%2d] ", *selected_line+1);
	else if (*selected_line > folder_lines)
		snprintf(count, sizeof(count), "^[%3d:%2d] ", elements, *selected_line+1);
	else if (elements-1 > folder_lines)
		snprintf(count, sizeof(count), "ˇ[%3d:%2d] ", elements, *selected_line+1);
	else
		snprintf(count, sizeof(count), " [%3d:%2d] ", elements, *selected_line+1);

	// print search term with prefix in front of it
	// prefix is in path_color
	global_attr.fg = path_color;
	int new_x = write_string(count, miny, minx, maxx+1);
	new_x = write_string(prefix, miny, new_x, maxx+1);
	global_attr = default_attributes;
	new_x = write_string(search, miny, new_x, maxx+1);

	// print elements
	int start_miny = miny;
	int offset;
	elements--;
	miny++;
	global_attr = default_attributes;
	global_attr.bg = alternate_bg_dark;
	const char* element;
	while(miny <= maxy && (element = get_next_element(prefix, search, &offset, &global_attr, NULL))) {
		if (elements > folder_lines && sel_local > folder_lines) {
			elements--;
			sel_local--;
			continue;
		}
		write_string(element, miny, minx, maxx+1);

		// change the color to highlight search term
		for (int i = minx + offset; i < minx + len + offset && i < maxx+1; i++)
			screen_set_attr(i, miny)->fg = highlight_color;
		// change the background of the selected line
		if (miny - start_miny - 1 == sel_local)
			for (int i = minx; i < maxx+1; i++)
				screen_set_attr(i, miny)->bg = selection_bg;
		miny++;
	}

	if (elements < 0) {
		global_attr = default_attributes;
		global_attr.fg = warning_color;
		write_string(err, start_miny, new_x, maxx+1);
	}

	// draw

	for (int y = start_miny; y < maxy+1; y++)
		xdrawline(minx, y, maxx+1);

	draw_horisontal_line(maxy-1, minx, maxx);
	xdrawcursor(new_x, start_miny, focused);

	global_attr = default_attributes;
}

static int
draw_dir(struct window_split_node* wn)
{
	soft_assert(wn->wb.mode < WB_MODES_END, return 1;);
	if (wn->wb.mode != WB_FILE_BROWSER) return 0;

	int focused = &wn->wb == focused_window;
	struct file_buffer* fb = get_fb(&wn->wb);
	char* folder = file_path_get_path(fb->file_path);

	fb_change(fb, "\0", 1, fb->len, 1);
	if (fb->len > 0) fb->len--;

	choose_one_of_selection(folder, fb->contents, " [Create New File]", file_browser_next_item,
							&wn->selected, wn->minx, wn->miny, wn->maxx, wn->maxy, focused);

	free(folder);
	return 1;
}

static int
draw_search_buffers(struct window_split_node* wn)
{
	soft_assert(wn->wb.mode < WB_MODES_END, return 1;);
	if (wn->wb.mode != WB_SEARCH_FOR_BUFFERS) return 0;

	int focused = &wn->wb == focused_window;
	choose_one_of_selection("Find loaded buffer: ", wn->search, " [No resuts]", buffer_search_next_item,
							&wn->selected, wn->minx, wn->miny, wn->maxx, wn->maxy, focused);
	return 1;
}

static int
draw_search_keyword_in_all_buffers(struct window_split_node* wn)
{
	soft_assert(wn->wb.mode < WB_MODES_END, return 1;);
	if (wn->wb.mode != WB_SEARCH_KEYWORD_ALL_BUFFERS) return 0;

	int focused = &wn->wb == focused_window;
	choose_one_of_selection("Find in all buffers: ", wn->search, " [No resuts]", buffers_search_keyword_next_item,
							&wn->selected, wn->minx, wn->miny, wn->maxx, wn->maxy, focused);
	return 1;
}

static int
file_browser_actions(KeySym keysym, int modkey)
{
	static char full_path[PATH_MAX];
	struct file_buffer* fb = get_fb(focused_window);
	int offset = fb->len;

	switch (keysym) {
		int new_fb;
	case XK_BackSpace:
		if (offset <= 0) {
			char* dest = strrchr(fb->file_path, '/');
			if (dest && dest != fb->file_path) *dest = 0;
			return 1;
		}

		focused_window->cursor_offset = offset;
		wb_move_on_line(focused_window, -1, 0);
		offset = focused_window->cursor_offset;

		fb_remove(fb, offset, 1, 0, 0);
		focused_node->selected = 0;
		return 1;
	case XK_Tab:
	case XK_Return:
	{
		char* path = file_path_get_path(fb->file_path);
		fb_change(fb, "\0", 1, fb->len, 1);
		if (fb->len > 0) fb->len--;

		file_browser_next_item(NULL, NULL, NULL, NULL, NULL);
		const char* filename;
		for (int y = 0; (filename = file_browser_next_item(path, fb->contents, NULL, NULL, NULL)); y++) {
			strcpy(full_path, path);
			strcat(full_path, filename);
			if (y == focused_node->selected) {
				if (path_is_folder(full_path)) {
					strcpy(fb->file_path, full_path);

					fb->len = 0;
					fb->contents[0] = '\0';
					focused_node->selected = 0;

					free(path);
					file_browser_next_item(NULL, NULL, NULL, NULL, NULL);
					*full_path = 0;
					return 1;
				}
				goto open_file;
			}
		}

		if (fb->contents[fb->len-1] == '/') {
			free(path);
			*full_path = 0;
			return 1;
		}

		strcpy(full_path, path);
		strcat(full_path, fb->contents);
open_file:
		new_fb = fb_new_entry(full_path);
		destroy_fb_entry(focused_node, &root_node);
		focused_node->wb = wb_new(new_fb);
		free(path);
		*full_path = 0;
		return 1;
	}
	case XK_Down:
		focused_node->selected++;
		return 1;
	case XK_Up:
		focused_node->selected--;
		if (focused_node->selected < 0)
			focused_node->selected = 0;
		return 1;
	case XK_Escape:
		if (destroy_fb_entry(focused_node, &root_node))
			writef_to_status_bar("Quit");
		return 1;
	}
	return 0;
}

static void
file_browser_string_insert(const char* buf, int buflen)
{
	static char full_path[PATH_MAX];
	struct file_buffer* fb = get_fb(focused_window);

	if (fb->len + buflen + strlen(fb->file_path) > PATH_MAX)
		return;

	if (buf[0] >= 32 || buflen > 1) {
		fb_insert(fb, buf, buflen, fb->len, 0);
		focused_node->selected = 0;

		if (*buf == '/') {
			fb_change(fb, "\0", 1, fb->len, 0);
			if (fb->len > 0) fb->len--;
			char* path = file_path_get_path(fb->file_path);
			strcpy(full_path, path);
			strcat(full_path, fb->contents);

			free(path);

			if (path_is_folder(full_path)) {
				file_browser_actions(XK_Return, 0);
				return;
			}
		}
	} else {
		writef_to_status_bar("unhandled control character 0x%x\n", buf[0]);
	}
}

static int
search_for_buffer_actions(KeySym keysym, int modkey)
{
	switch (keysym) {
	case XK_Return:
	case XK_Tab:
		if (focused_window->mode == WB_SEARCH_KEYWORD_ALL_BUFFERS) {
			struct keyword_pos kw;
			int n = 0;
			buffers_search_keyword_next_item(NULL, NULL, NULL, NULL, NULL);
			while (buffers_search_keyword_next_item("", focused_node->search, NULL, NULL, &kw)) {
				if (n == focused_node->selected) {
					*focused_window = wb_new(kw.fb_index);
					focused_window->cursor_offset = kw.offset;
					return 1;
				}
				n++;
			}
			buffers_search_keyword_next_item(NULL, NULL, NULL, NULL, NULL);
		} else if (focused_window->mode == WB_SEARCH_FOR_BUFFERS) {
			int fb_index;
			int n = 0;
			buffer_search_next_item(NULL, NULL, NULL, NULL, NULL);
			while (buffer_search_next_item("", focused_node->search, NULL, NULL, &fb_index)) {
				if (n == focused_node->selected) {
					*focused_window = wb_new(fb_index);
					return 1;
				}
				n++;
			}
			buffer_search_next_item(NULL, NULL, NULL, NULL, NULL);
		}
		writef_to_status_bar("no results for \"%s\"", focused_node->search);
		return 1;
	case XK_BackSpace:
		utf8_remove_string_end(focused_node->search);
		focused_node->selected = 0;
		return 1;
	case  XK_Down:
		focused_node->selected++;
		return 1;
	case  XK_Up:
		focused_node->selected--;
		if (focused_node->selected < 0)
			focused_node->selected = 0;
		return 1;
	case  XK_Page_Down:
		focused_node->selected += 10;
		return 1;
	case  XK_Page_Up:
		focused_node->selected -= 10;
		if (focused_node->selected < 0)
			focused_node->selected = 0;
		return 1;
	case XK_Escape:
		if (path_is_folder(get_fb(focused_window)->file_path))
			focused_window->mode = WB_FILE_BROWSER;
		else
			focused_window->mode = WB_NORMAL;

		writef_to_status_bar("Quit");
		return 1;
	}
	return 0;
}

static void
search_for_buffer_string_insert(const char* buf, int buflen)
{
	int len = strlen(focused_node->search);
	if (buflen + len + 1 > SEARCH_TERM_MAX_LEN)
		return;

	if (buf[0] >= 32 || buflen > 1) {
		memcpy(focused_node->search + len, buf, buflen);
		focused_node->search[len + buflen] = 0;
		focused_node->selected = 0;
	} else {
		writef_to_status_bar("unhandled control character 0x%x\n", buf[0]);
	}
}

int
file_buffer_keypress_override_callback(int* skip_keypress_callback, struct window_split_node* wn, KeySym ksym, int modkey, const char* buf, int len)
{
	soft_assert(wn->wb.mode < WB_MODES_END, return 1;);
	if (wn->wb.mode != WB_FILE_BROWSER) return 0;

	if (file_browser_actions(ksym, modkey)) {
		*skip_keypress_callback = 1;
		return 1;
	}
	file_browser_string_insert(buf, len);
	*skip_keypress_callback = 1;
	return 1;
}

int
choose_one_of_selection_keypress_override_callback(int* skip_keypress_callback, struct window_split_node* wn, KeySym ksym, int modkey, const char* buf, int len)
{
	soft_assert(wn->wb.mode < WB_MODES_END, return 1;);
	if (wn->wb.mode != WB_SEARCH_FOR_BUFFERS &&
		wn->wb.mode != WB_SEARCH_KEYWORD_ALL_BUFFERS)
		return 0;

	if (search_for_buffer_actions(ksym, modkey)) {
		*skip_keypress_callback = 1;
		return 1;
	}
	search_for_buffer_string_insert(buf, len);
	*skip_keypress_callback = 1;
	return 1;
}

#endif // CHOOSE_ONE_OF_SELECTION_H_
