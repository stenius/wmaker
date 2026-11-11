/* quickactions.c - application actions in the appicon and dock menus
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

#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "icon.h"
#include "appicon.h"
#include "framewin.h"
#include "menu.h"
#include "defaults.h"
#include "desktopentry.h"
#include "main.h"
#include "quickactions.h"

#define MAX_ACTIONS 12

typedef struct {
	WScreen *scr;
	char *command;
} QuickAction;

static void runAction(WMenu *menu, WMenuEntry *entry)
{
	QuickAction *qa = entry->clientdata;

	(void) menu;
	if (qa && qa->command)
		ExecuteShellCommand(qa->scr, qa->command);
}

static int addAction(WMenu *menu, int index, WScreen *scr, const char *label, const char *command)
{
	QuickAction *qa;
	WMenuEntry *entry;

	if (!label || !label[0] || !command || !command[0])
		return 0;
	qa = wmalloc(sizeof(QuickAction));
	qa->scr = scr;
	qa->command = wstrdup(command);
	entry = wMenuInsertCallback(menu, index, label, runAction, qa);
	if (!entry) {
		wfree(qa->command);
		wfree(qa);
		return 0;
	}
	return 1;
}

int wQuickActionsInsert(WMenu *menu, WAppIcon *aicon)
{
	WScreen *scr = menu->frame->screen_ptr;
	WWindow *owner = aicon->icon ? aicon->icon->owner : NULL;
	const char *desktop_id = owner ? owner->desktop_id : NULL;
	const char *instance = aicon->wm_instance, *class = aicon->wm_class;
	const WDesktopEntry *de;
	WMPropList *user;
	int n = 0;

	if (!wPreferences.appicon_actions)
		return 0;
	if (owner) {
		if (!instance)
			instance = owner->wm_instance;
		if (!class)
			class = owner->wm_class;
	}

	/* the user's own actions first */
	user = wDefaultGetQuickActions(instance, class);
	if (user) {
		int i, count = WMGetPropListItemCount(user);

		for (i = 0; i < count && n < MAX_ACTIONS; i++) {
			WMPropList *item = WMGetFromPLArray(user, i), *label, *command;

			if (!item || !WMIsPLArray(item) || WMGetPropListItemCount(item) != 2)
				continue;
			label = WMGetFromPLArray(item, 0);
			command = WMGetFromPLArray(item, 1);
			if (!WMIsPLString(label) || !WMIsPLString(command))
				continue;
			n += addAction(menu, n, scr, WMGetFromPLString(label), WMGetFromPLString(command));
		}
	}

	/* then what the application's desktop file declares */
	de = wDesktopEntryForApp(desktop_id, instance, class, aicon->command);
	if (de && de->actions) {
		WDesktopAction *a;
		int i;

		WM_ITERATE_ARRAY(de->actions, a, i) {
			if (n >= MAX_ACTIONS)
				break;
			n += addAction(menu, n, scr, a->name, a->exec);
		}
	}
	return n;
}

void wQuickActionsRemove(WMenu *menu, int n)
{
	while (n-- > 0 && menu->entry_no > 0) {
		QuickAction *qa = menu->entries[0]->clientdata;

		if (qa) {
			wfree(qa->command);
			wfree(qa);
		}
		wMenuRemoveItem(menu, 0);
	}
}
