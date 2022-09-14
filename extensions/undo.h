#ifndef UNDO_H_
#define UNDO_H_

static int
fb_add_to_undo_callback(struct file_buffer* fb, int offset, enum buffer_content_reason reason)
{
    fb_add_to_undo(fb, offset, reason);
    return 0;
}

static int
cursor_undo(struct window_buffer* wb, enum cursor_reason callback_reason)
{
	fb_add_to_undo(get_fb(wb),  wb->cursor_offset, FB_CONTENT_CURSOR_MOVE);
	//writef_to_status_bar("moved to: %d | reason: %d\n", wb->cursor_offset, callback_reason);
	return 0;
}

static const struct extension undo = {
    .fb_contents_updated = fb_add_to_undo_callback,
    .wb_cursor_movement = cursor_undo
};

#endif // UNDO_H_
