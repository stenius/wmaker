/* wgeometry.h - remember window geometry per application
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

#ifndef WMGEOMETRY_H
#define WMGEOMETRY_H

#include "window.h"

/*
 * With RememberWindowGeometry = YES, the position and size of a window
 * are stored under its "instance.class" in the WMWindowGeometry domain
 * when it is unmanaged, and the next window of that class that asks for
 * no position of its own opens there, unless a window of the same class
 * is already open on the workspace.
 */

/* record the window's normal geometry (called on unmanage) */
void wGeometryRemember(WWindow *wwin);

/* fetch a remembered geometry for a window about to be placed;
 * x, y are frame coordinates, width/height the client size.
 * Returns False when nothing applies. */
Bool wGeometryRecall(WWindow *wwin, int *x, int *y, unsigned int *width, unsigned int *height);

#endif /* WMGEOMETRY_H */
