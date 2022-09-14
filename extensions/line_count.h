static char* line_count(struct window_buffer* wb, int y, int lines_left, int minx, int maxx, struct glyph* attr)
{
	static char line[LINE_MAX_LEN];
	int tmp, tmp2, cy;
	fb_offset_to_xy(get_fb(wb), wb->cursor_offset, 0, wb->y_scroll, &tmp, &cy, &tmp2);

	y += wb->y_scroll + 1;
	cy += wb->y_scroll + 1;

	snprintf(line, LINE_MAX_LEN, "%3d ", y);

	if (y == cy) {
		attr->fg = yellow;
		attr->bg = alternate_bg_bright;
	}

	return line;
}

// add with this:
// char*(*wb_new_line_draw)(struct window_buffer* wb, int y, int lines_left, int minx, int maxx, Glyph* attr) = line_count;
