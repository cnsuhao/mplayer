/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MPLAYER_GUI_OPTS_H
#define MPLAYER_GUI_OPTS_H

#include <gtk/gtk.h>

extern GtkWidget * AudioConfig;
extern GtkWidget * Preferences;
extern GtkWidget * prEFontName;

extern GtkWidget * create_Preferences( void );
extern GtkWidget * create_AudioConfig( void );

extern void ShowPreferences( void );

#endif /* MPLAYER_GUI_OPTS_H */
