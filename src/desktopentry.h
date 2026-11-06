/* desktopentry.h - minimal freedesktop .desktop file reader
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

#ifndef WMDESKTOPENTRY_H
#define WMDESKTOPENTRY_H

#include <WINGs/WUtil.h>

/*
 * Just enough of the Desktop Entry specification for launching things:
 * the [Desktop Entry] group of Type=Application files.
 */
typedef struct WDesktopEntry {
	char *path;		/* file the entry was read from */
	char *id;		/* file name without the .desktop suffix */
	char *name;		/* Name=, unlocalised */
	char *exec;		/* Exec= with the %-field codes removed */
	char *try_exec;		/* TryExec= or NULL */
	char *only_show_in;	/* raw ';'-separated list or NULL */
	char *not_show_in;	/* raw ';'-separated list or NULL */
	unsigned int hidden:1;	/* Hidden=true or NoDisplay=true */
	unsigned int terminal:1;	/* Terminal=true */
	unsigned int autostart_disabled:1;	/* X-GNOME-Autostart-enabled=false */
} WDesktopEntry;

/*
 * Read every *.desktop application entry in the given directories.  An
 * entry whose id was already seen in an earlier directory is skipped, so
 * list user directories before system ones.  The result is sorted by name
 * and owns its entries; release it with wDesktopEntriesFree().
 */
WMArray *wDesktopEntriesLoad(const char *const *dirs, int ndirs);
void wDesktopEntriesFree(WMArray *entries);

/* a shell command to launch the entry (caller frees); honours Terminal= */
char *wDesktopEntryCommand(const WDesktopEntry *entry);

/* OnlyShowIn / NotShowIn test against a desktop name such as "WindowMaker" */
Bool wDesktopEntryShowsIn(const WDesktopEntry *entry, const char *desktop);

/* True when there is no TryExec= or the program it names is on the PATH */
Bool wDesktopEntryTryExecOk(const WDesktopEntry *entry);

#endif /* WMDESKTOPENTRY_H */
