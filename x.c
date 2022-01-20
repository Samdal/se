/* See LICENSE for license details. */

/*
** This file mainly contains X11 stuff (drawing to the screen, window hints, etc)
** Most of that part is unchanged from ST (https://st.suckless.org/)
** the main() function and the main loop are found at the very bottom of this file
** there are a very few functions here that are interresting for configuratinos.
** I would suggest looking into x.h and seeing if any of the callbacks fit
** your configuration needs.
*/

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <locale.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#include "x.h"

/////////////////////////////////////////////////////
// config.c variables that must be defined
//

extern int border_px;
extern float cw_scale;
extern float ch_scale;
extern char* fontconfig;
extern const char* const colors[];
extern Glyph default_attributes;
extern unsigned int cursor_fg;
extern unsigned int cursor_bg;
extern unsigned int cursor_thickness;
extern unsigned int cursor_shape;
extern unsigned int default_cols;
extern unsigned int default_rows;

// callbacks
extern void(*draw_callback)(void);
extern void(*buffer_contents_updated)(struct file_buffer* modified_fb, int current_pos, enum buffer_content_reason reason);
// non-zero return value means the kpress function will not proceed further
extern int(*keypress_callback)(KeySym keycode, int modkey);
extern void(*string_input_callback)(const char* buf, int len);
// TODO: planned callbacks:
// buffer focused
// window focused

//////////////////////////////////
// macros
//

// X modifiers
#define XK_ANY_MOD    UINT_MAX
#define XK_NO_MOD     0
#define XK_SWITCH_MOD (1<<13|1<<14)

// XEMBED messages
#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

#define IS_SET(flag)		((win.mode & (flag)) != 0)
#define TRUERED(x)		(((x) & 0xff0000) >> 8)
#define TRUEGREEN(x)		(((x) & 0xff00))
#define TRUEBLUE(x)		(((x) & 0xff) << 8)
#define DEFAULT(a, b)		(a) = (a) ? (a) : (b)
#define MODBIT(x, set, bit)	((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))
#define IS_TRUECOL(x)		(1 << 24 & (x))
#define ATTRCMP(a, b)		((a).mode != (b).mode || (a).fg != (b).fg || (a).bg != (b).bg)
#define DIVCEIL(n, d)		(((n) + ((d) - 1)) / (d))

////////////////////////////////////////
// Internal Functions
//

static int file_browser_actions(KeySym keysym, int modkey);
static void file_browser_string_insert(const char* buf, int buflen);
static int xmakeglyphfontspecs(XftGlyphFontSpec *, const Glyph *, int, int, int);
static void xdrawglyphfontspecs(const XftGlyphFontSpec *, Glyph, int, int, int);
static void xdrawglyph(Glyph, int, int);
static void xclear(int, int, int, int);
static int xgeommasktogravity(int);
static int ximopen(Display *);
static void ximinstantiate(Display *, XPointer, XPointer);
static void ximdestroy(XIM, XPointer, XPointer);
static int xicdestroy(XIC, XPointer, XPointer);
static void xinit(int, int);
static void xresize(int, int);
static int xloadcolor(int, const char *, Color *);
static int xloadfont(Font *, FcPattern *);
static void xsetenv(void);
static void xseturgency(int);
static void xsettitle(char *);
static void run(void);

///////////////////////////////////////////////////
// X11 events
//

static void expose(XEvent *);
static void visibility(XEvent *);
static void unmap(XEvent *);
static void selnotify(XEvent *);
static void propnotify(XEvent *e);
static void selrequest(XEvent *);
static void kpress(XEvent *);
static void cmessage(XEvent *);
static void resize(XEvent *);
static void focus(XEvent *);

static void (*handler[LASTEvent])(XEvent *) = {
	[KeyPress] = kpress,
	[ClientMessage] = cmessage,
	[ConfigureNotify] = resize,
	[VisibilityNotify] = visibility,
	[UnmapNotify] = unmap,
	[Expose] = expose,
	[FocusIn] = focus,
	[FocusOut] = focus,
	[PropertyNotify] = propnotify,
	[SelectionNotify] = selnotify,
	[SelectionRequest] = selrequest,
};

////////////////////////////////////////////////
// Globals
//

extern Term term;
static struct file_buffer* file_buffers;
static int available_buffer_slots = 0;
struct window_split_node root_node = {.mode = WINDOW_SINGULAR};

// cursor is set when calling buffer_write_to_screen and the buffer is focused_window
struct window_split_node* focused_node = &root_node;
struct window_buffer* focused_window = &root_node.window;

Atom xtarget;
char* copy_buffer;
int copy_len;
TermWindow win;
DC dc;
XWindow xw;

// Fontcache is an array. A new font will be appended to the array.
int frccap = 0;
Fontcache *frc = NULL;
int frclen = 0;
double defaultfontsize = 0;
double usedfontsize = 0;

/////////////////////////////////////////////////
// function implementations
//

struct file_buffer*
get_file_buffer(struct window_buffer* buf)
{
	assert(buf);
	assert(file_buffers);

	if (buf->buffer_index < 0)
		buf->buffer_index = available_buffer_slots-1;
	else if (buf->buffer_index >= available_buffer_slots)
		buf->buffer_index = 0;

	if (!file_buffers[buf->buffer_index].contents) {
		for(int n = 0; n < available_buffer_slots; n++) {
			if (file_buffers[n].contents) {
				buf->buffer_index = n;
				assert(file_buffers[n].contents);
				return &file_buffers[n];
			}
		}
	} else {
		assert(file_buffers[buf->buffer_index].contents);
		return &file_buffers[buf->buffer_index];
	}

	buf->buffer_index = new_file_buffer_entry(NULL);
	return get_file_buffer(buf);
}

int
new_file_buffer_entry(const char* file_path)
{
	static char full_path[PATH_MAX];
	if (!file_path)
		file_path = "./";
	assert(strlen(file_path) < PATH_MAX);

	char* res = realpath(file_path, full_path);
	if (available_buffer_slots) {
		if (res) {
			for(int n = 0; n < available_buffer_slots; n++)
				if (file_buffers[n].contents)
					if (strcmp(file_buffers[n].file_path, full_path) == 0)
						return n;
		} else {
			strcpy(full_path, file_path);
		}

		for(int n = 0; n < available_buffer_slots; n++) {
			if (!file_buffers[n].contents) {
				file_buffers[n] = buffer_new(full_path);
				return n;
			}
		}
	}

	available_buffer_slots++;
	file_buffers = xrealloc(file_buffers, sizeof(struct file_buffer) * available_buffer_slots);
	file_buffers[available_buffer_slots-1] = buffer_new(full_path);
	return available_buffer_slots-1;
}

void
destroy_file_buffer_entry(struct window_split_node* node, struct window_split_node* root)
{
	// do not allow deletion of the lst file buffer
	int n = 0;
	for(; n < available_buffer_slots; n++)
		if (file_buffers[n].contents && n != node->window.buffer_index)
			break;
	if (n >= available_buffer_slots)
		return;

	if (window_other_nodes_contain_file_buffer(node, root)) {
		node->window.buffer_index++;
		node->window = window_buffer_new(node->window.buffer_index);
		return;
	}
	buffer_destroy(get_file_buffer(&node->window));

	if (node->window.mode == WINDOW_BUFFER_FILE_BROWSER)
		node->window = window_buffer_new(node->window.cursor_col);
	else
		node->window = window_buffer_new(node->window.buffer_index);
}

int
delete_selection(struct file_buffer* buf)
{
	if (buf->mode & BUFFER_SELECTION_ON) {
		buffer_remove_selection(buf);
		buffer_move_cursor_to_selection_start(focused_window);
		buf->mode &= ~(BUFFER_SELECTION_ON);
		return 1;
	}
	return 0;
}

void
propnotify(XEvent *e)
{
	XPropertyEvent *xpev;
	Atom clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);

	xpev = &e->xproperty;
	if (xpev->state == PropertyNewValue &&
		(xpev->atom == XA_PRIMARY ||
		 xpev->atom == clipboard)) {
		selnotify(e);
	}
}

void
selnotify(XEvent *e)
{
	ulong nitems, ofs, rem;
	int format;
	uchar *data, *last, *repl;
	Atom type, incratom, property = None;

	incratom = XInternAtom(xw.dpy, "INCR", 0);

	ofs = 0;
	if (e->type == SelectionNotify)
		property = e->xselection.property;
	else if (e->type == PropertyNotify)
		property = e->xproperty.atom;

	if (property == None)
		return;

	do {
		if (XGetWindowProperty(xw.dpy, xw.win, property, ofs,
							   BUFSIZ/4, False, AnyPropertyType,
							   &type, &format, &nitems, &rem,
							   &data)) {
			fprintf(stderr, "Clipboard allocation failed\n");
			return;
		}

		if (e->type == PropertyNotify && nitems == 0 && rem == 0) {
			/*
			 * If there is some PropertyNotify with no data, then
			 * this is the signal of the selection owner that all
			 * data has been transferred. We won't need to receive
			 * PropertyNotify events anymore.
			 */
			MODBIT(xw.attrs.event_mask, 0, PropertyChangeMask);
			XChangeWindowAttributes(xw.dpy, xw.win, CWEventMask,
									&xw.attrs);
		}

		if (type == incratom) {
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			MODBIT(xw.attrs.event_mask, 1, PropertyChangeMask);
			XChangeWindowAttributes(xw.dpy, xw.win, CWEventMask,
									&xw.attrs);

			// Deleting the property is the transfer start signal.
			XDeleteProperty(xw.dpy, xw.win, (int)property);
			continue;
		}

		// replace all '\r' with '\n'.
		repl = data;
		last = data + nitems * format / 8;
		while ((repl = memchr(repl, '\r', last - repl))) {
			*repl++ = '\n';
		}

		struct file_buffer* fb = get_file_buffer(focused_window);
		if (fb->contents) {
			if (fb->mode & BUFFER_SELECTION_ON) {
				buffer_remove_selection(fb);
				buffer_move_cursor_to_selection_start(focused_window);
				fb->mode &= ~(BUFFER_SELECTION_ON);
			}
			int offset = focused_window->cursor_offset;
			buffer_insert(fb, (char*)data, nitems * format / 8, offset, 1);
			buffer_move_to_offset(focused_window, offset + (nitems * format / 8), 0);
			if (buffer_contents_updated)
				buffer_contents_updated(fb, offset, BUFFER_CONTENT_BIG_CHANGE);
		}
		XFree(data);
		/* number of 32-bit chunks returned */
		ofs += nitems * format / 32;
	} while (rem > 0);

	/*
	 * Deleting the property again tells the selection owner to send the
	 * next data chunk in the property.
	 */
	XDeleteProperty(xw.dpy, xw.win, (int)property);
}

void
selrequest(XEvent *e)
{
	XSelectionRequestEvent *xsre = (XSelectionRequestEvent *) e;
	XSelectionEvent xev = {0};
	Atom xa_targets, string, clipboard;
	char *seltext = NULL;

	xev.type = SelectionNotify;
	xev.requestor = xsre->requestor;
	xev.selection = xsre->selection;
	xev.target = xsre->target;
	xev.time = xsre->time;
	if (xsre->property == None)
		xsre->property = xsre->target;

	// reject
	xev.property = None;

	xa_targets = XInternAtom(xw.dpy, "TARGETS", 0);
	if (xsre->target == xa_targets) {
		// respond with the supported type
		string = xtarget;
		XChangeProperty(xsre->display, xsre->requestor, xsre->property,
						XA_ATOM, 32, PropModeReplace,
						(uchar *) &string, 1);
		xev.property = xsre->property;
	} else if (xsre->target == xtarget || xsre->target == XA_STRING) {
		/*
		 * xith XA_STRING non ascii characters may be incorrect in the
		 * requestor. It is not our problem, use utf8.
		 */
		clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
		int sel_len;
		if (xsre->selection == XA_PRIMARY) {
			seltext = buffer_get_selection(get_file_buffer(focused_window), &sel_len);
		} else if (xsre->selection == clipboard) {
			seltext = copy_buffer;
			sel_len = copy_len;
		} else {
			fprintf(stderr,
					"Unhandled clipboard selection 0x%lx\n",
					xsre->selection);
			return;
		}
		if (seltext) {
			XChangeProperty(xsre->display, xsre->requestor,
							xsre->property, xsre->target,
							8, PropModeReplace,
							(uchar *)seltext, sel_len);
			xev.property = xsre->property;
			if (seltext != copy_buffer)
				free(seltext);
		}
	}

	// all done, send a notification to the listener
	if (!XSendEvent(xsre->display, xsre->requestor, 1, 0, (XEvent *) &xev))
		fprintf(stderr, "Error sending SelectionNotify event\n");
}

void
cresize(int width, int height)
{
	int col, row;

	if (width != 0)
		win.w = width;
	if (height != 0)
		win.h = height;

	col = (win.w - 2 * border_px) / win.cw;
	row = (win.h - 2 * border_px) / win.ch;
	col = MAX(1, col);
	row = MAX(1, row);

	tresize(col, row);
	xresize(col, row);
}

void
xresize(int col, int row)
{
	win.tw = col * win.cw;
	win.th = row * win.ch;

	XFreePixmap(xw.dpy, xw.buf);
	xw.buf = XCreatePixmap(xw.dpy, xw.win, win.w, win.h,
						   DefaultDepth(xw.dpy, xw.scr));
	XftDrawChange(xw.draw, xw.buf);
	xclear(0, 0, win.w, win.h);

	// resize to new width
	xw.specbuf = xrealloc(xw.specbuf, col * sizeof(GlyphFontSpec));
}

int
xloadcolor(int i, const char *name, Color *ncolor)
{
	if (!name) {
		if (!(name = colors[i]))
			return 0;
	}

	return XftColorAllocName(xw.dpy, xw.vis, xw.cmap, name, ncolor);
}

void
xloadcols(void)
{
	int i;
	static int loaded;

	if (loaded) {
		Color *cp;
		for (cp = dc.col; cp < &dc.col[dc.collen]; ++cp)
			XftColorFree(xw.dpy, xw.vis, xw.cmap, cp);
	} else {
		i = 0;
		while (colors[i++])
			;
		dc.collen  = i;
		dc.col = xmalloc(dc.collen * sizeof(Color));
		loaded = 1;
	}

	for (i = 0; i < dc.collen; i++) {
		if (!xloadcolor(i, NULL, &dc.col[i])) {
			if (colors[i])
				die("could not allocate color '%s'\n", colors[i]);
		}
	}
}

int
xsetcolorname(int x, const char *name)
{
	Color ncolor;

	if (!BETWEEN(x, 0, dc.collen))
		return 1;

	if (!xloadcolor(x, name, &ncolor))
		return 1;

	XftColorFree(xw.dpy, xw.vis, xw.cmap, &dc.col[x]);
	dc.col[x] = ncolor;

	return 0;
}


// Absolute coordinates.
void
xclear(int x1, int y1, int x2, int y2)
{
	XftDrawRect(xw.draw, &dc.col[default_attributes.bg],
				x1, y1, x2-x1, y2-y1);
}

void
xhints(void)
{
	XClassHint class = {"se", "se"};
	XWMHints wm = {.flags = InputHint, .input = 1};
	XSizeHints *sizeh;

	sizeh = XAllocSizeHints();

	sizeh->flags = PSize | PResizeInc | PBaseSize | PMinSize;
	sizeh->height = win.h;
	sizeh->width = win.w;
	sizeh->height_inc = win.ch;
	sizeh->width_inc = win.cw;
	sizeh->base_height = 2 * border_px;
	sizeh->base_width = 2 * border_px;
	sizeh->min_height = win.ch + 2 * border_px;
	sizeh->min_width = win.cw + 2 * border_px;
	if (xw.isfixed) {
		sizeh->flags |= PMaxSize;
		sizeh->min_width = sizeh->max_width = win.w;
		sizeh->min_height = sizeh->max_height = win.h;
	}
	if (xw.gm & (XValue|YValue)) {
		sizeh->flags |= USPosition | PWinGravity;
		sizeh->x = xw.l;
		sizeh->y = xw.t;
		sizeh->win_gravity = xgeommasktogravity(xw.gm);
	}

	XSetWMProperties(xw.dpy, xw.win, NULL, NULL, NULL, 0, sizeh, &wm,
					 &class);
	XFree(sizeh);
}

int
xgeommasktogravity(int mask)
{
	switch (mask & (XNegative|YNegative)) {
	case 0:
		return NorthWestGravity;
	case XNegative:
		return NorthEastGravity;
	case YNegative:
		return SouthWestGravity;
	}

	return SouthEastGravity;
}

int
xloadfont(Font *f, FcPattern *pattern)
{
	FcPattern *configured;
	FcPattern *match;
	FcResult result;
	XGlyphInfo extents;
	int wantattr, haveattr;

	/*
	 * Manually configure instead of calling XftMatchFont
	 * so that we can use the configured pattern for
	 * "missing glyph" lookups.
	 */
	configured = FcPatternDuplicate(pattern);
	if (!configured)
		return 1;

	FcConfigSubstitute(NULL, configured, FcMatchPattern);
	XftDefaultSubstitute(xw.dpy, xw.scr, configured);

	match = FcFontMatch(NULL, configured, &result);
	if (!match) {
		FcPatternDestroy(configured);
		return 1;
	}

	if (!(f->match = XftFontOpenPattern(xw.dpy, match))) {
		FcPatternDestroy(configured);
		FcPatternDestroy(match);
		return 1;
	}

	if ((XftPatternGetInteger(pattern, "slant", 0, &wantattr) ==
		 XftResultMatch)) {
		/*
		 * Check if xft was unable to find a font with the appropriate
		 * slant but gave us one anyway. Try to mitigate.
		 */
		if ((XftPatternGetInteger(f->match->pattern, "slant", 0,
								  &haveattr) != XftResultMatch) || haveattr < wantattr) {
			f->badslant = 1;
			fputs("font slant does not match\n", stderr);
		}
	}

	if ((XftPatternGetInteger(pattern, "weight", 0, &wantattr) ==
		 XftResultMatch)) {
		if ((XftPatternGetInteger(f->match->pattern, "weight", 0,
								  &haveattr) != XftResultMatch) || haveattr != wantattr) {
			f->badweight = 1;
			fputs("font weight does not match\n", stderr);
		}
	}

	// Printable characters in ASCII, used to estimate the advance width of single wide characters.
	const char ascii_printable[] =
		" !\"#$%&'()*+,-./0123456789:;<=>?"
		"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
		"`abcdefghijklmnopqrstuvwxyz{|}~";

	XftTextExtentsUtf8(xw.dpy, f->match,
					   (const FcChar8 *) ascii_printable,
					   strlen(ascii_printable), &extents);

	f->set = NULL;
	f->pattern = configured;

	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	f->lbearing = 0;
	f->rbearing = f->match->max_advance_width;

	f->height = f->ascent + f->descent;
	f->width = DIVCEIL(extents.xOff, strlen(ascii_printable));

	return 0;
}

void
xloadfonts(const char *fontstr, double fontsize)
{
	FcPattern *pattern;
	double fontval;

	if (fontstr[0] == '-')
		pattern = XftXlfdParse(fontstr, False, False);
	else
		pattern = FcNameParse((const FcChar8 *)fontstr);

	if (!pattern)
		die("can't open font %s\n", fontstr);

	if (fontsize > 1) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, (double)fontsize);
		usedfontsize = fontsize;
	} else {
		if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontval) ==
			FcResultMatch) {
			usedfontsize = fontval;
		} else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontval) ==
				   FcResultMatch) {
			usedfontsize = -1;
		} else {
			/*
			 * Default font size is 12, if none given. This is to
			 * have a known usedfontsize value.
			 */
			FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
			usedfontsize = 12;
		}
		defaultfontsize = usedfontsize;
	}

	if (xloadfont(&dc.font, pattern))
		die("can't open font %s\n", fontstr);

	if (usedfontsize < 0) {
		FcPatternGetDouble(dc.font.match->pattern,
		                   FC_PIXEL_SIZE, 0, &fontval);
		usedfontsize = fontval;
		if (fontsize == 0)
			defaultfontsize = fontval;
	}

	/* Setting character width and height. */
	win.cw = ceilf(dc.font.width * cw_scale);
	win.ch = ceilf(dc.font.height * ch_scale);

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	if (xloadfont(&dc.ifont, pattern))
		die("can't open font %s\n", fontstr);

	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	if (xloadfont(&dc.ibfont, pattern))
		die("can't open font %s\n", fontstr);

	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	if (xloadfont(&dc.bfont, pattern))
		die("can't open font %s\n", fontstr);

	FcPatternDestroy(pattern);
}

int
ximopen(Display *dpy)
{
	XIMCallback imdestroy = { .client_data = NULL, .callback = ximdestroy };
	XICCallback icdestroy = { .client_data = NULL, .callback = xicdestroy };

	xw.ime.xim = XOpenIM(xw.dpy, NULL, NULL, NULL);
	if (xw.ime.xim == NULL)
		return 0;

	if (XSetIMValues(xw.ime.xim, XNDestroyCallback, &imdestroy, NULL))
		fprintf(stderr, "XSetIMValues: "
				"Could not set XNDestroyCallback.\n");

	xw.ime.spotlist = XVaCreateNestedList(0, XNSpotLocation, &xw.ime.spot,
	                                      NULL);

	if (xw.ime.xic == NULL) {
		xw.ime.xic = XCreateIC(xw.ime.xim, XNInputStyle,
		                       XIMPreeditNothing | XIMStatusNothing,
		                       XNClientWindow, xw.win,
		                       XNDestroyCallback, &icdestroy,
		                       NULL);
	}
	if (xw.ime.xic == NULL)
		fprintf(stderr, "XCreateIC: Could not create input context.\n");

	return 1;
}

void
ximinstantiate(Display *dpy, XPointer client, XPointer call)
{
	if (ximopen(dpy))
		XUnregisterIMInstantiateCallback(xw.dpy, NULL, NULL, NULL,
		                                 ximinstantiate, NULL);
}

void
ximdestroy(XIM xim, XPointer client, XPointer call)
{
	xw.ime.xim = NULL;
	XRegisterIMInstantiateCallback(xw.dpy, NULL, NULL, NULL,
	                               ximinstantiate, NULL);
	XFree(xw.ime.spotlist);
}

int
xicdestroy(XIC xim, XPointer client, XPointer call)
{
	xw.ime.xic = NULL;
	return 1;
}

void
xinit(int cols, int rows)
{
	XGCValues gcvalues;
	Cursor cursor;
	Window parent;
	pid_t thispid = getpid();
	XColor xmousefg, xmousebg;

	if (!(xw.dpy = XOpenDisplay(NULL)))
		die("can't open display\n");
	xw.scr = XDefaultScreen(xw.dpy);
	xw.vis = XDefaultVisual(xw.dpy, xw.scr);

	/* font */
	if (!FcInit())
		die("could not init fontconfig.\n");

	xloadfonts(fontconfig, 0);

	/* colors */
	xw.cmap = XDefaultColormap(xw.dpy, xw.scr);
	xloadcols();

	/* adjust fixed window geometry */
	win.w = 2 * border_px + cols * win.cw;
	win.h = 2 * border_px + rows * win.ch;
	if (xw.gm & XNegative)
		xw.l += DisplayWidth(xw.dpy, xw.scr) - win.w - 2;
	if (xw.gm & YNegative)
		xw.t += DisplayHeight(xw.dpy, xw.scr) - win.h - 2;

	/* Events */
	xw.attrs.background_pixel = dc.col[default_attributes.bg].pixel;
	xw.attrs.border_pixel = dc.col[default_attributes.bg].pixel;
	xw.attrs.bit_gravity = NorthWestGravity;
	xw.attrs.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask
		| ExposureMask | VisibilityChangeMask | StructureNotifyMask
		| ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	xw.attrs.colormap = xw.cmap;

	parent = XRootWindow(xw.dpy, xw.scr);
	xw.win = XCreateWindow(xw.dpy, parent, xw.l, xw.t,
						   win.w, win.h, 0, XDefaultDepth(xw.dpy, xw.scr), InputOutput,
						   xw.vis, CWBackPixel | CWBorderPixel | CWBitGravity
						   | CWEventMask | CWColormap, &xw.attrs);

	memset(&gcvalues, 0, sizeof(gcvalues));
	gcvalues.graphics_exposures = False;
	dc.gc = XCreateGC(xw.dpy, parent, GCGraphicsExposures,
					  &gcvalues);
	xw.buf = XCreatePixmap(xw.dpy, xw.win, win.w, win.h,
						   DefaultDepth(xw.dpy, xw.scr));
	XSetForeground(xw.dpy, dc.gc, dc.col[default_attributes.bg].pixel);
	XFillRectangle(xw.dpy, xw.buf, dc.gc, 0, 0, win.w, win.h);

	/* font spec buffer */
	xw.specbuf = xmalloc(cols * sizeof(GlyphFontSpec));

	/* Xft rendering context */
	xw.draw = XftDrawCreate(xw.dpy, xw.buf, xw.vis, xw.cmap);

	/* input methods */
	if (!ximopen(xw.dpy)) {
		XRegisterIMInstantiateCallback(xw.dpy, NULL, NULL, NULL,
									   ximinstantiate, NULL);
	}

	/* white cursor, black outline */
	cursor = XCreateFontCursor(xw.dpy, XC_xterm);
	XDefineCursor(xw.dpy, xw.win, cursor);

	if (XParseColor(xw.dpy, xw.cmap, colors[cursor_fg], &xmousefg) == 0) {
		xmousefg.red   = 0xffff;
		xmousefg.green = 0xffff;
		xmousefg.blue  = 0xffff;
	}

	if (XParseColor(xw.dpy, xw.cmap, colors[cursor_bg], &xmousebg) == 0) {
		xmousebg.red   = 0x0000;
		xmousebg.green = 0x0000;
		xmousebg.blue  = 0x0000;
	}

	XRecolorCursor(xw.dpy, cursor, &xmousefg, &xmousebg);

	xw.xembed = XInternAtom(xw.dpy, "_XEMBED", False);
	xw.wmdeletewin = XInternAtom(xw.dpy, "WM_DELETE_WINDOW", False);
	xw.netwmname = XInternAtom(xw.dpy, "_NET_WM_NAME", False);
	xw.netwmiconname = XInternAtom(xw.dpy, "_NET_WM_ICON_NAME", False);
	XSetWMProtocols(xw.dpy, xw.win, &xw.wmdeletewin, 1);

	xw.netwmpid = XInternAtom(xw.dpy, "_NET_WM_PID", False);
	XChangeProperty(xw.dpy, xw.win, xw.netwmpid, XA_CARDINAL, 32,
					PropModeReplace, (uchar *)&thispid, 1);

	win.mode = MODE_NUMLOCK;
	xsettitle(NULL);
	xhints();
	XMapWindow(xw.dpy, xw.win);
	XSync(xw.dpy, False);

	xtarget = XInternAtom(xw.dpy, "UTF8_STRING", 0);
	if (xtarget == None)
		xtarget = XA_STRING;
}

int
xmakeglyphfontspecs(XftGlyphFontSpec *specs, const Glyph *glyphs, int len, int x, int y)
{
	float winx = border_px + x * win.cw, winy = border_px + y * win.ch, xp, yp;
	ushort mode, prevmode = USHRT_MAX;
	Font *font = &dc.font;
	int frcflags = FRC_NORMAL;
	float runewidth = win.cw;
	Rune rune;
	FT_UInt glyphidx;
	FcResult fcres;
	FcPattern *fcpattern, *fontpattern;
	FcFontSet *fcsets[] = { NULL };
	FcCharSet *fccharset;
	int i, f, numspecs = 0;

	for (i = 0, xp = winx, yp = winy + font->ascent; i < len; ++i) {
		/* Fetch rune and mode for current glyph. */
		rune = glyphs[i].u;
		mode = glyphs[i].mode;

		/* Skip dummy wide-character spacing. */
		if (mode & ATTR_WDUMMY)
			continue;

		/* Determine font for glyph if different from previous glyph. */
		if (prevmode != mode) {
			prevmode = mode;
			font = &dc.font;
			frcflags = FRC_NORMAL;
			runewidth = win.cw * ((mode & ATTR_WIDE) ? 2.0f : 1.0f);
			if ((mode & ATTR_ITALIC) && (mode & ATTR_BOLD)) {
				font = &dc.ibfont;
				frcflags = FRC_ITALICBOLD;
			} else if (mode & ATTR_ITALIC) {
				font = &dc.ifont;
				frcflags = FRC_ITALIC;
			} else if (mode & ATTR_BOLD) {
				font = &dc.bfont;
				frcflags = FRC_BOLD;
			}
			yp = winy + font->ascent;
		}

		/* Lookup character index with default font. */
		glyphidx = XftCharIndex(xw.dpy, font->match, rune);
		if (glyphidx) {
			specs[numspecs].font = font->match;
			specs[numspecs].glyph = glyphidx;
			specs[numspecs].x = (short)xp;
			specs[numspecs].y = (short)yp;
			xp += runewidth;
			numspecs++;
			continue;
		}

		/* Fallback on font cache, search the font cache for match. */
		for (f = 0; f < frclen; f++) {
			glyphidx = XftCharIndex(xw.dpy, frc[f].font, rune);
			/* Everything correct. */
			if (glyphidx && frc[f].flags == frcflags)
				break;
			/* We got a default font for a not found glyph. */
			if (!glyphidx && frc[f].flags == frcflags
				&& frc[f].unicodep == rune) {
				break;
			}
		}

		/* Nothing was found. Use fontconfig to find matching font. */
		if (f >= frclen) {
			if (!font->set)
				font->set = FcFontSort(0, font->pattern,
				                       1, 0, &fcres);
			fcsets[0] = font->set;

			/*
			 * Nothing was found in the cache. Now use
			 * some dozen of Fontconfig calls to get the
			 * font for one single character.
			 *
			 * Xft and fontconfig are design failures.
			 */
			fcpattern = FcPatternDuplicate(font->pattern);
			fccharset = FcCharSetCreate();

			FcCharSetAddChar(fccharset, rune);
			FcPatternAddCharSet(fcpattern, FC_CHARSET,
								fccharset);
			FcPatternAddBool(fcpattern, FC_SCALABLE, 1);

			FcConfigSubstitute(0, fcpattern,
							   FcMatchPattern);
			FcDefaultSubstitute(fcpattern);

			fontpattern = FcFontSetMatch(0, fcsets, 1,
										 fcpattern, &fcres);

			/* Allocate memory for the new cache entry. */
			if (frclen >= frccap) {
				frccap += 16;
				frc = xrealloc(frc, frccap * sizeof(Fontcache));
			}

			frc[frclen].font = XftFontOpenPattern(xw.dpy,
												  fontpattern);
			if (!frc[frclen].font)
				die("XftFontOpenPattern failed seeking fallback font: %s\n",
					strerror(errno));
			frc[frclen].flags = frcflags;
			frc[frclen].unicodep = rune;

			glyphidx = XftCharIndex(xw.dpy, frc[frclen].font, rune);

			f = frclen;
			frclen++;

			FcPatternDestroy(fcpattern);
			FcCharSetDestroy(fccharset);
		}

		specs[numspecs].font = frc[f].font;
		specs[numspecs].glyph = glyphidx;
		specs[numspecs].x = (short)xp;
		specs[numspecs].y = (short)yp;
		xp += runewidth;
		numspecs++;
	}

	return numspecs;
}

void
xdrawglyphfontspecs(const XftGlyphFontSpec *specs, Glyph base, int len, int x, int y)
{
	int charlen = len * ((base.mode & ATTR_WIDE) ? 2 : 1);
	int winx = border_px + x * win.cw, winy = border_px + y * win.ch,
	    width = charlen * win.cw;
	Color *fg, *bg, *temp, revfg, truefg, truebg;
	XRenderColor colfg, colbg;
	XRectangle r;

	/* Fallback on color display for attributes not supported by the font */
	if (base.mode & ATTR_ITALIC && base.mode & ATTR_BOLD) {
		if (dc.ibfont.badslant || dc.ibfont.badweight)
			base.fg = default_attributes.fg;
	} else if ((base.mode & ATTR_ITALIC && dc.ifont.badslant) ||
			   (base.mode & ATTR_BOLD && dc.bfont.badweight)) {
		base.fg = default_attributes.fg;
	}

	if (IS_TRUECOL(base.fg)) {
		colfg.alpha = 0xffff;
		colfg.red = TRUERED(base.fg);
		colfg.green = TRUEGREEN(base.fg);
		colfg.blue = TRUEBLUE(base.fg);
		XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colfg, &truefg);
		fg = &truefg;
	} else {
		fg = &dc.col[base.fg];
	}

	if (IS_TRUECOL(base.bg)) {
		colbg.alpha = 0xffff;
		colbg.green = TRUEGREEN(base.bg);
		colbg.red = TRUERED(base.bg);
		colbg.blue = TRUEBLUE(base.bg);
		XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colbg, &truebg);
		bg = &truebg;
	} else {
		bg = &dc.col[base.bg];
	}

	/* Change basic system colors [0-7] to bright system colors [8-15] */
	if ((base.mode & ATTR_BOLD_FAINT) == ATTR_BOLD && BETWEEN(base.fg, 0, 7))
		fg = &dc.col[base.fg + 8];

	if ((base.mode & ATTR_BOLD_FAINT) == ATTR_FAINT) {
		colfg.red = fg->color.red / 2;
		colfg.green = fg->color.green / 2;
		colfg.blue = fg->color.blue / 2;
		colfg.alpha = fg->color.alpha;
		XftColorAllocValue(xw.dpy, xw.vis, xw.cmap, &colfg, &revfg);
		fg = &revfg;
	}

	if (base.mode & ATTR_REVERSE) {
		temp = fg;
		fg = bg;
		bg = temp;
	}

	if (base.mode & ATTR_INVISIBLE)
		fg = bg;

	/* Intelligent cleaning up of the borders. */
	if (x == 0) {
		xclear(0, (y == 0)? 0 : winy, border_px,
			   winy + win.ch +
			   ((winy + win.ch >= border_px + win.th)? win.h : 0));
	}
	if (winx + width >= border_px + win.tw) {
		xclear(winx + width, (y == 0)? 0 : winy, win.w,
			   ((winy + win.ch >= border_px + win.th)? win.h : (winy + win.ch)));
	}
	if (y == 0)
		xclear(winx, 0, winx + width, border_px);
	if (winy + win.ch >= border_px + win.th)
		xclear(winx, winy + win.ch, winx + width, win.h);

	/* Clean up the region we want to draw to. */
	XftDrawRect(xw.draw, bg, winx, winy, width, win.ch);

	/* Set the clip region because Xft is sometimes dirty. */
	r.x = 0;
	r.y = 0;
	r.height = win.ch;
	r.width = width;
	XftDrawSetClipRectangles(xw.draw, winx, winy, &r, 1);

	/* Render the glyphs. */
	XftDrawGlyphFontSpec(xw.draw, fg, specs, len);

	/* Render underline and strikethrough. */
	if (base.mode & ATTR_UNDERLINE) {
		XftDrawRect(xw.draw, fg, winx, winy + dc.font.ascent + 1,
					width, 1);
	}

	if (base.mode & ATTR_STRUCK) {
		XftDrawRect(xw.draw, fg, winx, winy + 2 * dc.font.ascent / 3,
					width, 1);
	}

	/* Reset clip to none. */
	XftDrawSetClip(xw.draw, 0);
}

void
xdrawglyph(Glyph g, int x, int y)
{
	int numspecs;
	XftGlyphFontSpec spec;

	numspecs = xmakeglyphfontspecs(&spec, &g, 1, x, y);
	xdrawglyphfontspecs(&spec, g, numspecs, x, y);
}

void
xdrawcursor(int cx, int cy, int focused)
{
	LIMIT(cx, 0, term.col-1);
	LIMIT(cy, 0, term.row-1);
	Glyph g = term.line[cy][cx];
	if (IS_SET(MODE_HIDE)) return;

	g.mode &= ATTR_ITALIC|ATTR_UNDERLINE|ATTR_STRUCK|ATTR_WIDE;
	g.fg = cursor_bg;
	g.bg = cursor_fg;
	Color drawcol = dc.col[g.bg];

	/* draw the new one */
	if (IS_SET(MODE_FOCUSED) && !(get_file_buffer(focused_window)->mode & BUFFER_SELECTION_ON) && focused) {
		switch (win.cursor) {
		case 0: // Blinking Block
		case 1: // Blinking Block (Default)
		case 2: // Steady Block
			xdrawglyph(g, cx, cy);
			break;
		case 3: // Blinking Underline
		case 4: // Steady Underline
			XftDrawRect(xw.draw, &drawcol,
						border_px + cx * win.cw,
						border_px + (cy + 1) * win.ch - \
						cursor_thickness,
						win.cw, cursor_thickness);
			break;
		case 5: // Blinking bar
		case 6: // Steady bar
			XftDrawRect(xw.draw, &drawcol,
						border_px + cx * win.cw,
						border_px + cy * win.ch,
						cursor_thickness, win.ch);
			break;
		}
	} else {
		XftDrawRect(xw.draw, &drawcol,
					border_px + cx * win.cw,
					border_px + cy * win.ch,
					win.cw - 1, 1);
		XftDrawRect(xw.draw, &drawcol,
					border_px + cx * win.cw,
					border_px + cy * win.ch,
					1, win.ch - 1);
		XftDrawRect(xw.draw, &drawcol,
					border_px + (cx + 1) * win.cw - 1,
					border_px + cy * win.ch,
					1, win.ch - 1);
		XftDrawRect(xw.draw, &drawcol,
					border_px + cx * win.cw,
					border_px + (cy + 1) * win.ch - 1,
					win.cw, 1);
	}
}

void
xseticontitle(char *p)
{
	XTextProperty prop;
	DEFAULT(p, "se");

	if (Xutf8TextListToTextProperty(xw.dpy, &p, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMIconName(xw.dpy, xw.win, &prop);
	XSetTextProperty(xw.dpy, xw.win, &prop, xw.netwmiconname);
	XFree(prop.value);
}

void
xsettitle(char *p)
{
	XTextProperty prop;
	DEFAULT(p, "se");

	if (Xutf8TextListToTextProperty(xw.dpy, &p, 1, XUTF8StringStyle,
	                                &prop) != Success)
		return;
	XSetWMName(xw.dpy, xw.win, &prop);
	XSetTextProperty(xw.dpy, xw.win, &prop, xw.netwmname);
	XFree(prop.value);
}

void
xdrawline(Line line, int x1, int y1, int x2)
{
	int i, x, ox, numspecs;
	Glyph base, new;
	XftGlyphFontSpec *specs = xw.specbuf;

	numspecs = xmakeglyphfontspecs(specs, &line[x1], x2 - x1, x1, y1);
	i = ox = 0;
	for (x = x1; x < x2 && i < numspecs; x++) {
		new = line[x];
		if (new.mode & ATTR_WDUMMY)
			continue;
		if (i > 0 && ATTRCMP(base, new)) {
			xdrawglyphfontspecs(specs, base, i, ox, y1);
			specs += i;
			numspecs -= i;
			i = 0;
		}
		if (i == 0) {
			ox = x;
			base = new;
		}
		i++;
	}
	if (i > 0)
		xdrawglyphfontspecs(specs, base, i, ox, y1);
}

void xsetenv(void) {
	char buf[sizeof(long) * 8 + 1];
	snprintf(buf, sizeof(buf), "%lu", xw.win);
	setenv("WINDOWID", buf, 1);
}

int xstartdraw(void) {return IS_SET(MODE_VISIBLE);}

void xfinishdraw(void) {
	XCopyArea(xw.dpy, xw.buf, xw.win, dc.gc, 0, 0, win.w, win.h, 0, 0);
	XSetForeground(xw.dpy, dc.gc, dc.col[default_attributes.bg].pixel);
}

void expose(XEvent *ev) {} // do nothing

void visibility(XEvent *ev) {
	XVisibilityEvent *e = &ev->xvisibility;
	MODBIT(win.mode, e->state != VisibilityFullyObscured, MODE_VISIBLE);
}

void unmap(XEvent *ev) {win.mode &= ~MODE_VISIBLE;}

void
xsetpointermotion(int set)
{
	MODBIT(xw.attrs.event_mask, set, PointerMotionMask);
	XChangeWindowAttributes(xw.dpy, xw.win, CWEventMask, &xw.attrs);
}

int xsetcursor(int cursor) {
	if (!BETWEEN(cursor, 0, 7)) /* 7: st extension */
		return 1;
	win.cursor = cursor;
	return 0;
}

void xseturgency(int add) {
	XWMHints *h = XGetWMHints(xw.dpy, xw.win);
	MODBIT(h->flags, add, XUrgencyHint);
	XSetWMHints(xw.dpy, xw.win, h);
	XFree(h);
}

void
xunloadfonts(void)
{
	/* Free the loaded fonts in the font cache.  */
	while (frclen > 0)
		XftFontClose(xw.dpy, frc[--frclen].font);

	xunloadfont(&dc.font);
	xunloadfont(&dc.bfont);
	xunloadfont(&dc.ifont);
	xunloadfont(&dc.ibfont);
}

void
xunloadfont(Font *f)
{
	assert(f);
	assert(f->match);
	assert(f->pattern);
	XftFontClose(xw.dpy, f->match);
	FcPatternDestroy(f->pattern);
	if (f->set)
		FcFontSetDestroy(f->set);
}

void
focus(XEvent *ev)
{
	XFocusChangeEvent *e = &ev->xfocus;

	if (e->mode == NotifyGrab)
		return;

	if (ev->type == FocusIn) {
		if (xw.ime.xic)
			XSetICFocus(xw.ime.xic);
		win.mode |= MODE_FOCUSED;
		xseturgency(0);
	} else {
		if (xw.ime.xic)
			XUnsetICFocus(xw.ime.xic);
		win.mode &= ~MODE_FOCUSED;
	}
}

int
file_browser_actions(KeySym keysym, int modkey)
{
	static char full_path[PATH_MAX];
	static char filename[PATH_MAX];
	struct file_buffer* fb = get_file_buffer(focused_window);
	int offset = fb->len;

	switch (keysym) {
		float new_font_size;
		int new_fb;
	case XK_BackSpace:
		if (offset <= 0) {
			char* dest = strrchr(fb->file_path, '/');
			printf("%ld\n", dest - fb->file_path);
			if (dest && dest != fb->file_path) *dest = 0;
			return 1;
		}

		focused_window->cursor_offset = offset;
		buffer_move_on_line(focused_window, -1, 0);
		offset = focused_window->cursor_offset;

		buffer_remove(fb, offset, 1, 0, 0);
		focused_window->y_scroll = 0;
		return 1;
	case XK_Tab:
	case XK_Return:
	{
		char* path = file_path_get_path(fb->file_path);
		buffer_change(fb, "\0", 1, fb->len, 1);
		if (fb->len > 0) fb->len--;

		DIR *dir = opendir(path);
		for (int y = 0; file_browser_next_item(dir, path, fb->contents, full_path, filename); y++) {
			if (y == focused_window->y_scroll) {
				if (path_is_folder(full_path)) {
					strcat(full_path, "/");
					strcpy(fb->file_path, full_path);

					fb->len = 0;
					fb->contents[0] = '\0';
					focused_window->y_scroll = 0;
					focused_window->y_scroll = 0;

					free(path);
					closedir(dir);
					return 1;
				}
				goto open_file;
			}
		}

		if (fb->contents[fb->len-1] == '/') {
			free(path);
			closedir(dir);
			return 1;
		}

		strcpy(full_path, path);
		strcat(full_path, fb->contents);
open_file:
		new_fb = new_file_buffer_entry(full_path);
		destroy_file_buffer_entry(focused_node, &root_node);
		focused_node->window = window_buffer_new(new_fb);
		free(path);
		closedir(dir);
		return 1;
	}
	case XK_Down:
		focused_window->y_scroll++;
		if (focused_window->y_scroll < 0)
			focused_window->y_scroll = 0;
		return 1;
	case XK_Up:
		focused_window->y_scroll--;
		if (focused_window->y_scroll < 0)
			focused_window->y_scroll = 0;
		return 1;
	case XK_Escape:
		destroy_file_buffer_entry(focused_node, &root_node);
		return 1;

	case XK_Page_Down:
		new_font_size = usedfontsize-1.0;
		goto set_fontsize;
	case XK_Page_Up:
		new_font_size = usedfontsize+1.0;
		goto set_fontsize;
	case XK_Home:
		new_font_size = defaultfontsize;
	set_fontsize:
		xunloadfonts();
		xloadfonts(fontconfig, new_font_size);
		cresize(0, 0);
		xhints();
		return 1;
	}
	return 0;
}

void
file_browser_string_insert(const char* buf, int buflen)
{
	static char full_path[PATH_MAX];
	struct file_buffer* fb = get_file_buffer(focused_window);

	if (fb->len + buflen + strlen(fb->file_path) > PATH_MAX)
		return;

	if (buf[0] >= 32 || buflen > 1) {
		buffer_insert(fb, buf, buflen, fb->len, 0);
		buffer_move_offset_relative(focused_window, buflen, 0);
		focused_window->y_scroll = 0;

		if (*buf == '/') {
			buffer_change(fb, "\0", 1, fb->len, 0);
			if (fb->len > 0) fb->len--;
			char* path = file_path_get_path(fb->file_path);
			strcpy(full_path, path);
			strcat(full_path, fb->contents);

			free(path);

			if (path_is_folder(full_path)) {
				file_browser_actions(XK_Return, 0);
				return;
			}
		}
	} else {
		printf("unhandled control character %x\n", buf[0]);
	}
}

int match(uint mask, uint state) {
	const uint ignoremod = Mod2Mask|XK_SWITCH_MOD;
	return mask == XK_ANY_MOD || mask == (state & ~ignoremod);
}

void
kpress(XEvent *ev)
{
	XKeyEvent *e = &ev->xkey;
	KeySym ksym;
	char buf[64];
	int len;
	Rune c;
	Status status;

	if (IS_SET(MODE_KBDLOCK))
		return;

	if (xw.ime.xic)
		len = XmbLookupString(xw.ime.xic, e, buf, sizeof buf, &ksym, &status);
	else
		len = XLookupString(e, buf, sizeof buf, &ksym, NULL);

	// keysym callback
	if (focused_window->mode == WINDOW_BUFFER_FILE_BROWSER) {
		if(file_browser_actions(ksym, e->state))
			return;
	} else if (keypress_callback) {
		if (keypress_callback(ksym, e->state))
			return;
	}

	// composed string from input method and send to callback
	if (string_input_callback) {
		if (len == 0)
			return;
		if (len == 1 && e->state & Mod1Mask) {
			if (*buf < 0177) {
				c = *buf | 0x80;
				len = utf8encode(c, buf);
			}
		}
		if (focused_window->mode == WINDOW_BUFFER_FILE_BROWSER)
			file_browser_string_insert(buf, len);
		else
			string_input_callback(buf, len);
	}
}

void
cmessage(XEvent *e)
{
	// See xembed specs
	//  http://standards.freedesktop.org/xembed-spec/xembed-spec-latest.html
	if (e->xclient.message_type == xw.xembed && e->xclient.format == 32) {
		if (e->xclient.data.l[1] == XEMBED_FOCUS_IN) {
			win.mode |= MODE_FOCUSED;
			xseturgency(0);
		} else if (e->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
			win.mode &= ~MODE_FOCUSED;
		}
	} else if (e->xclient.data.l[0] == xw.wmdeletewin) {
		exit(0);
	}
}

void
resize(XEvent *e)
{
	if (e->xconfigure.width == win.w && e->xconfigure.height == win.h)
		return;

	cresize(e->xconfigure.width, e->xconfigure.height);
}

void
run(void)
{
	XEvent ev;
	int w = win.w, h = win.h;

	// Waiting for window mapping
	do {
		XNextEvent(xw.dpy, &ev);
		/*
		 * This XFilterEvent call is required because of XOpenIM. It
		 * does filter out the key event and some client message for
		 * the input method too.
		 */
		if (XFilterEvent(&ev, None))
			continue;
		if (ev.type == ConfigureNotify) {
			w = ev.xconfigure.width;
			h = ev.xconfigure.height;
		}
	} while (ev.type != MapNotify);

	cresize(w, h);

	for (;;) {

		int xev = 0;
		while (XPending(xw.dpy)) {
			XNextEvent(xw.dpy, &ev);
			if (XFilterEvent(&ev, None))
				continue;
			if (handler[ev.type]) {
				(handler[ev.type])(&ev);
				xev = 1;
			}
		}

		if (!xev) {
			nanosleep(&(struct timespec){.tv_nsec = 1e6}, NULL);
			continue;
		}

		tsetregion(0, 0, term.col-1, term.row-1, ' ');
		window_draw_tree_to_screen(&root_node, 0, 0, term.col-1, term.row-1);

		if (draw_callback)
			draw_callback();

		xfinishdraw();
		XFlush(xw.dpy);
	}
}

int
main(int argc, char *argv[])
{
	xw.l = xw.t = 0;
	xw.isfixed = False;
	xsetcursor(cursor_shape);

	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	int cols = MAX(default_cols, 1);
	int rows = MAX(default_rows, 1);
	tnew(cols, rows);
	xinit(cols, rows);
	xsetenv();

	if (argc <= 1) {
		*focused_window = window_buffer_new(new_file_buffer_entry(NULL));
	} else  {
		int master_stack = 1;
		for (int i = 1; i < argc; i++) {
			if (*argv[i] == '-') {
				i++;
			} else {
				if (master_stack < 0) {
					window_node_split(focused_node, 0.5, WINDOW_HORISONTAL);
					master_stack = 0;
				} else if (master_stack) {
					*focused_window = window_buffer_new(new_file_buffer_entry(argv[i]));
					master_stack = -1;
					continue;
				} else {
					window_node_split(focused_node, 0.5, WINDOW_VERTICAL);
				}
				if (focused_node->node2) {
					focused_node = focused_node->node2;
					focused_window = &focused_node->window;
					if (!master_stack)
						*focused_window = window_buffer_new(new_file_buffer_entry(argv[i]));
				}
				master_stack = 0;
			}
		}
	}

	run();

	return 0;
}
