/* See LICENSE file for copyright and license details. */

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
unsigned int defaultfg = 7;
unsigned int defaultbg = 0;
static unsigned int defaultcs = 256;

int default_mode = MODE_UTF8;
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

void(*cursor_movement_callback)(int, int, enum cursor_reason) = cursor_callback;
void(*buffer_contents_updated)(Buffer*, int, int, enum buffer_content_reason) = buffer_content_callback;

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
