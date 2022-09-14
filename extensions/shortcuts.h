typedef union {
	int i;
	unsigned int ui;
	float f;
	int vec2i[2];
	const void *v;
	const char *s;
} shortcut_arg;


typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const shortcut_arg* arg);
	const shortcut_arg arg;
} Shortcut;

#define shortcuts const Shortcut shortcut_array[]

#define check_shortcuts(_ksym, _modkey)                                 \
    do {                                                                \
        for (int i = 0; i < LEN(shortcut_array); i++) {                 \
            if (_ksym == shortcut_array[i].keysym && match(shortcut_array[i].mod, _modkey)) { \
                shortcut_array[i].func(&(shortcut_array[i].arg));       \
                return 1;                                               \
            }                                                           \
        }                                                               \
    }while(0)

/*
implement with:

SHORTCUTS() = {
//    mask                  keysym          function        argument
	{ 0,                    XK_Right,       cursor_move_x_relative,       {.i = +1} },
	{ 0,                    XK_Left,        cursor_move_x_relative,       {.i = -1} },
	{ 0,                    XK_Down,        cursor_move_y_relative,       {.i = +1} },
};

...

int
keypress_actions(KeySym keysym, int modkey)
{
	check_shortcuts(keysym, modkey);
	...
*/
