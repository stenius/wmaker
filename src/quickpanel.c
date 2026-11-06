/* quickpanel.c - type-to-find window switcher and application launcher
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

#include "wconfig.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "actions.h"
#include "main.h"
#include "workspace.h"
#include "xinerama.h"
#include "misc.h"
#include "desktopentry.h"
#include "quickpanel.h"

#define QUICK_MAX_ROWS   40
#define QUICK_DESKTOP    "WindowMaker"

typedef enum { QK_WINDOW, QK_APP } QuickKind;

typedef struct {
	QuickKind kind;
	char *label;		/* window title or application name */
	char *detail;		/* "workspace · class" or the command */
	char *folded;		/* lower-cased label + detail, for matching */
	Window client;		/* QK_WINDOW: client id, re-looked-up on use */
	WDesktopEntry *app;	/* QK_APP: owned by the entries array */
} QuickEntry;

typedef struct {
	WScreen *scr;
	WMWindow *win;
	WMTextField *text;
	WMList *list;
	WMFont *font;
	WMFont *small;
	WMArray *apps;		/* WDesktopEntry*, from wDesktopEntriesLoad */
	WMArray *all;		/* QuickEntry*, every candidate */
	WMArray *shown;		/* QuickEntry*, the filtered subset (not owned) */
	QuickEntry *result;
} QuickPanel;

static Bool panel_open = False;

/* ---------------------------------------------------------------------- */
/* candidates                                                              */

static void freeEntry(void *data)
{
	QuickEntry *e = data;

	wfree(e->label);
	wfree(e->detail);
	wfree(e->folded);
	wfree(e);
}

static char *fold(const char *a, const char *b)
{
	char *s = wstrconcat(a, " ");
	char *p;

	s = wstrappend(s, b);
	for (p = s; *p; p++)
		*p = tolower((unsigned char)*p);
	return s;
}

static QuickEntry *newEntry(QuickKind kind, const char *label, const char *detail)
{
	QuickEntry *e = wmalloc(sizeof(QuickEntry));

	e->kind = kind;
	e->label = wstrdup(label ? label : "");
	e->detail = wstrdup(detail ? detail : "");
	e->folded = fold(e->label, e->detail);
	return e;
}

static const char *workspaceName(WScreen *scr, int ws)
{
	if (ws >= 0 && ws < scr->workspace_count && scr->workspaces[ws]->name)
		return scr->workspaces[ws]->name;
	return "";
}

static const char *const *appDirs(int *count)
{
	static char *dirs[32];
	static int ndirs = -1;

	if (ndirs < 0) {
		const char *home = getenv("HOME");
		const char *data_home = getenv("XDG_DATA_HOME");
		const char *data_dirs = getenv("XDG_DATA_DIRS");
		char *list, *p, *save = NULL;

		ndirs = 0;
		if (data_home && data_home[0])
			dirs[ndirs++] = wstrconcat(data_home, "/applications");
		else if (home && home[0])
			dirs[ndirs++] = wstrconcat(home, "/.local/share/applications");
		list = wstrdup((data_dirs && data_dirs[0]) ? data_dirs : "/usr/local/share:/usr/share");
		for (p = strtok_r(list, ":", &save); p && ndirs < 31; p = strtok_r(NULL, ":", &save))
			if (p[0])
				dirs[ndirs++] = wstrconcat(p, "/applications");
		wfree(list);
	}
	*count = ndirs;
	return (const char *const *)dirs;
}

static void collect(QuickPanel *p)
{
	WScreen *scr = p->scr;
	WWindow *wwin;
	WDesktopEntry *de;
	int ndirs = 0, i;

	p->all = WMCreateArrayWithDestructor(64, freeEntry);

	/* windows, most recently focused first */
	for (wwin = scr->focused_window; wwin; wwin = wwin->prev) {
		QuickEntry *e;
		char *detail;

		if (!wwin->frame || wwin->flags.internal_window || wwin->flags.is_dockapp)
			continue;
		if (WFLAGP(wwin, skip_window_list))
			continue;
		detail = wstrconcat(workspaceName(scr, wwin->frame->workspace), " \xc2\xb7 ");
		detail = wstrappend(detail, wwin->wm_class ? wwin->wm_class : "");
		e = newEntry(QK_WINDOW, wwin->frame->title, detail);
		e->client = wwin->client_win;
		wfree(detail);
		WMAddToArray(p->all, e);
	}

	/* applications */
	{
		const char *const *dirs = appDirs(&ndirs);

		p->apps = wDesktopEntriesLoad(dirs, ndirs);
	}
	WM_ITERATE_ARRAY(p->apps, de, i) {
		QuickEntry *e;

		if (de->hidden || !wDesktopEntryShowsIn(de, QUICK_DESKTOP))
			continue;
		e = newEntry(QK_APP, de->name, de->exec);
		e->app = de;
		WMAddToArray(p->all, e);
	}
}

/* ---------------------------------------------------------------------- */
/* filtering                                                               */

/* every whitespace-separated token of the query occurs in the entry */
static Bool matches(const QuickEntry *e, const char *query)
{
	const char *q = query;

	while (*q) {
		const char *end;
		size_t len;
		char *tok, *lp;
		Bool found;

		while (*q && isspace((unsigned char)*q))
			q++;
		if (!*q)
			break;
		end = q;
		while (*end && !isspace((unsigned char)*end))
			end++;
		len = end - q;
		tok = wstrndup(q, len);
		for (lp = tok; *lp; lp++)
			*lp = tolower((unsigned char)*lp);
		found = strstr(e->folded, tok) != NULL;
		wfree(tok);
		if (!found)
			return False;
		q = end;
	}
	return True;
}

static Bool prefixMatch(const QuickEntry *e, const char *query)
{
	size_t n = strlen(query);

	while (n > 0 && isspace((unsigned char)query[n - 1]))
		n--;
	return n > 0 && strncasecmp(e->label, query, n) == 0;
}

static void refilter(QuickPanel *p)
{
	char *query = WMGetTextFieldText(p->text);
	QuickEntry *e;
	int pass, i, rows = 0;

	WMClearList(p->list);
	if (p->shown)
		WMFreeArray(p->shown);
	p->shown = WMCreateArray(QUICK_MAX_ROWS);

	/* windows before applications, label-prefix matches first */
	for (pass = 0; pass < 4 && rows < QUICK_MAX_ROWS; pass++) {
		QuickKind kind = (pass < 2) ? QK_WINDOW : QK_APP;
		Bool want_prefix = (pass % 2 == 0);

		WM_ITERATE_ARRAY(p->all, e, i) {
			if (e->kind != kind || !matches(e, query))
				continue;
			if (query[0] && prefixMatch(e, query) != want_prefix)
				continue;
			if (!query[0] && !want_prefix)
				continue;	/* empty query: one pass per kind is enough */
			WMAddToArray(p->shown, e);
			WMAddListItem(p->list, e->label);
			WMGetListItem(p->list, rows)->clientData = e;
			if (++rows >= QUICK_MAX_ROWS)
				break;
		}
	}
	wfree(query);

	if (rows > 0)
		WMSelectListItem(p->list, 0);
}

/* ---------------------------------------------------------------------- */
/* widgets                                                                 */

static void drawRow(WMList *lPtr, int index, Drawable d, char *text, int state, WMRect *rect)
{
	QuickPanel *p = WMGetHangedData(lPtr);
	WMScreen *wscr = WMWidgetScreen(lPtr);
	Display *disp = WMScreenDisplay(wscr);
	WMListItem *item = WMGetListItem(lPtr, index);
	QuickEntry *e = item ? item->clientData : NULL;
	WMColor *back = (state & WLDSSelected) ? WMWhiteColor(wscr) : WMGrayColor(wscr);
	WMColor *fg = WMBlackColor(wscr);
	WMColor *dim = WMDarkGrayColor(wscr);
	int x = rect->pos.x, y = rect->pos.y, w = rect->size.width, h = rect->size.height;

	(void)text;
	XFillRectangle(disp, d, WMColorGC(back), x, y, w, h);
	if (e) {
		int dw = WMWidthOfString(p->small, e->detail, strlen(e->detail));
		int ty = y + (h - WMFontHeight(p->font)) / 2;
		int avail = w - 12 - dw - 8;
		char *label = e->label;
		char *shrunk = NULL;

		if (avail > 20 && WMWidthOfString(p->font, label, strlen(label)) > avail) {
			shrunk = ShrinkString(p->font, label, avail);
			label = shrunk;
		}
		WMDrawString(wscr, d, fg, p->font, x + 6, ty, label, strlen(label));
		if (dw < w / 2)
			WMDrawString(wscr, d, dim, p->small, x + w - dw - 6,
				     y + (h - WMFontHeight(p->small)) / 2, e->detail, strlen(e->detail));
		if (shrunk)
			wfree(shrunk);
	}
	WMReleaseColor(back);
	WMReleaseColor(fg);
	WMReleaseColor(dim);
}

static void finish(QuickPanel *p, Bool activate)
{
	int row = WMGetListSelectedItemRow(p->list);

	p->result = NULL;
	if (activate && row >= 0 && row < WMGetArrayItemCount(p->shown))
		p->result = WMGetFromArray(p->shown, row);
	WMBreakModalLoop(p->scr->wmscreen);
}

static void moveSelection(QuickPanel *p, int delta)
{
	int rows = WMGetListNumberOfRows(p->list);
	int row = WMGetListSelectedItemRow(p->list);

	if (rows <= 0)
		return;
	row = (row < 0) ? 0 : row + delta;
	if (row < 0)
		row = 0;
	if (row >= rows)
		row = rows - 1;
	WMSelectListItem(p->list, row);
}

static void keyHandler(XEvent *event, void *data)
{
	QuickPanel *p = data;
	KeySym ks = XLookupKeysym(&event->xkey, 0);
	Bool ctrl = (event->xkey.state & ControlMask) != 0;

	if (event->type != KeyPress)
		return;

	switch (ks) {
	case XK_Escape:
		finish(p, False);
		break;
	case XK_Return:
	case XK_KP_Enter:
		finish(p, True);
		break;
	case XK_Down:
	case XK_KP_Down:
	case XK_Tab:
		moveSelection(p, 1);
		break;
	case XK_Up:
	case XK_KP_Up:
	case XK_ISO_Left_Tab:
		moveSelection(p, -1);
		break;
	case XK_Page_Down:
		moveSelection(p, 8);
		break;
	case XK_Page_Up:
		moveSelection(p, -8);
		break;
	default:
		if (ctrl && (ks == XK_n || ks == XK_N))
			moveSelection(p, 1);
		else if (ctrl && (ks == XK_p || ks == XK_P))
			moveSelection(p, -1);
		else if (ctrl && (ks == XK_g || ks == XK_G))
			finish(p, False);
		break;
	}
}

static void textChanged(void *observerData, WMNotification *notification)
{
	(void)notification;
	refilter((QuickPanel *) observerData);
}

static void listDoubleAction(WMWidget *self, void *data)
{
	(void)self;
	finish((QuickPanel *) data, True);
}

/* ---------------------------------------------------------------------- */

void wQuickPanelShow(WScreen *scr)
{
	QuickPanel *p;
	Window parent;
	WWindow *wwin;
	WMPoint center;
	int width, height, pad, field_h;
	QuickEntry *result;
	QuickKind kind = QK_WINDOW;
	Window client = None;
	char *cmd = NULL;
	int wmScaleWidth, wmScaleHeight;	/* for the WMScaleX/Y macros */

	if (panel_open)
		return;
	panel_open = True;

	WMGetScaleBaseFromSystemFont(scr->wmscreen, &wmScaleWidth, &wmScaleHeight);

	p = wmalloc(sizeof(QuickPanel));
	p->scr = scr;
	p->font = WMBoldSystemFontOfSize(scr->wmscreen, WMScaleY(12));
	p->small = WMSystemFontOfSize(scr->wmscreen, WMScaleY(10));
	collect(p);

	width = WMScaleX(640);
	height = WMScaleY(420);
	pad = WMScaleY(8);
	field_h = WMScaleY(26);

	p->win = WMCreateWindow(scr->wmscreen, "quickpanel");
	WMResizeWidget(p->win, width, height);
	WMSetWindowTitle(p->win, _("Switch to window or launch application"));

	p->text = WMCreateTextField(p->win);
	WMResizeWidget(p->text, width - 2 * pad, field_h);
	WMMoveWidget(p->text, pad, pad);
	WMSetTextFieldFont(p->text, p->font);
	WMCreateEventHandler(WMWidgetView(p->text), KeyPressMask, keyHandler, p);
	WMAddNotificationObserver(textChanged, p, WMTextDidChangeNotification, p->text);

	p->list = WMCreateList(p->win);
	WMResizeWidget(p->list, width - 2 * pad, height - field_h - 3 * pad);
	WMMoveWidget(p->list, pad, 2 * pad + field_h);
	WMHangData(p->list, p);
	WMSetListUserDrawProc(p->list, drawRow);
	WMSetListDoubleAction(p->list, listDoubleAction, p);
	WMSetListAllowEmptySelection(p->list, True);

	WMRealizeWidget(p->win);
	WMMapSubwidgets(p->win);
	refilter(p);

	parent = XCreateSimpleWindow(dpy, scr->root_win, 0, 0, width, height, 0, 0, 0);
	XSelectInput(dpy, parent, KeyPressMask | KeyReleaseMask);
	XReparentWindow(dpy, WMWidgetXID(p->win), parent, 0, 0);
	center = wGetPointToCenterRectInHead(scr, wGetHeadForPointerLocation(scr), width, height);
	wwin = wManageInternalWindow(scr, parent, None, NULL, center.x, center.y, width, height);
	wwin->client_leader = WMWidgetXID(p->win);

	WMMapWidget(p->win);
	wWindowMap(wwin);
	WMSetFocusToWidget(p->text);

	WMRunModalLoop(scr->wmscreen, WMWidgetView(p->win));

	/* copy what we need out of the panel before tearing it down */
	result = p->result;
	if (result) {
		kind = result->kind;
		if (kind == QK_WINDOW)
			client = result->client;
		else
			cmd = wDesktopEntryCommand(result->app);
	}

	WMRemoveNotificationObserver(p);
	wUnmanageWindow(wwin, False, False);
	WMDestroyWidget(p->win);
	XDestroyWindow(dpy, parent);
	if (p->shown)
		WMFreeArray(p->shown);
	WMFreeArray(p->all);
	wDesktopEntriesFree(p->apps);
	WMReleaseFont(p->font);
	WMReleaseFont(p->small);
	wfree(p);
	panel_open = False;

	if (client != None) {
		WWindow *target = wWindowFor(client);

		if (target)
			wMakeWindowVisible(target);
	} else if (cmd) {
		ExecuteShellCommand(scr, cmd);
		wfree(cmd);
	}
}
