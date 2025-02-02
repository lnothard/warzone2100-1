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
 * @file
 * A header file that groups together all the object header files
 */

#ifndef __INCLUDED_SRC_OBJECTS_H__
#define __INCLUDED_SRC_OBJECTS_H__

#include "basedef.h"

/* Initialise the object system */
bool objInitialise();

/* Shutdown the object system */
bool objShutdown();

/// Goes through the list passed in reversing the order so the first entry becomes the last and the last entry becomes the first!
void reverseObjectList(BaseObject ** ppsList);

template <typename OBJECT>
void reverseObjectList(OBJECT** ppsList)
{
  BaseObject * baseList = *ppsList;
	reverseObjectList(&baseList);
	*ppsList = static_cast<OBJECT*>(baseList);
}

/** Output an informative string about this object. For debugging. */
const char* objInfo(const BaseObject * psObj);

#endif // __INCLUDED_SRC_OBJECTS_H__
