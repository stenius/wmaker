/* autostart.h - XDG autostart
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

#ifndef WMAUTOSTART_H
#define WMAUTOSTART_H

#include "screen.h"

/* the desktop name used for OnlyShowIn / NotShowIn and XDG_CURRENT_DESKTOP */
#define WMAKER_DESKTOP_NAME "WindowMaker"

/*
 * Launch the applications registered for autostart in
 * $XDG_CONFIG_HOME/autostart and $XDG_CONFIG_DIRS/autostart, following
 * the Desktop Application Autostart specification: a user entry overrides
 * a system entry with the same file name, Hidden entries are skipped, as
 * are entries whose OnlyShowIn/NotShowIn exclude this desktop or whose
 * TryExec program is missing.
 */
void wAutostartRun(WScreen *scr);

#endif /* WMAUTOSTART_H */
