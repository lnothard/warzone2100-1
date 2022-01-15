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
 * @file missiondef.h
 * Definitions for missions
 */

#ifndef __INCLUDED_MISSIONDEF_H__
#define __INCLUDED_MISSIONDEF_H__

#include "gateway.h"
#include "map.h"
#include "power.h"

/* Mission types */

// used to set the reinforcement time on hold whilst the transporter
// is unable to land hopefully they'll never need to set it this high
// for other reasons!
static constexpr auto SCR_LZ_COMPROMISED_TIME = 999990;

// this is used to compare the value passed in from the scripts with
// which is multiplied by 100
static constexpr auto LZ_COMPROMISED_TIME = 99999000;

// same value in seconds, as passed from JavaScript
static constexpr auto JS_LZ_COMPROMISED_TIME = 99999;

struct LANDING_ZONE
{
	uint8_t x1;
	uint8_t y1;
	uint8_t x2;
	uint8_t y2;
};

//storage structure for values that need to be kept between missions
struct MISSION
{
	LEVEL_TYPE type; //defines which start and end functions to use - see levels_type in levels.h
	std::unique_ptr<Tile[]> psMapTiles; //the original mapTiles
	int mapWidth; //the original mapWidth
	int mapHeight; //the original mapHeight
	std::unique_ptr<uint8_t[]> psBlockMap[AUX_MAX];
	std::unique_ptr<uint8_t[]> psAuxMap[MAX_PLAYERS + AUX_MAX];
	GATEWAY_LIST psGateways; //the gateway list
	int scrollMinX; //scroll coords for original map
	int scrollMinY;
	int scrollMaxX;
	int scrollMaxY;
	std::array< std::vector<Structure*>, MAX_PLAYERS> apsStructLists;
  Structure* apsExtractorLists[MAX_PLAYERS];
	std::array< std::vector<Droid>, MAX_PLAYERS> apsDroidLists;
	std::array< std::vector<Feature*>, MAX_PLAYERS> apsFeatureLists;
	SimpleObject* apsSensorList[1];
	Feature* apsOilList[1];
	FlagPosition* apsFlagPosLists[MAX_PLAYERS];
	int asCurrentPower[MAX_PLAYERS];

	unsigned startTime; //time the mission started
	int time; //how long the mission can last
	// < 0 = no limit
	int ETA; //time taken for reinforcements to arrive
	// < 0 = none allowed
	unsigned cheatTime; //time the cheating started (mission time-wise!)

	uint16_t homeLZ_X; //selectedPlayer's LZ x and y
	uint16_t homeLZ_Y;
	unsigned playerX; //original view position
	unsigned playerY;

	/* transporter entry/exit tiles */
	uint16_t iTranspEntryTileX[MAX_PLAYERS];
	uint16_t iTranspEntryTileY[MAX_PLAYERS];
	uint16_t iTranspExitTileX[MAX_PLAYERS];
	uint16_t iTranspExitTileY[MAX_PLAYERS];
};

#endif // __INCLUDED_MISSIONDEF_H__
