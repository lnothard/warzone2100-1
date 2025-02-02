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
 * @file loop.h
 * Interface to the main game loop routine
 */

#ifndef __INCLUDED_SRC_LOOP_H__
#define __INCLUDED_SRC_LOOP_H__

class Droid;
enum class LEVEL_TYPE;

enum class GAME_CODE
{
	CONTINUE,
	RESTARTGAME,
	QUITGAME,
	PLAYVIDEO,
	NEWLEVEL,
	FASTEXIT,
	LOADGAME
};

// the states the loop goes through before starting a new level
enum LOOP_MISSION_STATE
{
	LMS_NORMAL,
	// normal state of the loop
	LMS_SETUPMISSION,
	// make the call to set up mission
	LMS_SAVECONTINUE,
	// the save/continue box is up between missions
	LMS_NEWLEVEL,
	// start a new level
	LMS_LOADGAME,
	// load a savegame
	LMS_CLEAROBJECTS,
	// make the call to destroy objects
};

extern LOOP_MISSION_STATE loopMissionState;

// this is set by scrStartMission to say what type of new level is to be started
extern LEVEL_TYPE nextMissionType;

extern std::size_t loopPieCount;
extern std::size_t loopPolyCount;

GAME_CODE gameLoop();
void videoLoop();
void loop_SetVideoPlaybackMode();
void loop_ClearVideoPlaybackMode();
bool loop_GetVideoStatus();
int loop_GetVideoMode();
bool gamePaused();
void setGamePauseStatus(bool val);

bool gameUpdatePaused();
bool audioPaused();
bool scriptPaused();
bool scrollPaused();
bool consolePaused();

constexpr std::size_t WZ_DEFAULT_MAX_FASTFORWARD_TICKS = 1;
std::size_t getMaxFastForwardTicks();
void setMaxFastForwardTicks(optional<std::size_t> value = nullopt,
                            bool fixedToNormalTickRate = true);

void setGameUpdatePause(bool state);
void setAudioPause(bool state);
void setScriptPause(bool state);
void setScrollPause(bool state);
void setConsolePause(bool state);
//set all the pause states to the state value
void setAllPauseStates(bool state);

// Number of units in the current list.
unsigned getNumDroids(unsigned player);
// Number of units on transporters.
unsigned getNumTransporterDroids(unsigned player);
// Number of units in the mission list.
unsigned getNumMissionDroids(unsigned player);
unsigned getNumCommandDroids(unsigned player);
unsigned getNumConstructorDroids(unsigned player);
// increase the droid counts - used by update factory to keep the counts in sync
void adjustDroidCount(Droid* droid, int delta);
// Increase counts of droids in a transporter
void droidCountsInTransporter(Droid* droid, unsigned player);

void countUpdate(bool synch = false);

#endif // __INCLUDED_SRC_LOOP_H__
