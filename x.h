/* See LICENSE for license details. */

/*
** This file mainly contains X11 stuff (drawing to the screen, window hints, etc)
** Most of that part is unchanged from ST (https://st.suckless.org/)
** the main() function and the main loop are found at the very bottom of this file
** there are a very few functions here that are interresting for configuratinos.
*/

#ifndef _X_H
#define _X_H

#include "utf8.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <limits.h>

void draw_horisontal_line(int y, int x1, int x2);
// TODO: vertical line
// !!! NOTE:
// !!! buffer MUST be malloced and NOT be freed after it is passed
void set_clipboard_copy(char* buffer, int len);
void execute_clipbaord_event();

enum glyph_attribute {
	ATTR_NULL       = 0,
	ATTR_BOLD       = 1 << 0,
	ATTR_FAINT      = 1 << 1,
	ATTR_ITALIC     = 1 << 2,
	ATTR_UNDERLINE  = 1 << 3,
	ATTR_REVERSE    = 1 << 5,
	ATTR_INVISIBLE  = 1 << 6,
	ATTR_STRUCK     = 1 << 7,
	ATTR_WIDE       = 1 << 9,
	ATTR_WDUMMY     = 1 << 10,
	ATTR_BOLD_FAINT = ATTR_BOLD | ATTR_FAINT,
};

struct glyph {
	rune_t  u;		// character code
	uint16_t  mode;	// attribute flags
	uint32_t fg;	// foreground
	uint32_t bg;	// background
};

// Internal representation of the screen
struct screen {
	int row;         // row count
	int col;         // column count
	struct glyph** lines;   // screen letters 2d array
};

extern struct screen screen;
extern struct glyph global_attr;

void screen_init(int col, int row);
void screen_resize(int col, int row);
void screen_set_region(int x1, int y1, int x2, int y2, rune_t u);
int screen_set_char(rune_t u, int x, int y);
struct glyph* screen_set_attr(int x, int y);

void* xmalloc(size_t len);
void* xrealloc(void *p, size_t len);
void die(const char *, ...);

// the va_args can be used to return; or any other stuff like that
// TODO: optionally crash the program for debugging
#define soft_assert(condition, ...)										\
	do {																\
		if(!(condition)) {												\
			fprintf(stderr, "SOFT ASSERT ERROR: (%s) failed at %s %s():%d\n", #condition, __FILE__, __func__, __LINE__); \
			writef_to_status_bar("SOFT ASSERT ERROR: (%s) failed at %s %s():%d", #condition, __FILE__, __func__, __LINE__); \
			status_bar_bg = error_color;								\
			__VA_ARGS__													\
		}																\
	} while(0)															\

enum win_mode {
	MODE_VISIBLE     = 1 << 0,
	MODE_FOCUSED     = 1 << 1,
	MODE_APPKEYPAD   = 1 << 2,
	MODE_KBDLOCK     = 1 << 6,
	MODE_HIDE        = 1 << 7,
	MODE_APPCURSOR   = 1 << 8,
	MODE_MOUSESGR    = 1 << 9,
	MODE_BLINK       = 1 << 11,
	MODE_FBLINK      = 1 << 12,
	MODE_BRCKTPASTE  = 1 << 16,
	MODE_NUMLOCK     = 1 << 17,
};

#define MIN(a, b)		((a) < (b) ? (a) : (b))
#define MAX(a, b)		((a) < (b) ? (b) : (a))
#define LEN(a)			(sizeof(a) / sizeof(a)[0])
#define LIMIT(x, a, b)		(x) = (x) < (a) ? (a) : (x) > (b) ? (b) : (x)
#define BETWEEN(x, a, b)	((a) <= (x) && (x) <= (b))

////////////////////////////////////////////////
// X11 and drawing
//

// X modifiers
#define XK_ANY_MOD    UINT_MAX
#define XK_NO_MOD     0
#define XK_SWITCH_MOD (1<<13|1<<14)

extern double defaultfontsize;
extern double usedfontsize;

void xdrawcursor(int, int, int focused);
void xdrawline(int, int, int);
void xfinishdraw(void);
void xloadcols(void);
void xloadfonts(const char *, double);
int xsetcolorname(int, const char *);
void xseticontitle(char *);
void xsetpointermotion(int);
int xstartdraw(void);
void xunloadfonts(void);
void cresize(int, int);
void xhints(void);
int match(unsigned int, unsigned int);

#endif // _X_H
