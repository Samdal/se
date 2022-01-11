/* See LICENSE for license details. */
#ifndef _WIN_H
#define _WIN_H

#include "st.h"

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

void xclipcopy(void);
void xdrawcursor(int, int, Glyph);
void xdrawline(Line, int, int, int);
void xfinishdraw(void);
void xloadcols(void);
int xsetcolorname(int, const char *);
void xseticontitle(char *);
int xsetcursor(int);
void xsetpointermotion(int);
int xstartdraw(void);

struct file_buffer* get_file_buffer(struct window_buffer* buf);
int new_file_buffer(struct file_buffer buf);

#endif // _WIN_H
