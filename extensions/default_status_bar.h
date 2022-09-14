#include <math.h>

extern struct window_buffer* focused_window;

static int default_status_bar_callback(int* write_again, struct window_buffer* buf, int minx, int maxx, int cx, int cy, char line[LINE_MAX_LEN], struct glyph* g);

static const struct extension default_status_bar = {
	.wb_write_status_bar = default_status_bar_callback
};

int
default_status_bar_callback(int* write_again, struct window_buffer* buf, int minx, int maxx, int cx, int cy, char line[LINE_MAX_LEN], struct glyph* g)
{
	static int count = 0;
	if (!buf) {
		count = 0;
		return 0;
	}

	struct file_buffer* fb = get_fb(buf);
	switch (count) {
		const char* name;
		int percent;
	case 0:
		if (fb->mode & FB_SEARCH_BLOCKING_IDLE) {
			int before;
			int search_count = fb_count_string_instances(fb, fb->search_term, focused_window->cursor_offset, &before);
			snprintf(line, LINE_MAX_LEN, " %d/%d", before, search_count);
		}
		break;
	case 1:
		snprintf(line, LINE_MAX_LEN, " %dk ", fb->len/1000);
		break;
	case 2:
		g->fg = path_color;
		char* path = file_path_get_path(fb->file_path);
		snprintf(line, LINE_MAX_LEN, "%s", path);
		free(path);
		break;
	case 3:
		name = strrchr(fb->file_path, '/')+1;
		if (name)
			snprintf(line, LINE_MAX_LEN, "%s", name);
		break;
	case 4:
		percent = ceilf(((float)(buf->cursor_offset)/(float)fb->len)*100.0f);
		LIMIT(percent, 0, 100);
		snprintf(line, LINE_MAX_LEN, "  %d:%d %d%%" , cy+1, cx, percent);
		break;
	case 5:
		count = 0;
		*write_again = 0;
		return 0;
	}
	count++;
	*write_again = 1;
	return 0;
}
