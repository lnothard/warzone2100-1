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
 * @file cheat.cpp
 * Handles cheat codes
 *
 * Alex M 19th - Jan. 1999
 */

#include "input/debugmappings.h"
#include "lib/netplay/netplay.h"

#include "cheat.h"
#include "display.h"
#include "keybind.h"

void addDumpInfo(const char*);
void listTemplates();
void jsShowDebug();
bool triggerEventCheatMode(bool);


struct CHEAT_ENTRY
{
	char const* pName;
	void (*function)(); // pointer to void* function
};

bool Cheated = false;
static CHEAT_ENTRY cheatCodes[] =
{
	{"templates", listTemplates}, // print templates
	{"jsdebug", jsShowDebug}, // show scripting states
	{"teach us", kf_TeachSelected}, // give experience to selected units
	{"makemehero", kf_MakeMeHero}, // make selected units Heros
	{"untouchable", kf_Unselectable}, // make selected droids unselectable
	{"clone wars", [] { kf_CloneSelected(10); }}, // clone selected units
	{"clone wars!", [] { kf_CloneSelected(40); }}, // clone selected units
	{"clone wars!!", [] { kf_CloneSelected(135); }}, // clone selected units
	{"noassert", kf_NoAssert}, // turn off asserts
	{"count me", kf_ShowNumObjects}, // give a count of objects in the world
	{"give all", kf_AllAvailable}, // give all
	{"research all", kf_FinishAllResearch}, // research everything at once
	{"superpower", kf_MaxPower}, // get tons of power
	{"more power", kf_UpThePower}, // get tons of power
	{"deity", kf_ToggleGodMode}, //from above
	{"droidinfo", kf_DebugDroidInfo}, //show unit stats
	{"sensors", kf_ToggleSensorDisplay}, //show sensor ranges
	{"timedemo", kf_FrameRate}, //timedemo
	{"kill", kf_KillSelected}, //kill selected
	{"john kettley", kf_ToggleWeather}, //john kettley
	{"mouseflip", kf_ToggleMouseInvert}, //mouseflip
	{"biffer baker", kf_BifferBaker}, // almost invincible units
	{"easy", kf_SetEasyLevel}, //easy
	{"normal", kf_SetNormalLevel}, //normal
	{"hard", kf_SetHardLevel}, //hard
	{"double up", kf_DoubleUp}, // your units take half the damage
	{"whale fin", kf_TogglePower}, // turns on/off infinte power
	{"get off my land", kf_KillEnemy}, // kills all enemy units and structures
	{"build info", kf_BuildInfo}, // tells you when the game was built
	{"time toggle", kf_ToggleMissionTimer},
	{"work harder", kf_FinishResearch},
	{"tileinfo", kf_TileInfo}, // output debug info about a tile
	{"showfps", kf_ToggleFPS}, //displays your average FPS
	{"showunits", kf_ToggleUnitCount}, //displays unit count information
	{"showsamples", kf_ToggleSamples}, //displays the # of Sound samples in Queue & List
	{"showorders", kf_ToggleOrders}, //displays unit order/action state.
	{"pause", kf_TogglePauseMode}, // Pause the game.
	{"power info", kf_PowerInfo},
	{"reload me", kf_Reload}, // reload selected weapons immediately
	{"desync me", kf_ForceDesync},
	{"damage me", kf_DamageMe},
	{"autogame on", kf_AutoGame},
	{"autogame off", kf_AutoGame},
	{"shakey", kf_ToggleShakeStatus}, //shakey
};

bool _attemptCheatCode(const char* cheat_name)
{
	static const CHEAT_ENTRY* const EndCheat = &cheatCodes[ARRAY_SIZE(cheatCodes)];

	// there is no reason to make people enter "cheat mode" to enter following commands
	if (!strcasecmp("showfps", cheat_name)) {
		kf_ToggleFPS();
		return true;
	}

	if (!strcasecmp("showunits", cheat_name)) {
		kf_ToggleUnitCount();
		return true;
	}

	if (!strcasecmp("specstats", cheat_name)) {
		kf_ToggleSpecOverlays();
		return true;
	}

	auto const& dbgInputManager = gInputManager.debugManager();
	if (strcmp(cheat_name, "cheat on") == 0 || strcmp(cheat_name, "debug") == 0) {
		if (!dbgInputManager.debugMappingsAllowed()) {
			kf_ToggleDebugMappings();
		}
		return true;
	}

	if (strcmp(cheat_name, "cheat off") == 0 && dbgInputManager.debugMappingsAllowed()) {
		kf_ToggleDebugMappings();
		return true;
	}

	if (!dbgInputManager.debugMappingsAllowed()) {
		return false;
	}

	for (auto curCheat = cheatCodes; curCheat != EndCheat; ++curCheat)
	{
    if (strcasecmp(cheat_name, curCheat->pName) != 0)
      continue;

    char buf[256];
    /* We've got our man... */
    curCheat->function();// run it

    // Copy this info to be used by the crash handler for the dump file
    ssprintf(buf, "User has used cheat code: %s", curCheat->pName);
    addDumpInfo(buf);

    /* And get out of here */
    Cheated = true;
    return true;
  }
	return false;
}

bool attemptCheatCode(char const* cheat_name)
{
	return _attemptCheatCode(cheat_name);
}

void sendProcessDebugMappings(bool val)
{
	if (NETisReplay()) {
		return;
	}

	if (selectedPlayer >= MAX_PLAYERS) {
		return;
	}
	NETbeginEncode(NETgameQueue(selectedPlayer), GAME_DEBUG_MODE);
	NETbool(&val);
	NETend();
}

static std::string getWantedDebugMappingStatuses(const DebugInputManager& dbgInputManager, bool bStatus)
{
	char ret[MAX_PLAYERS + 1] = "\0";
	char* p = ret;
	for (auto n = 0; n < MAX_PLAYERS; ++n)
	{
		if (NetPlay.players[n].allocated && !NetPlay.players[n].isSpectator &&
        dbgInputManager.getPlayerWantsDebugMappings(n) == bStatus) {
			*p++ = '0' + NetPlay.players[n].position;
		}
	}
	std::sort(ret, p);
	*p++ = '\0';
	return ret;
}
