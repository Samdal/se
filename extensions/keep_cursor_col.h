#ifndef KEEP_CURSOR_COL_H_
#define KEEP_CURSOR_COL_H_

static int
keep_cursor_col_callback(struct window_buffer* buf, enum cursor_reason callback_reason)
{
	if (callback_reason == CURSOR_COMMAND_MOVEMENT || callback_reason == CURSOR_RIGHT_LEFT_MOVEMENT) {
		int y, tmp;
		fb_offset_to_xy(get_fb(buf), buf->cursor_offset, 0, buf->y_scroll, &buf->cursor_col, &y, &tmp);
	}
    return 0;
}

static const struct extension keep_cursor_col = {
    .wb_cursor_movement = keep_cursor_col_callback
};

#endif // KEEP_CURSOR_COL_H_
