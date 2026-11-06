/* autostart.c - XDG autostart
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

#include "WindowMaker.h"
#include "screen.h"
#include "window.h"
#include "main.h"
#include "desktopentry.h"
#include "autostart.h"

#define MAX_AUTOSTART_DIRS 16

/* "$XDG_CONFIG_HOME/autostart" then each "$XDG_CONFIG_DIRS entry/autostart" */
static int autostartDirs(char **dirs, int max)
{
	const char *home = getenv("HOME");
	const char *config_home = getenv("XDG_CONFIG_HOME");
	const char *config_dirs = getenv("XDG_CONFIG_DIRS");
	char *list, *p, *save = NULL;
	int n = 0;

	if (config_home && config_home[0]) {
		dirs[n++] = wstrconcat(config_home, "/autostart");
	} else if (home && home[0]) {
		dirs[n++] = wstrconcat(home, "/.config/autostart");
	}

	list = wstrdup((config_dirs && config_dirs[0]) ? config_dirs : "/etc/xdg");
	for (p = strtok_r(list, ":", &save); p && n < max; p = strtok_r(NULL, ":", &save)) {
		if (p[0] == '\0')
			continue;
		dirs[n++] = wstrconcat(p, "/autostart");
	}
	wfree(list);
	return n;
}

void wAutostartRun(WScreen *scr)
{
	char *dirs[MAX_AUTOSTART_DIRS];
	WMArray *entries;
	WDesktopEntry *e;
	int ndirs, i;

	ndirs = autostartDirs(dirs, MAX_AUTOSTART_DIRS);
	entries = wDesktopEntriesLoad((const char *const *)dirs, ndirs);

	WM_ITERATE_ARRAY(entries, e, i) {
		char *cmd;

		if (e->hidden || e->autostart_disabled)
			continue;
		if (!wDesktopEntryShowsIn(e, WMAKER_DESKTOP_NAME))
			continue;
		if (!wDesktopEntryTryExecOk(e))
			continue;

		cmd = wDesktopEntryCommand(e);
		ExecuteShellCommand(scr, cmd);
		wfree(cmd);
	}

	wDesktopEntriesFree(entries);
	for (i = 0; i < ndirs; i++)
		wfree(dirs[i]);
}
