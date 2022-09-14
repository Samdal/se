/* See LICENSE for license details. */
#ifndef _ST_H
#define _ST_H

#include "utf8.h"
#include "buffer.h"
#include "seek.h"
#include "x.h"

#define STATUS_BAR_MAX_LEN 4096

int write_string(const char* string, int y, int minx, int maxx);

// TODO: make this user addable
// so lessen the size of the root window and put this at the bottom (each frame)
extern char status_bar_contents[STATUS_BAR_MAX_LEN];
extern uint32_t status_bar_bg;
void writef_to_status_bar(const char* fmt, ...);
void draw_status_bar();

void window_node_draw_to_screen(struct window_split_node* wn);

#endif // _ST_H
