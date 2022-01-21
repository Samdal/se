// see colors at: https://github.com/morhetz/gruvbox

enum colour_names {
	bg,
	bg0_h,
	fg,
	sel,
	line,
	red,
	dark_green,
	green,
	teal,
	yellow,
	orange,
	blue,
	purple,
	aqua,
	gray,
};

const char * const colors[] = {
	[bg]	 = "#282828",
	[bg0_h]	 = "#1d2021",
	[fg]	 = "#fbf1c7",
	[sel]	 = "#504945",
	[line]   = "#32302f",
	[red]	 = "#cc251d",
	[dark_green] = "#98971a",
	[green]  = "#b8bb26",
	[teal]   = "#8ec07c",
	[yellow] = "#fabd2f",
	[orange] = "#d65d0e",
	[blue]	 = "#458588",
	[purple] = "#b16286",
	[aqua]	 = "#83a598",
	[gray]	 = "#a89984",
	NULL
};

// default colors
Glyph default_attributes = {.fg = fg, .bg = bg};
unsigned int alternate_bg_bright = sel;
unsigned int alternate_bg_dark = bg0_h;

unsigned int cursor_fg = fg;
unsigned int cursor_bg = bg;
unsigned int mouse_line_bg = line;

unsigned int selection_bg = sel;
unsigned int highlight_color = yellow;
unsigned int path_color = teal;

unsigned int error_color = red;
unsigned int warning_color = yellow;
unsigned int ok_color = green;

#define normal_color    {.fg = fg}
#define string_color	{.fg = gray}
#define comment_color	{.fg = gray}
#define type_color		{.fg = teal}
#define keyword_color	{.fg = green}
#define macro_color		{.fg = yellow}
#define operator_color	{.fg = yellow, .mode = ATTR_BOLD}
#define constants_color {.fg = dark_green}
#define number_color	{.fg = gray}
#define function_color  {.fg = aqua}
