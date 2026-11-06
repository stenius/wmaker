/* expose.h - exposé-style overview of the windows on the current workspace
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

#ifndef WMEXPOSE_H
#define WMEXPOSE_H

#include "screen.h"

/*
 * Show a grid of thumbnails of every window on the current workspace of
 * the head under the pointer (minimized, hidden and shaded ones included)
 * and let the user pick one with the mouse or the keyboard.  The picked
 * window is made visible and focused.  Calling this while the overview is
 * already open closes it, so a key, a hot corner or a _WINDOWMAKER_COMMAND
 * "Expose" client message all act as toggles.
 */
void wExposeStart(WScreen *scr);

#endif /* WMEXPOSE_H */
