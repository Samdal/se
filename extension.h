#ifndef EXTENSION_H_
#define EXTENSION_H_

#include "se.h"

struct extension {
        char* name;
        char* description;

// standard functions
        void(*init)(struct extension* self);
        void(*frame)(void);
        void(*enable)(void);
        void(*disable)(void);

/////////////////////////////////////////
// callbacks
//
// A non-zero return value from an extension callback
// will not call any further extensions that have the same callback.
// This is necearry for any of the callbacks that have "return" values,
// namely wb_new_line_draw, wb_write_status_bar
// and wn_custom_window_keypress_override
//


// window buffer
        int(*wb_cursor_movement)(struct window_buffer* wb, enum cursor_reason reason);

        ///////////////////////////////////
        // string will be drawn at the start of a new line
        int(*wb_new_line_draw)(char** string, struct window_buffer* wb, int y, int lines_left, int minx, int maxx, struct glyph* attr);

        ///////////////////////////////////
        // write_again means the line will be written and the callback will
        // be called again with minx at the end line
        int(*wb_write_status_bar)(int* write_again, struct window_buffer* wb, int minx, int maxx, int cx, int cy, char line[LINE_MAX_LEN], struct glyph* g);


// file buffer
        int(*fb_new_file_opened)(struct file_buffer* fb);

        int(*fb_paste)(struct file_buffer* fb, char* data, int len);
        int(*keypress)(KeySym keycode, int modkey, const char* buf, int len);
        int(*fb_written_to_file)(struct file_buffer* fb);

        ///////////////////////////////////
        // For undo functionality you can use this function to update the undo buffers
        // there is provided the fb_add_to_undo function, but you may implement your own
        // see buffer.h/buffer.c fb_add_to_undo()
        int(*fb_contents_updated)(struct file_buffer* fb, int offset, enum buffer_content_reason reason);


// window node
        int(*wn_custom_window_draw)(struct window_split_node* wn);
        int(*window_written_to_screen)(struct window_split_node* wn, const int offset_start, const int offset_end, uint8_t* move_buffer, const int move_buffer_len);
        int(*wn_custom_window_keypress_override)(int* skip_keypress_callback, struct window_split_node* wn, KeySym keycode, int modkey, const char* buf, int len);
};

struct extension_meta {
        struct extension e;
        size_t enabled;
        size_t end;
};

#define extension_callback_exists(_callback, ...)                       \
        do {                                                            \
                if (!extensions)                                        \
                        break;                                          \
                for (int _iterator = 0; !extensions[_iterator].end; _iterator++) { \
                        if (extensions[_iterator].e._callback && extensions[_iterator].enabled) { \
                                __VA_ARGS__                             \
                                break;                                  \
                        }                                               \
                }                                                       \
        } while (0)

#define call_extension(_callback, ...)                                  \
        do {                                                            \
                if (!extensions)                                        \
                        break;                                          \
                for (int _iterator = 0; !extensions[_iterator].end; _iterator++) \
                        if (extensions[_iterator].e._callback && extensions[_iterator].enabled) \
                                if (extensions[_iterator].e._callback(__VA_ARGS__)) \
                                        break;                          \
        } while (0)


#endif // EXTENSION_H_
