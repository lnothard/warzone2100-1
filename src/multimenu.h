/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/**
 * @file multimenu.h
 * Definition for in-game multiplayer interface
 */

#ifndef __INCLUDED_SRC_MULTIMENU__
#define __INCLUDED_SRC_MULTIMENU__

static constexpr auto MULTIMENU				= 10600;
static constexpr auto MULTIMENU_FORM			= MULTIMENU;

// requester
void addMultiRequest(const char* searchDir, const char* fileExtension, unsigned id, UBYTE numPlayers,
                     std::string const& searchString = std::string());
void closeMultiRequester();

extern bool multiRequestUp;
extern std::shared_ptr<W_SCREEN> psRScreen; // requester stuff.

bool runMultiRequester(unsigned id, unsigned* mode, WzString* chosen,
                       LEVEL_DATASET** chosenValue, bool* isHoverPreview);

void displayRequestOption(WIDGET* psWidget, unsigned xOffset, unsigned yOffset);

// multimenu
void intProcessMultiMenu(unsigned id);
bool intRunMultiMenu();
bool intCloseMultiMenu();
void intCloseMultiMenuNoAnim();
bool intAddMultiMenu();

void multiMenuScreenSizeDidChange(unsigned oldWidth, unsigned oldHeight,
                                  unsigned newWidth, unsigned newHeight);

extern bool MultiMenuUp;

extern unsigned current_numplayers;

#endif // __INCLUDED_SRC_MULTIMENU__
