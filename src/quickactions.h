/* quickactions.h - application actions in the appicon and dock menus
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

#ifndef WMQUICKACTIONS_H
#define WMQUICKACTIONS_H

#include "menu.h"
#include "appicon.h"

/*
 * With AppIconActions = YES, the right-click menu of an application icon
 * (free or docked) starts with the application's secondary launch
 * commands: the user's QuickActions for that application in
 * WMWindowAttributes,
 *
 *   thunderbird = {
 *     QuickActions = ( ("Calendar", "thunderbird -calendar"),
 *                      ("Compose", "thunderbird -compose") );
 *   };
 *
 * followed by the Desktop Actions of the application's .desktop file
 * ("Open a New Window", "Compose New Message", ...).  The desktop file is
 * found through the window's _KDE_NET_WM_DESKTOP_FILE / _GTK_APPLICATION_ID
 * hint, its StartupWMClass, its id, or the docked command.
 */

/* insert the actions for the icon at the top of menu; returns how many */
int wQuickActionsInsert(WMenu *menu, WAppIcon *aicon);

/* remove n entries inserted by wQuickActionsInsert from the top of menu */
void wQuickActionsRemove(WMenu *menu, int n);

#endif /* WMQUICKACTIONS_H */
