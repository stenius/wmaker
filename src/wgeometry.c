/* wgeometry.c - remember window geometry per application
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "framewin.h"
#include "defaults.h"
#include "misc.h"
#include "wgeometry.h"

/*
 * ~/GNUstep/Defaults/WMWindowGeometry looks like
 *
 *   {
 *     "konsole.konsole" = { Position = "120,80"; Size = "900,600"; };
 *   }
 */

static Bool eligible(WWindow *wwin)
{
	if (!wPreferences.remember_geometry)
		return False;
	if (!wwin->wm_class || !wwin->frame)
		return False;
	if (wwin->transient_for != None && wwin->transient_for != wwin->screen_ptr->root_win)
		return False;
	if (wwin->flags.internal_window || wwin->flags.is_dockapp)
		return False;
	return True;
}

static WMPropList *keyFor(WWindow *wwin)
{
	WMPropList *key;
	char *buffer;

	if (wwin->wm_instance && wwin->wm_instance[0]) {
		buffer = StrConcatDot(wwin->wm_instance, wwin->wm_class);
		key = WMCreatePLString(buffer);
		wfree(buffer);
	} else {
		key = WMCreatePLString(wwin->wm_class);
	}
	return key;
}

void wGeometryRemember(WWindow *wwin)
{
	WDDomain *db = w_global.domain.window_geometry;
	WMPropList *dict, *key, *value, *pos, *size;
	char buf[64];
	int x, y;
	unsigned int w, h;

	if (!db || !eligible(wwin))
		return;
	if (wwin->flags.fullscreen)
		return;	/* the pre-fullscreen frame rect is not a client size; skip */

	if (wwin->flags.maximized && wwin->old_geometry.width > 0) {
		x = wwin->old_geometry.x;
		y = wwin->old_geometry.y;
		w = wwin->old_geometry.width;
		h = wwin->old_geometry.height;
	} else {
		x = wwin->frame_x;
		y = wwin->frame_y;
		w = wwin->client.width;
		h = wwin->client.height;
	}
	if (w == 0 || h == 0)
		return;

	dict = db->dictionary;
	if (!dict) {
		dict = WMCreatePLDictionary(NULL, NULL);
		if (!dict)
			return;
		db->dictionary = dict;
	}

	WMPLSetCaseSensitive(True);
	snprintf(buf, sizeof(buf), "%d,%d", x, y);
	pos = WMCreatePLString(buf);
	snprintf(buf, sizeof(buf), "%u,%u", w, h);
	size = WMCreatePLString(buf);
	value = WMCreatePLDictionary(WMCreatePLString("Position"), pos, WMCreatePLString("Size"), size, NULL);
	key = keyFor(wwin);
	WMPutInPLDictionary(dict, key, value);
	WMReleasePropList(key);
	WMReleasePropList(value);
	WMReleasePropList(pos);
	WMReleasePropList(size);

	if (!wPreferences.flags.noupdates)
		UpdateDomainFile(db);
}

/* another mapped window of the same application on this workspace? */
static Bool classOpenOnWorkspace(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	WWindow *other;

	for (other = scr->focused_window; other; other = other->prev) {
		if (other == wwin || !other->frame || !other->wm_class)
			continue;
		if (!other->flags.mapped || other->flags.internal_window)
			continue;
		if (other->frame->workspace != scr->current_workspace && !IS_OMNIPRESENT(other))
			continue;
		if (strcmp(other->wm_class, wwin->wm_class) != 0)
			continue;
		if ((other->wm_instance == NULL) != (wwin->wm_instance == NULL))
			continue;
		if (other->wm_instance && strcmp(other->wm_instance, wwin->wm_instance) != 0)
			continue;
		return True;
	}
	return False;
}

Bool wGeometryRecall(WWindow *wwin, int *x, int *y, unsigned int *width, unsigned int *height)
{
	WDDomain *db = w_global.domain.window_geometry;
	WMPropList *key, *value, *pos, *size;
	int px, py;
	unsigned int pw, ph;
	Bool ok = False;

	if (!db || !db->dictionary || !wPreferences.remember_geometry)
		return False;
	if (!wwin->wm_class || wwin->flags.internal_window || wwin->flags.is_dockapp)
		return False;
	if (wwin->transient_for != None && wwin->transient_for != wwin->screen_ptr->root_win)
		return False;
	if (classOpenOnWorkspace(wwin))
		return False;

	WMPLSetCaseSensitive(True);
	key = keyFor(wwin);
	value = WMGetFromPLDictionary(db->dictionary, key);
	WMReleasePropList(key);
	if (!value || !WMIsPLDictionary(value))
		return False;

	pos = WMGetFromPLDictionary(value, WMCreatePLString("Position"));
	size = WMGetFromPLDictionary(value, WMCreatePLString("Size"));
	if (pos && WMIsPLString(pos) && sscanf(WMGetFromPLString(pos), "%d,%d", &px, &py) == 2) {
		*x = px;
		*y = py;
		ok = True;
	}
	if (ok && size && WMIsPLString(size) && sscanf(WMGetFromPLString(size), "%u,%u", &pw, &ph) == 2
	    && pw > 0 && ph > 0 && !(wwin->normal_hints && (wwin->normal_hints->flags & USSize))) {
		*width = pw;
		*height = ph;
	}
	return ok;
}
