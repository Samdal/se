#ifndef MOVE_SELECTION_WITH_CURSOR_H_
#define MOVE_SELECTION_WITH_CURSOR_H_

static int
move_selection(struct window_buffer* wb, enum cursor_reason callback_reason)
{
	struct file_buffer* fb = get_fb(wb);
	if (fb->mode & FB_SELECTION_ON) {
        if (fb->mode & FB_LINE_SELECT) {
            int twice = 2;
            while (twice--) {
                    if (fb_is_selection_start_top_left(fb)) {
                        fb->s2o = fb_seek_char(fb, wb->cursor_offset, '\n');
                        fb->s1o = fb_seek_char_backwards(fb, fb->s1o, '\n');
                    } else {
                        fb->s2o = fb_seek_char_backwards(fb, wb->cursor_offset, '\n');
                        fb->s1o = fb_seek_char(fb, fb->s1o, '\n');
                }
            }
        } else {
            fb->s2o = wb->cursor_offset;
        }

	}
    return 0;
}

static const struct extension move_selection_with_cursor = {
    .wb_cursor_movement = move_selection,
};

#endif // MOVE_SELECTION_WITH_CURSOR_H_
