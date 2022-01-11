/* See LICENSE file for copyright and license details. */

#include "win.h"
#undef Glyph
#include <unistd.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>

/* types used in config.h */
typedef struct {
	uint mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Shortcut;

/* X modifiers */
#define XK_ANY_MOD    UINT_MAX
#define XK_NO_MOD     0
#define XK_SWITCH_MOD (1<<13|1<<14)

// default functions used for the config
// for the funtions implementation see the x.c file
static void numlock(const Arg *);
static void zoom(const Arg *);
static void zoomabs(const Arg *);
static void zoomreset(const Arg *);
static void cursor_move_x_relative(const Arg* arg);
static void cursor_move_y_relative(const Arg* arg);
static void save_buffer(const Arg* arg);
static void toggle_selection(const Arg* arg);
static void move_cursor_to_offset(const Arg* arg);
static void move_cursor_to_end_of_buffer(const Arg* arg);
static void clipboard_copy(const Arg* arg);
static void clipboard_paste(const Arg* arg);
static void undo(const Arg* arg);
static void redo(const Arg* arg);

static void cursor_callback(struct window_buffer* buf, enum cursor_reason callback_reason);

static void buffer_content_callback(struct file_buffer* buffer, int offset, enum buffer_content_reason reason);

//TODO: make this file friendly to IDE's

/*
 * appearance
 *
 * font: see http://freedesktop.org/software/fontconfig/fontconfig-user.html
 */
static char *font = "Iosevka:pixelsize=16:antialias=true:autohint=true";
static int borderpx = 2;

/* Kerning / character bounding-box multipliers */
static float cwscale = 1.0;
static float chscale = 1.0;


/*
 * thickness of underline and bar cursors
 */
static unsigned int cursorthickness = 2;

/* default TERM value */
char *termname = "st-256color";

// spaces per tab
// tabs will self align
unsigned int tabspaces = 8;

/* Terminal colors (16 first used in escape sequence) */
static const char *colorname[] = {
	/* 8 normal colors */
	"#282828",
	"red3",
	"green3",
	"yellow3",
	"blue2",
	"magenta3",
	"cyan3",
	"#fbf1c7",

	/* 8 bright colors */
	"gray50",
	"red",
	"green",
	"yellow",
	"#5c5cff",
	"magenta",
	"cyan",
	"white",

	[255] = 0,

	/* more colors can be added after 255 to use with DefaultXX */
	"#cccccc",
	"#555555",
};


/*
 * Default colors (colorname index)
 * foreground, background, cursor, reverse cursor
 */
Glyph_ default_attributes = {.bg = 0, .fg = 7};
static unsigned int defaultcs = 256;

int undo_buffers = 32;

/*
 * Default shape of cursor
 * 2: Block ("█")
 * 4: Underline ("_")
 * 6: Bar ("|")
 * 7: Snowman ("☃")
 */
static unsigned int cursorshape = 2;

/*
 * Default columns and rows numbers
 */

static unsigned int cols = 80;
static unsigned int rows = 24;

/*
 * Default colour and shape of the mouse cursor
 */
static unsigned int mouseshape = XC_xterm;
static unsigned int mousefg = 7;
static unsigned int mousebg = 0;

/*
 * Color used to display font attributes when fontconfig selected a font which
 * doesn't match the ones requested.
 */
static unsigned int defaultattr = 11;

void(*cursor_movement_callback)(struct window_buffer*, enum cursor_reason) = cursor_callback;
void(*buffer_contents_updated)(struct file_buffer*, int, enum buffer_content_reason) = buffer_content_callback;

/* Internal keyboard shortcuts. */
#define MODKEY Mod1Mask
#define TERMMOD (ControlMask|ShiftMask)

static Shortcut shortcuts[] = {
	/* mask                 keysym          function        argument */
	{ 0,                    XK_Right,       cursor_move_x_relative,       {.i = +1} },
	{ 0,                    XK_Left,        cursor_move_x_relative,       {.i = -1} },
	{ 0,                    XK_Down,        cursor_move_y_relative,       {.i = +1} },
	{ 0,                    XK_Up,          cursor_move_y_relative,       {.i = -1} },
	{ ControlMask,          XK_m,           toggle_selection,             {0}       },
	{ ControlMask,          XK_g,           move_cursor_to_offset,        {0}       },
	{ TERMMOD,              XK_G,           move_cursor_to_end_of_buffer, {0}       },
	{ ControlMask,          XK_z,           undo,           {0}       },
	{ TERMMOD,              XK_Z,           redo,           {0}       },
	{ ControlMask,          XK_s,           save_buffer,    {0}       },
	{ ControlMask,          XK_c,           clipboard_copy, {0}       },
	{ ControlMask,          XK_v,           clipboard_paste,{0}       },
	{ TERMMOD,              XK_Prior,       zoom,           {.f = +1} },
	{ TERMMOD,              XK_Next,        zoom,           {.f = -1} },
	{ TERMMOD,              XK_Home,        zoomreset,      {.f =  0} },
	{ TERMMOD,              XK_Num_Lock,    numlock,        {.i =  0} },
};

/*
 * Special keys (change & recompile st.info accordingly)
 *
 * Mask value:
 * * Use XK_ANY_MOD to match the key no matter modifiers state
 * * Use XK_NO_MOD to match the key alone (no modifiers)
 * appkey value:
 * * 0: no value
 * * > 0: keypad application mode enabled
 * *   = 2: term.numlock = 1
 * * < 0: keypad application mode disabled
 * appcursor value:
 * * 0: no value
 * * > 0: cursor application mode enabled
 * * < 0: cursor application mode disabled
 *
 * Be careful with the order of the definitions because st searches in
 * this table sequentially, so any XK_ANY_MOD must be in the last
 * position for a key.
 */

/*
 * State bits to ignore when matching key or button events.  By default,
 * numlock (Mod2Mask) and keyboard layout (XK_SWITCH_MOD) are ignored.
 */
static uint ignoremod = Mod2Mask|XK_SWITCH_MOD;

/*
 * Printable characters in ASCII, used to estimate the advance width
 * of single wide characters.
 */
static char ascii_printable[] =
	" !\"#$%&'()*+,-./0123456789:;<=>?"
	"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
	"`abcdefghijklmnopqrstuvwxyz{|}~";
