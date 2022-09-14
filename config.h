/*
 * ***THIS FILE IS NOT MADE FOR CUSTOMISATION, FOR CUSTOMISATION USE config.c***
 *
 * Unless you are adding new shared globals or heavily modifying the program,
 * this file just expands global symbols that the rest of the files
 * rely on to configure themselves.
 *
 * this file can also be used for reference if you are unsure what
 * variables you might be missing from your own config.
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#include "se.h"
#include "x.h"
#include "extension.h"

extern struct glyph default_attributes;
extern unsigned int alternate_bg_bright;
extern unsigned int alternate_bg_dark;
extern unsigned int cursor_fg;
extern unsigned int cursor_bg;
extern unsigned int mouse_line_bg;
extern unsigned int selection_bg;
extern unsigned int highlight_color;
extern unsigned int path_color;
extern unsigned int error_color;
extern unsigned int warning_color;
extern unsigned int ok_color;

extern int border_px;
extern float cw_scale;
extern float ch_scale;
extern char* fontconfig;
extern const char* const colors[];
extern struct glyph default_attributes;
extern unsigned int cursor_fg;
extern unsigned int cursor_bg;
extern unsigned int cursor_thickness;
extern unsigned int cursor_shape;
extern unsigned int default_cols;
extern unsigned int default_rows;

extern unsigned int tabspaces;
extern unsigned int default_indent_len; // 0 means tab
extern int wrap_buffer;

// see extension.h and extension.c
extern struct extension_meta* extensions;

#endif // CONFIG_H_
