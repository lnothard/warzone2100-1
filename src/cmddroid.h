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
 * @file cmddroid.h
 * Type definitions and interface for command droids
 */

#ifndef __INCLUDED_SRC_CMDDROID_H__
#define __INCLUDED_SRC_CMDDROID_H__

class Droid;

/// The maximum number of command droids allowed per player.
static constexpr auto MAX_CMDDROIDS =	5;

/** \brief Initialises the global instance for command droids.*/
bool cmdDroidInit();

/** \brief Shut down commander code module.
 */
void cmdDroidShutDown();

/** \brief Checks the validity of all target designators.*/
void cmdDroidUpdate();

/** \brief Adds a droid to a command group.*/
bool cmdDroidAddDroid(Droid* psCommander, Droid* psDroid);

/** \brief Returns the current target designator for a player.*/
Droid* cmdDroidGetDesignator(unsigned player);

/** \brief Sets the current target designator for a player.*/
void cmdDroidSetDesignator(Droid* psDroid);

/** \brief Clears the current target designator for a player.*/
void cmdDroidClearDesignator(unsigned player);

/** \brief Gets the index of the command droid.*/
int cmdDroidGetIndex(Droid* psCommander);

/** \brief Gets the maximum group size for a command droid.*/
unsigned int cmdDroidMaxGroup(const Droid* psCommander);

/** \brief Updates the experience of a command droid if psShooter is in a command group.*/
void cmdDroidUpdateExperience(Droid* psShooter, unsigned experienceInc);

/** \brief Gets the level of the droid group's commander, if any.*/
unsigned int cmdGetCommanderLevel(const Droid* psDroid);

/** \brief Returns if the droid has commander.*/
bool hasCommander(const Droid* psDroid);

#endif // __INCLUDED_SRC_CMDDROID_H__
