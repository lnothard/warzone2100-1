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
 * Definitions for position objects
 */

#ifndef __INCLUDED_POSITIONDEF_H__
#define __INCLUDED_POSITIONDEF_H__

enum class POSITION_TYPE
{
	POS_DELIVERY,
	//Delivery Points NOT wayPoints
	POS_PROXDATA,
	//proximity messages that are data generated
	POS_PROXOBJ,
	//proximity messages that are in game generated
	POS_TEMPDELIVERY //SAVE ONLY delivery point for factory currently assigned to commander
};

struct ObjectPosition
{
	POSITION_TYPE type; ///< the type of position obj - FlagPos or ProxDisp
	unsigned frameNumber; ///< when the Position was last drawn
	unsigned screenX; ///< screen coords and radius of Position imd
	unsigned screenY;
	unsigned screenR;
	unsigned player; ///< which player the Position belongs to
	bool selected; ///< flag to indicate whether the Position is to be highlighted
};

#endif // __INCLUDED_POSITIONDEF_H__
