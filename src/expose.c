/* expose.c - exposé-style overview of the windows on the current workspace
 *
 *  Window Maker window manager
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * How it works
 *
 * One override-redirect window covers the head under the pointer.  Its
 * background pixmap is rendered once with wrlib: a dark field with one
 * thumbnail per window laid out on a grid.  The highlight rectangle and
 * the titles are drawn on top with a GC, so moving the selection costs an
 * XClearWindow plus a handful of draw calls.
 *
 * Thumbnail sources, in order of preference:
 *   - a live XGetImage of the frame for mapped windows.  With a compositor
 *     (picom) redirecting windows this returns the window's own contents
 *     even when it is covered; without one, covered parts show whatever
 *     is painted on top of them.
 *   - the aspect-correct capture stored in WIcon->expose_thumb when the
 *     window was minimized (see wIconifyWindow and set_icon_expose_thumb).
 *   - the application icon, for hidden and shaded windows or when a
 *     capture failed.
 *
 * The keyboard and the pointer are grabbed while the overview is up and a
 * private event loop runs, like the Alt-Tab cycle in cycling.c.  Events
 * that are not ours are still handed to WMHandleEvent() so the window
 * manager keeps working underneath.
 */

#include "wconfig.h"

#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>

#include <WINGs/WINGsP.h>	/* for the shared XftDraw, see destroyPanel() */

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "icon.h"
#include "appicon.h"
#include "application.h"
#include "actions.h"
#include "keybind.h"
#include "defaults.h"
#include "xinerama.h"
#include "misc.h"
#include "expose.h"

#define EXPOSE_MARGIN     24	/* panel edge to first cell */
#define EXPOSE_GAP        16	/* between cells */
#define EXPOSE_BORDER      4	/* highlight ring, including a 1px outline on each side */
#define EXPOSE_LABEL_PAD   6	/* above and below the title line */
#define EXPOSE_ICON_SIZE  96	/* icon fallback size */

typedef struct {
	Window client;		/* client XID; re-looked-up before use */
	RImage *thumb;		/* already scaled to fit the cell, or NULL */
	Bool is_icon;		/* thumb is an application icon, not a capture */
	char *title;
	int x, y, w, h;		/* cell rectangle, panel coordinates */
	int tx, ty, tw, th;	/* thumbnail rectangle inside the cell */
} ExposeCell;

typedef struct {
	WScreen *scr;
	Window win;
	Pixmap base;		/* rendered thumbnails, never drawn on */
	Pixmap bg;		/* base + highlight, the window background */
	GC gc;
	WMFont *font;
	WMColor *accent;		/* highlight color */
	int x, y, width, height;	/* panel geometry on the root window */
	int cols, rows;
	int cell_w, cell_h;		/* cell size, label included */
	int label_h;
	ExposeCell *cells;
	int count;
	int current;
	Bool done;
	Bool activate;
} ExposePanel;

/* non-NULL while the overview's event loop is running */
static ExposePanel *active_panel = NULL;

/* ---------------------------------------------------------------------- */
/* window selection                                                        */

static Bool wantWindow(WScreen *scr, WWindow *wwin)
{
	if (!wwin->frame)
		return False;
	if (wwin->flags.internal_window || wwin->flags.is_dockapp)
		return False;
	if (WFLAGP(wwin, no_focusable) || WFLAGP(wwin, skip_switchpanel))
		return False;
	if (wwin->transient_for != None && wwin->transient_for != scr->root_win)
		return False;
	if (wwin->frame->workspace != scr->current_workspace && !IS_OMNIPRESENT(wwin))
		return False;
	if (!wwin->flags.mapped && !wwin->flags.shaded && !wwin->flags.miniaturized && !wwin->flags.hidden)
		return False;
	return True;
}

static int collectWindows(WScreen *scr, ExposeCell **out)
{
	WWindow *wwin;
	ExposeCell *cells;
	int n = 0, i;

	for (wwin = scr->focused_window; wwin; wwin = wwin->prev)
		if (wantWindow(scr, wwin))
			n++;

	if (n == 0) {
		*out = NULL;
		return 0;
	}

	cells = wmalloc(sizeof(ExposeCell) * n);
	/* focus order, most recently focused first, like the switch panel */
	for (wwin = scr->focused_window, i = 0; wwin && i < n; wwin = wwin->prev) {
		if (!wantWindow(scr, wwin))
			continue;
		cells[i].client = wwin->client_win;
		cells[i].title = wstrdup(wwin->frame->title ? wwin->frame->title : "");
		i++;
	}

	*out = cells;
	return n;
}

/* ---------------------------------------------------------------------- */
/* thumbnails                                                              */

/*
 * Captures taken at minimize time also go into a property on the client
 * window, so they survive a Window Maker restart (WIcon->expose_thumb does
 * not).  Layout: CARDINAL/32 array [width, height, 0xRRGGBB * width*height].
 */
#define EXPOSE_PROP_NAME   "_WINDOWMAKER_EXPOSE_THUMB"
#define EXPOSE_PROP_MAX    400	/* longest side stored, keeps it under 640 KB */

static Atom exposeThumbAtom(void)
{
	static Atom atom = None;

	if (atom == None)
		atom = XInternAtom(dpy, EXPOSE_PROP_NAME, False);
	return atom;
}

void wExposeStoreThumb(WWindow *wwin, RImage *image)
{
	RImage *small = image;
	unsigned long *buf;
	unsigned int w = image->width, h = image->height, n, i;
	int stride = (image->format == RRGBAFormat) ? 4 : 3;
	unsigned char *src;

	if (w > EXPOSE_PROP_MAX || h > EXPOSE_PROP_MAX) {
		if (w >= h) {
			h = h * EXPOSE_PROP_MAX / w;
			w = EXPOSE_PROP_MAX;
		} else {
			w = w * EXPOSE_PROP_MAX / h;
			h = EXPOSE_PROP_MAX;
		}
		if (w < 1)
			w = 1;
		if (h < 1)
			h = 1;
		small = RSmoothScaleImage(image, w, h);
		if (!small)
			return;
		stride = (small->format == RRGBAFormat) ? 4 : 3;
	}

	n = w * h;
	buf = wmalloc(sizeof(unsigned long) * (n + 2));
	buf[0] = w;
	buf[1] = h;
	src = small->data;
	for (i = 0; i < n; i++, src += stride)
		buf[i + 2] = ((unsigned long)src[0] << 16) | ((unsigned long)src[1] << 8) | src[2];

	XChangeProperty(dpy, wwin->client_win, exposeThumbAtom(), XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)buf, n + 2);
	wfree(buf);
	if (small != image)
		RReleaseImage(small);
}

static RImage *loadStoredThumb(WWindow *wwin)
{
	Atom type;
	int format;
	unsigned long nitems, after, *data = NULL, w, h, i;
	unsigned char *dst;
	RImage *img;

	if (XGetWindowProperty(dpy, wwin->client_win, exposeThumbAtom(), 0,
			       2 + (long)EXPOSE_PROP_MAX * EXPOSE_PROP_MAX, False, XA_CARDINAL,
			       &type, &format, &nitems, &after, (unsigned char **)&data) != Success || !data)
		return NULL;

	img = NULL;
	if (type == XA_CARDINAL && format == 32 && nitems >= 2) {
		w = data[0];
		h = data[1];
		if (w >= 1 && h >= 1 && w <= EXPOSE_PROP_MAX && h <= EXPOSE_PROP_MAX && nitems >= 2 + w * h) {
			img = RCreateImage(w, h, False);
			if (img) {
				dst = img->data;
				for (i = 0; i < w * h; i++, dst += 3) {
					dst[0] = (data[i + 2] >> 16) & 0xff;
					dst[1] = (data[i + 2] >> 8) & 0xff;
					dst[2] = data[i + 2] & 0xff;
				}
			}
		}
	}
	XFree(data);
	return img;
}

static RImage *captureFrame(WScreen *scr, WWindow *wwin)
{
	XWindowAttributes attr;
	XImage *ximg;
	RImage *img;
	Window frame = wwin->frame->core->window;
	Window dummy;
	int x, y, w, h, ox = 0, oy = 0;

	if (!XGetWindowAttributes(dpy, frame, &attr) || attr.map_state != IsViewable)
		return NULL;

	XTranslateCoordinates(dpy, frame, scr->root_win, 0, 0, &x, &y, &dummy);
	w = attr.width;
	h = attr.height;

	/* XGetImage refuses rectangles that fall outside the screen */
	if (x < 0) {
		ox = -x;
		w -= ox;
	}
	if (y < 0) {
		oy = -y;
		h -= oy;
	}
	if (x + ox + w > scr->scr_width)
		w = scr->scr_width - (x + ox);
	if (y + oy + h > scr->scr_height)
		h = scr->scr_height - (y + oy);
	if (w <= 0 || h <= 0)
		return NULL;

	ximg = XGetImage(dpy, frame, ox, oy, w, h, AllPlanes, ZPixmap);
	if (!ximg)
		return NULL;

	img = RCreateImageFromXImage(scr->rcontext, ximg, NULL);
	XDestroyImage(ximg);
	return img;
}

static RImage *iconFor(WScreen *scr, WWindow *wwin, int size)
{
	RImage *image = NULL;

	if (!WFLAGP(wwin, always_user_icon) && wwin->net_icon_image)
		image = RRetainImage(wwin->net_icon_image);

	if (!image) {
		WApplication *wapp = wApplicationOf(wwin->main_window);

		if (wapp && wapp->app_icon && wapp->app_icon->icon && wapp->app_icon->icon->file_image)
			image = RRetainImage(wapp->app_icon->icon->file_image);
	}

	/* get_icon_image() includes the default icon image */
	if (!image)
		image = get_icon_image(scr, wwin->wm_instance, wwin->wm_class, size);

	return wIconValidateIconSize(image, size);
}

/* scale src down (never up) to fit max_w x max_h, keeping the aspect ratio */
static RImage *fitImage(RImage *src, int max_w, int max_h)
{
	double s, sw, sh;
	int w, h;

	sw = (double)max_w / src->width;
	sh = (double)max_h / src->height;
	s = sw < sh ? sw : sh;
	if (s >= 1.0)
		return RCloneImage(src);

	w = (int)(src->width * s);
	h = (int)(src->height * s);
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	return RSmoothScaleImage(src, w, h);
}

/* fill cell->thumb/is_icon with something that fits in max_w x max_h */
static void makeThumb(WScreen *scr, ExposeCell *cell, int max_w, int max_h)
{
	WWindow *wwin = wWindowFor(cell->client);
	RImage *src = NULL;
	Bool owned = False;

	cell->thumb = NULL;
	cell->is_icon = False;
	if (!wwin)
		return;

	if (wwin->flags.mapped && !wwin->flags.shaded) {
		src = captureFrame(scr, wwin);
		owned = (src != NULL);
	} else if (wwin->flags.miniaturized) {
		if (wwin->icon && wwin->icon->expose_thumb) {
			src = wwin->icon->expose_thumb;
		} else {
			/* minimized before this Window Maker instance started */
			src = loadStoredThumb(wwin);
			owned = (src != NULL);
		}
	}

	if (src) {
		cell->thumb = fitImage(src, max_w, max_h);
		if (owned)
			RReleaseImage(src);
	}

	if (!cell->thumb) {
		int size = EXPOSE_ICON_SIZE;

		if (size > max_w)
			size = max_w;
		if (size > max_h)
			size = max_h;
		cell->thumb = iconFor(scr, wwin, size);
		cell->is_icon = True;
	}
}

/* ---------------------------------------------------------------------- */
/* layout and rendering                                                    */

static void layoutCells(ExposePanel *p)
{
	int n = p->count;
	int W = p->width - 2 * EXPOSE_MARGIN;
	int H = p->height - 2 * EXPOSE_MARGIN;
	int c, best_c = 1;
	double best = -1.0;

	/* pick the column count that gives the biggest cells for a 16:10 window */
	for (c = 1; c <= n; c++) {
		int r = (n + c - 1) / c;
		double cw = (W - (c - 1) * EXPOSE_GAP) / (double)c;
		double ch = (H - (r - 1) * EXPOSE_GAP) / (double)r - p->label_h;
		double score;

		if (cw <= 0 || ch <= 0)
			continue;
		score = cw < ch * 1.6 ? cw : ch * 1.6;
		if (score > best) {
			best = score;
			best_c = c;
		}
	}

	p->cols = best_c;
	p->rows = (n + best_c - 1) / best_c;
	p->cell_w = (W - (p->cols - 1) * EXPOSE_GAP) / p->cols;
	p->cell_h = (H - (p->rows - 1) * EXPOSE_GAP) / p->rows;

	for (c = 0; c < n; c++) {
		ExposeCell *cell = &p->cells[c];
		int col = c % p->cols, row = c / p->cols;
		int in_row = (row == p->rows - 1) ? n - row * p->cols : p->cols;
		int offset = (p->cols - in_row) * (p->cell_w + EXPOSE_GAP) / 2;	/* center a short last row */

		cell->x = EXPOSE_MARGIN + offset + col * (p->cell_w + EXPOSE_GAP);
		cell->y = EXPOSE_MARGIN + row * (p->cell_h + EXPOSE_GAP);
		cell->w = p->cell_w;
		cell->h = p->cell_h;
	}
}

static void renderBackground(ExposePanel *p)
{
	RImage *bg = RCreateImage(p->width, p->height, False);
	RColor field = { 0x1c, 0x1c, 0x1c, 0xff };
	RColor tile = { 0x34, 0x34, 0x34, 0xff };
	int i;

	RFillImage(bg, &field);

	for (i = 0; i < p->count; i++) {
		ExposeCell *cell = &p->cells[i];
		int area_w = cell->w - 2 * EXPOSE_BORDER;
		int area_h = cell->h - p->label_h - 2 * EXPOSE_BORDER;
		int area_x = cell->x + EXPOSE_BORDER;
		int area_y = cell->y + EXPOSE_BORDER;

		if (cell->is_icon) {
			/* a plain tile so the icon has something to sit on */
			RImage *t = RCreateImage(area_w, area_h, False);

			RFillImage(t, &tile);
			RCombineArea(bg, t, 0, 0, area_w, area_h, area_x, area_y);
			RReleaseImage(t);
		}

		if (cell->thumb) {
			cell->tw = cell->thumb->width;
			cell->th = cell->thumb->height;
			cell->tx = area_x + (area_w - cell->tw) / 2;
			cell->ty = area_y + (area_h - cell->th) / 2;
			RCombineArea(bg, cell->thumb, 0, 0, cell->tw, cell->th, cell->tx, cell->ty);
		} else {
			cell->tx = area_x;
			cell->ty = area_y;
			cell->tw = area_w;
			cell->th = area_h;
		}
		if (cell->is_icon) {
			/* highlight the whole tile for icon cells */
			cell->tx = area_x;
			cell->ty = area_y;
			cell->tw = area_w;
			cell->th = area_h;
		}
	}

	RConvertImage(p->scr->rcontext, bg, &p->base);
	RReleaseImage(bg);

	p->bg = XCreatePixmap(dpy, p->scr->root_win, p->width, p->height, p->scr->w_depth);
}

/* a rectangular ring of the given thickness on the background pixmap */
static void drawRing(ExposePanel *p, int x, int y, int w, int h, int t)
{
	XFillRectangle(dpy, p->bg, p->gc, x, y, w, t);
	XFillRectangle(dpy, p->bg, p->gc, x, y + h - t, w, t);
	XFillRectangle(dpy, p->bg, p->gc, x, y, t, h);
	XFillRectangle(dpy, p->bg, p->gc, x + w - t, y, t, h);
}

static void redraw(ExposePanel *p)
{
	WScreen *scr = p->scr;
	int i;

	/* Compose highlight into the background pixmap rather than drawing on
	 * the window: it survives Expose events and avoids a visible clear. */
	XCopyArea(dpy, p->base, p->bg, p->gc, 0, 0, p->width, p->height, 0, 0);

	if (p->current >= 0 && p->current < p->count) {
		ExposeCell *cell = &p->cells[p->current];
		int x = cell->tx - EXPOSE_BORDER, y = cell->ty - EXPOSE_BORDER;
		int w = cell->tw + 2 * EXPOSE_BORDER, h = cell->th + 2 * EXPOSE_BORDER;

		/* dark outline one pixel outside and inside the accent bars, so the
		 * highlight reads against both light and dark window contents */
		XSetForeground(dpy, p->gc, WMColorPixel(scr->black));
		drawRing(p, x - 1, y - 1, w + 2, h + 2, EXPOSE_BORDER + 2);
		XSetForeground(dpy, p->gc, WMColorPixel(p->accent));
		drawRing(p, x, y, w, h, EXPOSE_BORDER);
	}

	for (i = 0; i < p->count; i++) {
		ExposeCell *cell = &p->cells[i];
		char *s = ShrinkString(p->font, cell->title, cell->w - 2 * EXPOSE_LABEL_PAD);
		int len = strlen(s);
		int tw = WMWidthOfString(p->font, s, len);
		int x = cell->x + (cell->w - tw) / 2;
		int y = cell->y + cell->h - p->label_h + EXPOSE_LABEL_PAD;

		WMDrawString(scr->wmscreen, p->bg, i == p->current ? scr->white : scr->gray, p->font, x, y, s, len);
		wfree(s);
	}

	XSetWindowBackgroundPixmap(dpy, p->win, p->bg);
	XClearWindow(dpy, p->win);
	XFlush(dpy);
}

/* ---------------------------------------------------------------------- */
/* panel lifetime                                                          */

static ExposePanel *createPanel(WScreen *scr)
{
	ExposePanel *p;
	ExposeCell *cells;
	WMRect rect;
	XSetWindowAttributes attr;
	XGCValues gcv;
	int count, i;

	count = collectWindows(scr, &cells);
	if (count == 0)
		return NULL;

	p = wmalloc(sizeof(ExposePanel));
	p->scr = scr;
	p->cells = cells;
	p->count = count;
	p->current = 0;
	p->font = scr->info_text_font;
	p->label_h = WMFontHeight(p->font) + 2 * EXPOSE_LABEL_PAD;
	p->accent = WMCreateNamedColor(scr->wmscreen, "#ff9a1f", True);
	if (!p->accent)
		p->accent = WMRetainColor(scr->white);

	rect = wGetRectForHead(scr, wGetHeadForPointerLocation(scr));
	p->x = rect.pos.x;
	p->y = rect.pos.y;
	p->width = rect.size.width;
	p->height = rect.size.height;

	layoutCells(p);

	for (i = 0; i < count; i++)
		makeThumb(scr, &p->cells[i],
			  p->cell_w - 2 * EXPOSE_BORDER, p->cell_h - p->label_h - 2 * EXPOSE_BORDER);

	renderBackground(p);

	attr.override_redirect = True;
	attr.save_under = True;
	attr.background_pixmap = p->base;
	attr.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask
	    | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
	p->win = XCreateWindow(dpy, scr->root_win, p->x, p->y, p->width, p->height, 0,
			       CopyFromParent, InputOutput, CopyFromParent,
			       CWOverrideRedirect | CWSaveUnder | CWBackPixmap | CWEventMask, &attr);

	gcv.foreground = WMColorPixel(scr->white);
	p->gc = XCreateGC(dpy, scr->root_win, GCForeground, &gcv);

	return p;
}

static void destroyPanel(ExposePanel *p)
{
	WScreen *scr = p->scr;
	XEvent ev;
	int i;

	/* keep the unmap from handing focus to whatever is under the pointer,
	 * the same trick wSwitchPanelDestroy() uses */
	ev.xclient.type = ClientMessage;
	ev.xclient.message_type = w_global.atom.wm.ignore_focus_events;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = True;
	XSendEvent(dpy, scr->info_window, True, EnterWindowMask, &ev);

	XUnmapWindow(dpy, p->win);

	ev.xclient.data.l[0] = False;
	XSendEvent(dpy, scr->info_window, True, EnterWindowMask, &ev);

	/* WMDrawString() re-points one shared XftDraw at whatever it last drew
	 * on, which is our background pixmap. Freeing it would kill that
	 * Picture, and the next text draw anywhere would then fail with
	 * RenderBadPicture when Xft frees it. Move the XftDraw off the pixmap
	 * while it still exists. */
	if (scr->wmscreen->xftdraw)
		XftDrawChange(scr->wmscreen->xftdraw, scr->root_win);

	XFreeGC(dpy, p->gc);
	XDestroyWindow(dpy, p->win);
	XFreePixmap(dpy, p->bg);
	XFreePixmap(dpy, p->base);
	WMReleaseColor(p->accent);

	for (i = 0; i < p->count; i++) {
		if (p->cells[i].thumb)
			RReleaseImage(p->cells[i].thumb);
		wfree(p->cells[i].title);
	}
	wfree(p->cells);
	wfree(p);
}

/* ---------------------------------------------------------------------- */
/* interaction                                                             */

static int cellAt(ExposePanel *p, int x, int y)
{
	int i;

	for (i = 0; i < p->count; i++) {
		ExposeCell *c = &p->cells[i];

		if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h)
			return i;
	}
	return -1;
}

static void moveSelection(ExposePanel *p, int dcol, int drow)
{
	int col = p->current % p->cols;
	int row = p->current / p->cols;
	int idx;

	col += dcol;
	row += drow;
	if (col < 0)
		col = 0;
	if (col >= p->cols)
		col = p->cols - 1;
	if (row < 0)
		row = 0;
	if (row >= p->rows)
		row = p->rows - 1;

	idx = row * p->cols + col;
	if (idx >= p->count)
		idx = p->count - 1;
	if (idx != p->current) {
		p->current = idx;
		redraw(p);
	}
}

static void stepSelection(ExposePanel *p, int delta)
{
	p->current = (p->current + delta + p->count) % p->count;
	redraw(p);
}

static void handleKey(ExposePanel *p, XKeyEvent *ev)
{
	KeySym ks = XLookupKeysym(ev, 0);
	unsigned int modifiers = ev->state & w_global.shortcut.modifiers_mask;
	Bool shift = (ev->state & ShiftMask) != 0;

	/* the key that opened us closes us */
	if (wKeyBindings[WKBD_EXPOSE].keycode == ev->keycode
	    && wKeyBindings[WKBD_EXPOSE].modifier == modifiers) {
		p->done = True;
		return;
	}

	switch (ks) {
	case XK_Escape:
		p->done = True;
		break;
	case XK_Return:
	case XK_KP_Enter:
	case XK_space:
		p->activate = True;
		p->done = True;
		break;
	case XK_Left:
	case XK_KP_Left:
		moveSelection(p, -1, 0);
		break;
	case XK_Right:
	case XK_KP_Right:
		moveSelection(p, 1, 0);
		break;
	case XK_Up:
	case XK_KP_Up:
		moveSelection(p, 0, -1);
		break;
	case XK_Down:
	case XK_KP_Down:
		moveSelection(p, 0, 1);
		break;
	case XK_Tab:
		stepSelection(p, shift ? -1 : 1);
		break;
	case XK_ISO_Left_Tab:
		stepSelection(p, -1);
		break;
	case XK_Home:
		p->current = 0;
		redraw(p);
		break;
	case XK_End:
		p->current = p->count - 1;
		redraw(p);
		break;
	default:
		/* modifiers and anything else: ignore */
		break;
	}
}

static void runLoop(ExposePanel *p)
{
	XEvent ev;

	while (!p->done) {
		WMMaskEvent(dpy, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask
			    | PointerMotionMask | ExposureMask, &ev);

		switch (ev.type) {
		case KeyPress:
			handleKey(p, &ev.xkey);
			break;

		case KeyRelease:
		case ButtonPress:
			break;

		case MotionNotify:
			{
				int i = cellAt(p, ev.xmotion.x_root - p->x, ev.xmotion.y_root - p->y);

				if (i >= 0 && i != p->current) {
					p->current = i;
					redraw(p);
				}
			}
			break;

		case ButtonRelease:
			if (ev.xbutton.button == Button1) {
				int i = cellAt(p, ev.xbutton.x_root - p->x, ev.xbutton.y_root - p->y);

				if (i >= 0) {
					p->current = i;
					p->activate = True;
				}
				p->done = True;
			} else if (ev.xbutton.button == Button3) {
				p->done = True;
			}
			break;

		case Expose:
			/* our window is fully covered by its background pixmap,
			 * so the server repaints it; nothing to do for us */
			if (ev.xexpose.window != p->win)
				WMHandleEvent(&ev);
			break;

		default:
			WMHandleEvent(&ev);
			break;
		}
	}
}

void wExposeRequest(WScreen *scr)
{
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = scr->root_win;
	ev.xclient.message_type = w_global.atom.wmaker.command;
	ev.xclient.format = 8;
	strncpy(ev.xclient.data.b, "Expose", sizeof(ev.xclient.data.b));
	XSendEvent(dpy, scr->root_win, False, SubstructureRedirectMask, &ev);
	XFlush(dpy);
}

void wExposeStart(WScreen *scr)
{
	ExposePanel *p;
	Window client = None;
	Bool activate;

	/* second trigger while open: close */
	if (active_panel) {
		active_panel->done = True;
		return;
	}

	p = createPanel(scr);
	if (!p)
		return;

	active_panel = p;
	scr->flags.doing_alt_tab = 1;	/* suppress EnterNotify focus changes */

	XMapRaised(dpy, p->win);
	XGrabKeyboard(dpy, p->win, False, GrabModeAsync, GrabModeAsync, CurrentTime);
	XGrabPointer(dpy, p->win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	redraw(p);

	runLoop(p);

	activate = p->activate;
	if (activate && p->current >= 0 && p->current < p->count)
		client = p->cells[p->current].client;

	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	destroyPanel(p);
	active_panel = NULL;
	scr->flags.doing_alt_tab = 0;

	if (client != None) {
		/* the window may have gone away while the overview was open */
		WWindow *wwin = wWindowFor(client);

		if (wwin)
			wMakeWindowVisible(wwin);
	}
}
