/* quickpanel.h - type-to-find window switcher and application launcher
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

#ifndef WMQUICKPANEL_H
#define WMQUICKPANEL_H

#include "screen.h"

/*
 * Open a panel with a text field and a list.  Typing filters the open
 * windows (all workspaces) and the installed applications; Return or a
 * double click activates the window or launches the application, Escape
 * closes.  Bound to QuickSwitchKey and to the _WINDOWMAKER_COMMAND
 * "QuickSwitch" client message.
 */
void wQuickPanelShow(WScreen *scr);

#endif /* WMQUICKPANEL_H */
