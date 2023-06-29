/* See LICENSE for license details. */

/*
** This file mainly contains X11 stuff (drawing to the screen, window hints, etc)
** Most of that part is unchanged from ST (https://st.suckless.org/)
** the main() function and the main loop are found at the very bottom of this file
** there are a very few functions here that are interresting for configuratinos.
*/


#include <errno.h>
#include <math.h>
#include <locale.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>
#include <assert.h>

#include "se.h"
#include "x.h"
#include "config.h"
#include "extension.h"

//////////////////////////////////
// macros
//

// XEMBED messages
#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

#define IS_SET(flag)        ((win.mode & (flag)) != 0)
#define TRUERED(x)        (((x) & 0xff0000) >> 8)
#define TRUEGREEN(x)        (((x) & 0xff00))
#define TRUEBLUE(x)        (((x) & 0xff) << 8)
#define DEFAULT(a, b)        (a) = (a) ? (a) : (b)
#define MODBIT(x, set, bit)    ((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))
#define IS_TRUECOL(x)        (1 << 24 & (x))
#define ATTRCMP(a, b)        ((a).mode != (b).mode || (a).fg != (b).fg || (a).bg != (b).bg)
#define DIVCEIL(n, d)        (((n) + ((d) - 1)) / (d))

#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>
#include <X11/XKBlib.h>

// Purely graphic info
typedef struct {
        int tw, th;    // tty width and height
        int w, h;    // window width and height
        int ch;        // char height
        int cw;        // char width
        int mode;    // window state/mode flags
} TermWindow;
extern TermWindow win;

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
        int isfixed;    // is fixed geometry?
        int l, t;        // left and top offset
        int gm;            // geometry mask
} XWindow;
extern XWindow xw;

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
        rune_t unicodep;
} Fontcache;

extern Fontcache *frc;
extern int frclen;

// Drawing Context
typedef struct {
        Color *col;
        size_t collen;
        Font font, bfont, ifont, ibfont;
        GC gc;
} DC;
extern DC dc;

////////////////////////////////////////
// Internal Functions
//

static void xunloadfont(Font *);
static int xmakeglyphfontspecs(XftGlyphFontSpec *, const struct glyph *, int, int, int);
static void xdrawglyphfontspecs(const XftGlyphFontSpec *, struct glyph, int, int, int);
static void xdrawglyph(struct glyph, int, int);
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

struct screen screen;
struct glyph global_attr;

static Atom xtarget;
static char* copy_buffer;
static int copy_len;

TermWindow win;
XWindow xw;
DC dc;

// Fontcache is an array. A new font will be appended to the array.
Fontcache *frc = NULL;
int frccap = 0;
int frclen = 0;
double defaultfontsize = 0;
double usedfontsize = 0;

/////////////////////////////////////////////////
// function implementations
//

void
screen_init(int col, int row)
{
        global_attr = default_attributes;

        screen.col = 0;
        screen.row = 0;
        screen.lines = NULL;
        screen_resize(col, row);
}

void
draw_horisontal_line(int y, int x1, int x2)
{
        if (y < 0 || y > screen.row ||
            x2 < x1 || x2 > screen.col ||
            x1 < 0 || x1 > x2-1)
                return;

        Color drawcol = dc.col[default_attributes.fg];
        XftDrawRect(xw.draw, &drawcol,
                    border_px + x1 * win.cw, border_px + (y + 1) * win.ch - cursor_thickness,
                    win.cw * (x2-x1+1), 1);
}

void
set_clipboard_copy(char* buffer, int len)
{
        if (!buffer)
                return;
        if (copy_buffer)
                free(copy_buffer);
        copy_buffer = buffer;
        copy_len = len;

        Atom clipboard;
        clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
        XSetSelectionOwner(xw.dpy, clipboard, xw.win, CurrentTime);
}

void
execute_clipbaord_event()
{
        Atom clipboard;

        clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
        XConvertSelection(xw.dpy, clipboard, xtarget, clipboard,
                          xw.win, CurrentTime);
}

int
screen_set_char(rune_t u, int x, int y)
{
        struct glyph attr = global_attr;
        if (y >= screen.row || x >= screen.col ||
            y < 0         || x < 0)
                return 1;

        if (u == 0)
                u = screen.lines[y][x].u;
        int width = wcwidth(u);
        if (width == -1)
                width = 1;
        else if (width > 1)
                attr.mode |= ATTR_WIDE;

        if (screen.lines[y][x].mode & ATTR_WIDE || attr.mode & ATTR_WIDE) {
                if (x+1 < screen.col) {
                        screen.lines[y][x+1].u = ' ';
                        screen.lines[y][x+1].mode |= ATTR_WDUMMY;
                }
        } else if (screen.lines[y][x].mode & ATTR_WDUMMY && x-1 >= 0) {
                screen.lines[y][x-1].u = ' ';
                screen.lines[y][x-1].mode &= ~ATTR_WIDE;
        }

        screen.lines[y][x] = attr;
        screen.lines[y][x].u = u;

        return width;
}

void*
xmalloc(size_t len)
{
        void *p;
        if (!(p = malloc(len)))
                die("malloc: error, reutrned NULL | errno: %s\n", strerror(errno));
        return p;
}

void*
xrealloc(void *p, size_t len)
{
        if ((p = realloc(p, len)) == NULL)
                die("realloc: error, returned NULL | errno: %s\n", strerror(errno));
        return p;
}

void
die(const char *errstr, ...)
{
        va_list ap;

        va_start(ap, errstr);
        vfprintf(stderr, errstr, ap);
        va_end(ap);
        assert(0);
}

////////////////////////////////////////////////
// X11 and drawing
//

struct glyph*
screen_set_attr(int x, int y)
{
        static struct glyph dummy;
        if (y >= screen.row || x >= screen.col ||
            y < 0         || x < 0)
                return &dummy;

        return &screen.lines[y][x];
}

void
screen_set_region(int x1, int y1, int x2, int y2, rune_t u)
{
        for (int y = y1; y <= y2; y++)
                for (int x = x1; x <= x2; x++)
                        screen_set_char(u, x, y);
}

void
screen_resize(int col, int row)
{
        if (col < 1 || row < 1) {
                fprintf(stderr,
                        "tresize: error resizing to %dx%d\n", col, row);
                return;
        }

        // resize to new height
        for (int i = row; i < screen.row; i++)
                free(screen.lines[i]);

        screen.lines = xrealloc(screen.lines, row * sizeof(*screen.lines));

        for (int i = screen.row; i < row; i++)
                screen.lines[i] = NULL;

        // resize each row to new width, zero-pad if needed
        for (int i = 0; i < row; i++) {
                screen.lines[i] = xrealloc(screen.lines[i], col * sizeof(struct glyph));
                memset(screen.lines[i], 0, col * sizeof(struct glyph));
        }

        // update terminal size
        screen.col = col;
        screen.row = row;
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
        unsigned long nitems, ofs, rem;
        int format;
        uint8_t *data, *last, *repl;
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

                struct file_buffer* fb = get_fb(focused_window);
                if (fb->contents) {
                        if (fb->mode & FB_SELECTION_ON) {
                                fb_remove_selection(fb);
                                wb_move_cursor_to_selection_start(focused_window);
                                fb->mode &= ~(FB_SELECTION_ON);
                        }
                        call_extension(fb_paste, fb, (char*)data, nitems * format / 8);
                        call_extension(fb_contents_updated, fb, focused_window->cursor_offset, FB_CONTENT_BIG_CHANGE);
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
                                (uint8_t *) &string, 1);
                xev.property = xsre->property;
        } else if (xsre->target == xtarget || xsre->target == XA_STRING) {
                /*
                 * xith XA_STRING non ascii characters may be incorrect in the
                 * requestor. It is not our problem, use utf8.
                 */
                clipboard = XInternAtom(xw.dpy, "CLIPBOARD", 0);
                int sel_len;
                if (xsre->selection == XA_PRIMARY) {
                        seltext = fb_get_selection(get_fb(focused_window), &sel_len);
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
                                        (uint8_t *)seltext, sel_len);
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

        if (width > 0)
                win.w = width;
        if (height > 0)
                win.h = height;

        col = (win.w - 2 * border_px) / win.cw;
        row = (win.h - 2 * border_px) / win.ch;
        col = MAX(1, col);
        row = MAX(1, row);

        screen_resize(col, row);
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
         * "missing struct glyph" lookups.
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
                        PropModeReplace, (uint8_t *)&thispid, 1);

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
xmakeglyphfontspecs(XftGlyphFontSpec *specs, const struct glyph *glyphs, int len, int x, int y)
{
        float winx = border_px + x * win.cw, winy = border_px + y * win.ch, xp, yp;
        unsigned short mode, prevmode = USHRT_MAX;
        Font *font = &dc.font;
        int frcflags = FRC_NORMAL;
        float runewidth = win.cw;
        rune_t rune;
        FT_UInt glyphidx;
        FcResult fcres;
        FcPattern *fcpattern, *fontpattern;
        FcFontSet *fcsets[] = { NULL };
        FcCharSet *fccharset;
        int i, f, numspecs = 0;

        for (i = 0, xp = winx, yp = winy + font->ascent; i < len; ++i) {
                /* Fetch rune and mode for current struct glyph. */
                rune = glyphs[i].u;
                mode = glyphs[i].mode;

                /* Skip dummy wide-character spacing. */
                if (mode & ATTR_WDUMMY)
                        continue;

                /* Determine font for struct glyph if different from previous struct glyph. */
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
                        /* We got a default font for a not found struct glyph. */
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
xdrawglyphfontspecs(const XftGlyphFontSpec *specs, struct glyph base, int len, int x, int y)
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
xdrawglyph(struct glyph g, int x, int y)
{
        int numspecs;
        XftGlyphFontSpec spec;

        numspecs = xmakeglyphfontspecs(&spec, &g, 1, x, y);
        xdrawglyphfontspecs(&spec, g, numspecs, x, y);
}

void
xdrawcursor(int cx, int cy, int focused)
{
        LIMIT(cx, 0, screen.col-1);
        LIMIT(cy, 0, screen.row-1);
        struct glyph g = screen.lines[cy][cx];
        if (IS_SET(MODE_HIDE)) return;

        g.mode &= ATTR_ITALIC|ATTR_UNDERLINE|ATTR_STRUCK|ATTR_WIDE;
        g.fg = cursor_bg;
        g.bg = cursor_fg;
        Color drawcol = dc.col[g.bg];

        /* draw the new one */
        if (IS_SET(MODE_FOCUSED) && !(get_fb(focused_window)->mode & FB_SELECTION_ON) && focused) {
                switch (cursor_shape) {
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
xdrawline(int x1, int y1, int x2)
{
        LIMIT(y1, 0, screen.row);
        LIMIT(x2, 0, screen.col);
        LIMIT(x1, 0, x2);
        struct glyph* line = screen.lines[y1];
        int i, x, ox, numspecs;
        struct glyph base, new;
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
        soft_assert(f, return;);
        soft_assert(f->match, return;);
        soft_assert(f->pattern, return;);
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
match(unsigned int mask, unsigned int state) {
        const unsigned int ignoremod = Mod2Mask|XK_SWITCH_MOD;
        return mask == XK_ANY_MOD || mask == (state & ~ignoremod);
}

static void
search_term_string_insert(const char* buf, int buflen)
{
        static int first = 0;

        struct file_buffer* fb = get_fb(focused_window);
        if (!buf) {
                first = 1;
                return;
        }
        if (first) {
                *fb->search_term = 0;
                first = 0;
        }
        if (buf[0] >= 32 || buflen > 1) {
                int len = strlen(fb->search_term);
                if (len + buflen + 1 > SEARCH_TERM_MAX_LEN)
                        return;
                memcpy(fb->search_term + len, buf, buflen);
                fb->search_term[len + buflen] = 0;
                if (fb->mode & FB_SEARCH_BLOCKING_BACKWARDS)
                        focused_window->cursor_offset = fb_seek_string_backwards(fb, focused_window->cursor_offset, fb->search_term);
                else
                        focused_window->cursor_offset = fb_seek_string(fb, focused_window->cursor_offset, fb->search_term);
                writef_to_status_bar("search: %s", fb->search_term);
        }
}


static int
search_term_actions(KeySym keysym, int modkey)
{
        static int first = 0;
        struct file_buffer* fb = get_fb(focused_window);
        if (keysym == XK_Return || keysym == XK_Escape) {
                int count = fb_count_string_instances(fb, fb->search_term, 0, NULL);
                if (!count) {
                        fb->mode &= ~FB_SEARCH_BLOCKING_MASK;
                        writef_to_status_bar("no resulrs for \"%s\"", fb->search_term);
                } else {
                        fb->mode &= ~FB_SEARCH_BLOCKING;
                        fb->mode &= ~FB_SEARCH_BLOCKING_BACKWARDS;
                        fb->mode |= FB_SEARCH_BLOCKING_IDLE;

                        writef_to_status_bar("%d results for \"%s\"", count, fb->search_term);
                        if (fb->mode & FB_SEARCH_BLOCKING_BACKWARDS)
                                focused_window->cursor_offset = fb_seek_string_backwards(fb, focused_window->cursor_offset, fb->search_term);
                        else
                                focused_window->cursor_offset = fb_seek_string(fb, focused_window->cursor_offset, fb->search_term);
                }
                first = 1;
                search_term_string_insert(NULL, 0);
                return 1;
        }
        if (keysym == XK_BackSpace) {
                if (first) {
                        first = 0;
                        *fb->search_term = 0;
                } else {
                        utf8_remove_string_end(fb->search_term);
                        focused_window->cursor_offset = wb_seek_string_wrap(focused_window, focused_window->cursor_offset, fb->search_term);
                }
                writef_to_status_bar("search: %s", fb->search_term);
                return 1;
        }
        first = 0;
        return 0;
}


void
kpress(XEvent *ev)
{
        XKeyEvent *e = &ev->xkey;
        KeySym ksym;
        char buf[64];
        int len;
        rune_t c;
        Status status;

        if (IS_SET(MODE_KBDLOCK))
                return;

        if (xw.ime.xic)
                len = XmbLookupString(xw.ime.xic, e, buf, sizeof(buf), &ksym, &status);
        else
                len = XLookupString(e, buf, sizeof(buf), &ksym, NULL);
        if (len == 1 && e->state & Mod1Mask) {
                if (*buf < 0177) {
                        c = *buf | 0x80;
                        len = utf8_encode(c, buf);
                }
        }

        const struct file_buffer* fb = get_fb(focused_window);
        // keysym callback
        if (fb->mode & FB_SEARCH_BLOCKING) {
                if (search_term_actions(ksym, e->state))
                        return;
        }
        if (fb->mode & FB_SEARCH_BLOCKING) {
                search_term_string_insert(buf, len);
                return;
        }

        if (focused_window->mode != WB_NORMAL) {
                int override = 0;
                call_extension(wn_custom_window_keypress_override, &override, focused_node, ksym, e->state, buf, len);
                if (override)
                        return;

                int wn_custom_window_keypress_override_callback_exists = 0;
                extension_callback_exists(wn_custom_window_keypress_override, wn_custom_window_keypress_override_callback_exists = 1;);
                soft_assert(wn_custom_window_keypress_override_callback_exists, );
        }

        call_extension(keypress, ksym, e->state, buf, len);
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
        writef_to_status_bar("window resize: %d:%d", screen.col, screen.row);
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

                screen_set_region(0, 0, screen.col-1, screen.row-1, ' ');
                if (screen.row-2 >= 0)
                        window_node_draw_tree_to_screen(&root_node, 0, 0, screen.col-1, screen.row-1);
                draw_status_bar();

                xfinishdraw();
                XFlush(xw.dpy);
        }
}

int
main(int argc, char *argv[])
{
        xw.l = xw.t = 0;
        xw.isfixed = False;

        setlocale(LC_CTYPE, "");
        XSetLocaleModifiers("");
        int cols = MAX(default_cols, 1);
        int rows = MAX(default_rows, 1);
        screen_init(cols, rows);
        xinit(cols, rows);
        xsetenv();

        if (argc <= 1) {
                *focused_window = wb_new(fb_new_entry(NULL));
        } else  {
                int master_stack = 1;
                for (int i = 1; i < argc; i++) {
                        if (*argv[i] == '-') {
                                i++;
                        } else {
                                if (master_stack < 0) {
                                        window_node_split(focused_node, 0.5, WINDOW_HORISONTAL);
                                        master_stack = 0;
                                } else if (master_stack > 0) {
                                        *focused_window = wb_new(fb_new_entry(argv[i]));
                                        master_stack = -1;
                                        continue;
                                } else {
                                        window_node_split(focused_node, 0.5, WINDOW_VERTICAL);
                                }

                                if (focused_node->node2) {
                                        focused_node = focused_node->node2;
                                        focused_window = &focused_node->wb;
                                        if (!master_stack)
                                                *focused_window = wb_new(fb_new_entry(argv[i]));
                                }
                                master_stack = 0;
                        }
                }
        }

        srand(time(NULL));

        // TODO: start screen extension

        if (extensions) {
                for (int i = 0; !extensions[i].end; i++) {
                        if (extensions[i].e.init)
                                extensions[i].e.init(&extensions[i].e);
                        if (extensions[i].e.enable)
                                extensions[i].e.enable();
                }
        }

        run();

        return 0;
}
