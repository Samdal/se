static char* line_count_relative(struct window_buffer* wb, int y, int lines_left, int minx, int maxx, struct glyph* attr)
{
	static char line[LINE_MAX_LEN];
	int tmp, tmp2, cy;
	fb_offset_to_xy(get_fb(wb), wb->cursor_offset, 0, wb->y_scroll, &tmp, &cy, &tmp2);

	cy += wb->y_scroll + 1;
	y += wb->y_scroll + 1;

	if (y == cy) {
		attr->fg = yellow;
		attr->bg = alternate_bg_bright;
		snprintf(line, LINE_MAX_LEN, "%3d ", y);
	} else {
		int tmp_y = y;
		char* offset = line;
		while(tmp_y >= 1000 && offset - line < LINE_MAX_LEN) {
			*offset++ = ' ';
			tmp_y /= 10;
		}
		snprintf(offset, LINE_MAX_LEN - (offset - line), "%3d ", abs(cy - y));
	}

	return line;
}

// add with this
// char*(*wb_new_line_draw)(struct window_buffer* wb, int y, int lines_left, int minx, int maxx, struct glyph* attr) = line_count_relative;
