#ifndef BUFFER_H_
#define BUFFER_H_

/* fb: file_buffer
** wb: window_buffer
** wn: window_split_node
**
*/

// Arbitrary sizes
#define SEARCH_TERM_MAX_LEN PATH_MAX
#define LINE_MAX_LEN 2048
#define MIN_WINDOW_SPLIT_SIZE_VERTICAL 10
#define MIN_WINDOW_SPLIT_SIZE_HORISONTAL 20

// external globals
extern struct window_split_node root_node;
extern struct window_split_node* focused_node;
extern struct window_buffer* focused_window;

////////////////////////////////////////////////
// File buffer
//

#define UNDO_BUFFERS_COUNT 128
struct undo_buffer {
	char* contents; // not null terminated
	int len, capacity;
	int cursor_offset;
	int y_scroll;
};

enum buffer_flags {
	FB_SELECTION_ON = 1 << 0,
	FB_BLOCK_SELECT = 1 << 1,
	FB_LINE_SELECT  = 1 << 2,
	FB_SELECT_MASK = (FB_SELECTION_ON | FB_BLOCK_SELECT | FB_LINE_SELECT),
	FB_SELECT_MODE_MASK = (FB_BLOCK_SELECT | FB_LINE_SELECT),
	FB_READ_ONLY    = 1 << 3,
	FB_UTF8_SIGNED  = 1 << 4,
	FB_SEARCH_BLOCKING       = 1 << 5,
	FB_SEARCH_BLOCKING_IDLE  = 1 << 6,
	FB_SEARCH_BLOCKING_MASK = (FB_SEARCH_BLOCKING | FB_SEARCH_BLOCKING_IDLE),
	FB_SEARCH_NON_BLOCKING   = 1 << 7,
	FB_SEARCH_BLOCKING_BACKWARDS   = 1 << 8,
	FB_SEARCH_NON_BLOCKING_BACKWARDS   = 1 << 9,
};

struct file_buffer {
	char* file_path;
	char* contents; // !! NOT NULL TERMINATED !!
	int len;
	int capacity;
	int mode; // buffer_flags
	struct undo_buffer* ub;
	// TODO: int file_buffer_len;
	int current_undo_buffer;
	int available_redo_buffers;
	int s1o, s2o; // selection start offset and end offset
	char* search_term;
	char* non_blocking_search_term;
	int syntax_index;
	unsigned int indent_len; // amount of spaces, if 0 tab is used
};

enum buffer_content_reason {
	FB_CONTENT_DO_NOT_CALLBACK = 0,
	FB_CONTENT_OPERATION_ENDED,
	FB_CONTENT_NORMAL_EDIT,
	FB_CONTENT_BIG_CHANGE,
	FB_CONTENT_INIT,
	FB_CONTENT_CURSOR_MOVE,
};

struct file_buffer* get_fb(struct window_buffer* wb);
int fb_new_entry(const char* file_path);
int destroy_fb_entry(struct window_split_node* node, struct window_split_node* root);
int fb_delete_selection(struct file_buffer* fb);

struct file_buffer fb_new(const char* file_path);
void fb_write_to_filepath(struct file_buffer* fb);
void fb_destroy(struct file_buffer* fb);

void fb_insert(struct file_buffer* fb, const char* new_content, const int len, const int offset, int do_not_callback);
void fb_change(struct file_buffer* fb, const char* new_content, const int len, const int offset, int do_not_callback);
int  fb_remove(struct file_buffer* fb, const int offset, int len, int do_not_calculate_charsize, int do_not_callback);

void fb_undo(struct file_buffer* fb);
void fb_redo(struct file_buffer* fb);
void fb_add_to_undo(struct file_buffer* fb, int offset, enum buffer_content_reason reason);

///////////////////////////////////
// returns a null terminated string containing the selection
// the returned value must be freed by the reciever
// for conveniance the length of the string may be taken with the pointer
// a selection_len of NULL wil be ignored
char* fb_get_string_between_offsets(struct file_buffer* fb, int start, int end);

char* fb_get_selection(struct file_buffer* fb, int* selection_len);
int   fb_is_selection_start_top_left(const struct file_buffer* fb);
void  fb_remove_selection(struct file_buffer* fb);

///////////////////////////////////
// returns a null terminated string containing the current line
// the returned value must be freed by the reciever
// TODO: make this take any string/char instead of hardcoded \n
char* fb_get_line_at_offset(const struct file_buffer* fb, int offset);

void fb_offset_to_xy(struct file_buffer* fb, int offset, int maxx, int y_scroll, int* cx, int* cy, int* xscroll);


////////////////////////////////////////////////
// Window buffer
//

#define WB_NORMAL 0
#define WB_FILE_BROWSER 1
#define WB_MODES_DEFAULT_END 1

struct window_buffer {
	int y_scroll;
	int cursor_offset;
	int cursor_col;

	int fb_index; // index into an array storing file buffers

    ///////////////////////////////////
    // you may implement your own "modes"
    // it will run a callback where you can render your window
    // a callback allowing you to override the default input callback
    // is also provided
    // TODO:↑
    // see extensions/window_modes for other modes
	unsigned int mode; // WB_NORMAL = 0
};

enum cursor_reason {
	CURSOR_DO_NOT_CALLBACK = 0,
	CURSOR_COMMAND_MOVEMENT = 1,
	CURSOR_UP_DOWN_MOVEMENT,
	CURSOR_RIGHT_LEFT_MOVEMENT,
	CURSOR_SNAPPED,
};

struct window_buffer wb_new(int buffer_index);

////////////////////////////////////////////////
// Window split node
//

enum window_split_mode {
	WINDOW_SINGULAR,
	WINDOW_HORISONTAL,
	WINDOW_VERTICAL,
	WINDOW_FILE_BROWSER,
};

struct window_split_node {
	struct window_buffer wb;
	enum window_split_mode mode;
	float ratio;
	struct window_split_node *node1, *node2, *parent;
	int minx, miny, maxx, maxy; // position informatin from the last frame
	char* search;
	int selected;
};

enum move_directons {
	MOVE_RIGHT,
	MOVE_LEFT,
	MOVE_UP,
	MOVE_DOWN,
};

////////////////////////////////////////////////
// Window buffer
//

void wb_write_selection(struct window_buffer* wb, int minx, int miny, int maxx, int maxy);
void wb_move_cursor_to_selection_start(struct window_buffer* wb);

void wb_move_on_line(struct window_buffer* wb, int amount, enum cursor_reason callback_reason);
void wb_move_lines(struct window_buffer* wb, int amount, enum cursor_reason callback_reason);
void wb_move_to_offset(struct window_buffer* wb, int offset, enum cursor_reason callback_reason);
void wb_move_offset_relative(struct window_buffer* wb, int amount, enum cursor_reason callback_reason);
void wb_move_to_x(struct window_buffer* wb, int x, enum cursor_reason callback_reason);

// window split node

void window_node_split(struct window_split_node* parent, float ratio, enum window_split_mode mode);
struct window_split_node* window_node_delete(struct window_split_node* node);
// uses focused_window to draw the cursor
void window_node_draw_tree_to_screen(struct window_split_node* root, int minx, int miny, int maxx, int maxy);
void window_node_move_all_cursors_on_same_fb(struct window_split_node* root, struct window_split_node* excluded, int buf_index, int offset, void(movement)(struct window_buffer*, int, enum cursor_reason), int move, enum cursor_reason reason);
void window_node_move_all_yscrolls(struct window_split_node* root, struct window_split_node* excluded, int buf_index, int offset, int move);
int  window_other_nodes_contain_fb(struct window_split_node* node, struct window_split_node* root);

struct window_split_node* window_switch_to_window(struct window_split_node* node, enum move_directons move);
// NOTE: if you have two splits both having two splits of the split same type, you can't resize the upper split
void window_node_resize(struct window_split_node* node, enum move_directons move, float amount);
void window_node_resize_absolute(struct window_split_node* node, enum move_directons move, float amount);


#endif // BUFFER_H_
