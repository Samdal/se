/* See LICENSE for license details. */
#ifndef _X_H
#define _X_H

#include "se.h"
#undef Glyph

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>

#define Glyph Glyph_

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

// Purely graphic info
typedef struct {
	int tw, th;	// tty width and height
	int w, h;	// window width and height
	int ch;		// char height
	int cw;		// char width
	int mode;	// window state/mode flags
	int cursor;	// cursor style
} TermWindow;

typedef XftDraw *Draw;
typedef XftColor Color;
typedef XftGlyphFontSpec GlyphFontSpec;

typedef struct {
	Display *dpy;
	Colormap cmap;
	Window win;
	Drawable buf;
	GlyphFontSpec *specbuf; // font spec buffer used for rendering
	Atom xembed, wmdeletewin, netwmname, netwmiconname, netwmpid;
	struct {
		XIM xim;
		XIC xic;
		XPoint spot;
		XVaNestedList spotlist;
	} ime;
	Draw draw;
	Visual *vis;
	XSetWindowAttributes attrs;
	int scr;
	int isfixed;	// is fixed geometry?
	int l, t;		// left and top offset
	int gm;			// geometry mask
} XWindow;

// Font structure
#define Font Font_
typedef struct {
	int height;
	int width;
	int ascent;
	int descent;
	int badslant;
	int badweight;
	short lbearing;
	short rbearing;
	XftFont *match;
	FcFontSet *set;
	FcPattern *pattern;
} Font;

// Font Ring Cache
enum {
	FRC_NORMAL,
	FRC_ITALIC,
	FRC_BOLD,
	FRC_ITALICBOLD
};

typedef struct {
	XftFont *font;
	int flags;
	Rune unicodep;
} Fontcache;

// Drawing Context
typedef struct {
	Color *col;
	size_t collen;
	Font font, bfont, ifont, ibfont;
	GC gc;
} DC;

void xclipcopy(void);
void xdrawcursor(int, int, int focused);
void xdrawline(int, int, int);
void xfinishdraw(void);
void xloadcols(void);
void xloadfonts(const char *, double);
int xsetcolorname(int, const char *);
void xseticontitle(char *);
int xsetcursor(int);
void xsetpointermotion(int);
int xstartdraw(void);
void xunloadfonts(void);
void xunloadfont(Font *);
void cresize(int, int);
void xhints(void);
int match(uint, uint);

struct file_buffer* get_file_buffer(struct window_buffer* buf);
int new_file_buffer_entry(const char* file_path);
int destroy_file_buffer_entry(struct window_split_node* node, struct window_split_node* root);
int delete_selection(struct file_buffer* buf);
void draw_horisontal_line(int y, int x1, int x2);
// buffer MUST be malloced and NOT be freed after it is passed
void set_clipboard_copy(char* buffer, int len);
void insert_clipboard_at_cursor();

#endif // _X_H
