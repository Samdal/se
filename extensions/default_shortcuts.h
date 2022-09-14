static void numlock(const shortcut_arg* arg);
static void zoom(const shortcut_arg* arg);
static void zoomabs(const shortcut_arg* arg);
static void zoomreset(const shortcut_arg* arg);

static void window_split(const shortcut_arg* arg);
static void window_resize(const shortcut_arg* arg);
static void window_delete(const shortcut_arg* arg);
static void window_change(const shortcut_arg* arg);

static void cursor_move_x_relative(const shortcut_arg* arg);
static void cursor_move_y_relative(const shortcut_arg* arg);
static void move_cursor_to_offset(const shortcut_arg* arg);
static void move_cursor_to_end_of_buffer(const shortcut_arg* arg);

static void swap_to_next_file_buffer(const shortcut_arg* arg);
static void save_buffer(const shortcut_arg* arg);
static void buffer_kill(const shortcut_arg* arg);

static void clipboard_copy(const shortcut_arg* arg);
static void clipboard_paste(const shortcut_arg* arg);
static void toggle_selection(const shortcut_arg* arg);
static void undo(const shortcut_arg* arg);
static void redo(const shortcut_arg* arg);

static void search(const shortcut_arg* arg);
static void search_next(const shortcut_arg* arg);
static void search_previous(const shortcut_arg* arg);
static void search_for_buffer(const shortcut_arg* arg);
static void search_keyword_in_buffers(const shortcut_arg* arg);
static void open_file_browser(const shortcut_arg* arg);

/////////////////////////////////////////////////
// function implementations
//

void
numlock(const shortcut_arg* arg)
{
	win.mode ^= MODE_NUMLOCK;
}

void window_split(const shortcut_arg* arg)
{
	window_node_split(focused_node, 0.5, arg->i);
#if 1
	if (focused_node->node2) {
		focused_node = focused_node->node2;
		focused_window = &focused_node->window;
	}
#else
	if (focused_node->node1) {
		focused_node = focused_node->node1;
		focused_window = &focused_node->window;
	}
#endif
}

void window_resize(const shortcut_arg* arg)
{
	float amount = (arg->i == MOVE_RIGHT || arg->i == MOVE_LEFT) ? 0.1f : 0.05f;
	window_node_resize(focused_node, arg->i, amount);
}

void window_delete(const shortcut_arg* arg)
{
	struct window_split_node* new_node = window_node_delete(focused_node);
	while (new_node->mode != WINDOW_SINGULAR)
		new_node = new_node->node1;
	focused_node = new_node;
	focused_window = &focused_node->window;
}

void window_change(const shortcut_arg* arg)
{
	focused_node = window_switch_to_window(focused_node, arg->i);
	focused_window = &focused_node->window;
}

void
zoom(const shortcut_arg* arg)
{
	shortcut_arg larg;

	larg.f = usedfontsize + arg->f;
	zoomabs(&larg);
}

void
zoomabs(const shortcut_arg* arg)
{
	xunloadfonts();
	xloadfonts(fontconfig, arg->f);
	cresize(0, 0);
	xhints();
}

void
zoomreset(const shortcut_arg* arg)
{
	shortcut_arg larg;

	if (defaultfontsize > 0) {
		larg.f = defaultfontsize;
		zoomabs(&larg);
	}
}

void
cursor_move_x_relative(const shortcut_arg* arg)
{
	if (focused_window->mode != WINDOW_BUFFER_FILE_BROWSER)
		buffer_move_on_line(focused_window, arg->i, CURSOR_RIGHT_LEFT_MOVEMENT);
}

void
cursor_move_y_relative(const shortcut_arg* arg)
{
	buffer_move_lines(focused_window, arg->i, 0);
	buffer_move_to_x(focused_window, focused_window->cursor_col, CURSOR_UP_DOWN_MOVEMENT);
}

void
swap_to_next_file_buffer(const shortcut_arg* arg)
{
	focused_window->buffer_index++;
}

void
save_buffer(const shortcut_arg* arg)
{
	buffer_write_to_filepath(get_file_buffer(focused_window));
}

void
toggle_selection(const shortcut_arg* arg)
{
	struct file_buffer* fb = get_file_buffer(focused_window);
	if (fb->mode & BUFFER_SELECTION_ON) {
		fb->mode &= ~(BUFFER_SELECTION_ON);
	} else {
		fb->mode |= BUFFER_SELECTION_ON;
		fb->s1o = fb->s2o = focused_window->cursor_offset;
	}
}

void
move_cursor_to_offset(const shortcut_arg* arg)
{
	focused_window->cursor_offset = arg->i;
}

void
move_cursor_to_end_of_buffer(const shortcut_arg* arg)
{
	focused_window->cursor_offset = get_file_buffer(focused_window)->len-1;
}

void
clipboard_copy(const shortcut_arg* arg)
{
	struct file_buffer* fb = get_file_buffer(focused_window);
	int len;
	char* buf = buffer_get_selection(fb, &len);
	set_clipboard_copy(buf, len);

	buffer_move_cursor_to_selection_start(focused_window);
	fb->mode &= ~BUFFER_SELECTION_ON;
}

void
clipboard_paste(const shortcut_arg* arg)
{
	insert_clipboard_at_cursor();
}

void
undo(const shortcut_arg* arg)
{
	buffer_undo(get_file_buffer(focused_window));
}

void
redo(const shortcut_arg* arg)
{
	buffer_redo(get_file_buffer(focused_window));
}

void
search(const shortcut_arg* arg)
{
	get_file_buffer(focused_window)->mode &= ~BUFFER_SEARCH_BLOCKING_IDLE;
	get_file_buffer(focused_window)->mode |= BUFFER_SEARCH_BLOCKING;
	writef_to_status_bar("search: %s", get_file_buffer(focused_window)->search_term);
}

void
search_next(const shortcut_arg* arg)
{
	int new_offset = buffer_seek_string_wrap(focused_window, focused_window->cursor_offset+1,
											 get_file_buffer(focused_window)->search_term);
	if (new_offset < 0) {
		writef_to_status_bar("no results for \"%s\"", get_file_buffer(focused_window)->search_term);
		return;
	} else if (focused_window->cursor_offset > new_offset) {
		writef_to_status_bar("search wrapped");
	}
	focused_window->cursor_offset = new_offset;
}

void
search_previous(const shortcut_arg* arg)
{
	int new_offset = buffer_seek_string_wrap_backwards(focused_window, focused_window->cursor_offset-1,
													   get_file_buffer(focused_window)->search_term);
	if (new_offset < 0) {
		writef_to_status_bar("no results for \"%s\"", get_file_buffer(focused_window)->search_term);
		return;
	} else if (focused_window->cursor_offset < new_offset) {
		writef_to_status_bar("search wrapped");
	}
	focused_window->cursor_offset = new_offset;
}

void
search_for_buffer(const shortcut_arg* arg)
{
	if (focused_window->mode != WINDOW_BUFFER_NORMAL)
		return;
	*focused_node->search = 0;
	focused_node->selected = 0;
	focused_window->mode = WINDOW_BUFFER_SEARCH_BUFFERS;
}

void
search_keyword_in_buffers(const shortcut_arg* arg)
{
	if (focused_window->mode != WINDOW_BUFFER_NORMAL)
		return;
	*focused_node->search = 0;
	focused_node->selected = 0;
	focused_window->mode = WINDOW_BUFFER_KEYWORD_ALL_BUFFERS;
}


void
open_file_browser(const shortcut_arg* arg)
{
	int last_fb = focused_window->buffer_index;
	struct file_buffer* fb = get_file_buffer(focused_window);

	char* path = file_path_get_path(fb->file_path);
	*focused_window = window_buffer_new(new_file_buffer_entry(path));
	focused_window->cursor_col = last_fb;
	free(path);
}

void
buffer_kill(const shortcut_arg* arg)
{
	destroy_file_buffer_entry(focused_node, &root_node);
}
