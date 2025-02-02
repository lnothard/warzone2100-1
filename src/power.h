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
/** @file
 *  Definitions for the Power Functionality.
 */

#ifndef __INCLUDED_SRC_POWER_H__
#define __INCLUDED_SRC_POWER_H__

#include <cstdint>
#include <string>

class Structure;


struct PowerRequest
{
  PowerRequest(unsigned id, int64_t amount);

  int64_t amount; ///< Amount of power being requested.
  unsigned id; ///< Structure which is requesting power.
};

struct PlayerPower
{
  // All fields are 32.32 fixed point.
  int64_t currentPower; ///< The current amount of power available to the player.
  std::vector<PowerRequest> powerQueue; ///< Requested power.
  int powerModifier; ///< Percentage modifier on power from each derrick.
  int64_t maxStorage; ///< Maximum storage of power, in total.
  int64_t extractedPower; ///< Total amount of extracted power in this game.
  int64_t wastedPower; ///< Total amount of wasted power in this game.
  int64_t powerGeneratedLastUpdate;
  ///< The power generated the last time updatePlayerPower was called for this player
};

static std::array<PlayerPower, MAX_PLAYERS> asPower;

/** Free power on collection of oildrum. */
static constexpr auto OILDRUM_POWER	= 100;

/** Allocate the space for the playerPower. */
bool allocPlayerPower();

/** Clear the playerPower. */
void clearPlayerPower();

/// Removes any pending power request from this Structure.
void delPowerRequest(Structure* psStruct);

/// Checks how much power must be accumulated, before the power request from this Structure can be satisfied.
/// Returns -1 if there is no power request or if there is enough power already.
int64_t checkPowerRequest(Structure* psStruct);

static void updateCurrentPower(Structure* psStruct, unsigned player, int ticks);
bool requestPower(Structure* psStruct, int64_t amount);

void addPower(unsigned player, int64_t quantity);

void usePower(unsigned player, uint32_t quantity);

/** Update current power based on what was extracted during the last cycle and what Power Generators exist.
  * If ticks is set, this is the number of game ticks to process for at once. */
void updatePlayerPower(unsigned player, int ticks = 1);

/** Used in multiplayer to force power levels. */
void setPower(unsigned player, int32_t power);

void setPowerModifier(unsigned player, int modifier);
void setPowerMaxStorage(unsigned player, int max);

/** Get the amount of power current held by the given player. */
int64_t getPower(unsigned player);
int64_t getPowerMinusQueued(unsigned player);
int64_t getQueuedPower(unsigned player);

/// Get amount of power extracted during the whole game
int64_t getExtractedPower(unsigned player);

/// Get amount of power wasted during the whole game
int64_t getWastedPower(unsigned player);

/// Get the approximate power generated per second for the specified player (for display purposes - not to be used for calculations)
std::string getApproxPowerGeneratedPerSecForDisplay(unsigned player);

/** Resets the power levels for all players when power is turned back on. */
void powerCalc(bool on);

/** Flag used to check for power calculations to be done or not. */
extern bool powerCalculated;

#endif // __INCLUDED_SRC_POWER_H__
