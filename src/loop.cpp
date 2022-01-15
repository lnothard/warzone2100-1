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
 * @file loop.cpp
 * The main game loop
 */

#include "lib/framework/wzapp.h"
#include "lib/ivis_opengl/piestate.h" //ivis render code
#include "lib/ivis_opengl/piemode.h"
#include "lib/sound/cdaudio.h"
#include "lib/sound/mixer.h"
#include "lib/netplay/netplay.h"

#include "loop.h"
#include "ingameop.h"
#include "display3d.h"
#include "multiplay.h" //ajl
#include "intelmap.h"
#include "loadsave.h"
#include "game.h"
#include "multijoin.h"
#include "intimage.h"
#include "seqdisp.h"
#include "mission.h"
#include "lighting.h"
#include "mapgrid.h"
#include "edit3d.h"
#include "cmddroid.h"
#include "keybind.h"
#include "wrappers.h"
#include "qtscript.h"
#include "version.h"
#include "notifications.h"
#include "scores.h"
#include "clparse.h"

#ifdef DEBUG
#include "objmem.h"
#endif

void proj_UpdateAll();
void visUpdateLevel();
void processVisibility();

/*
 * Global variables
 */
std::size_t loopPieCount;
std::size_t loopPolyCount;

/*
 * local variables
 */
static bool paused = false;
static bool video = false;
static unsigned short int skipCounter = 0;

//holds which pause is valid at any one time
struct PAUSE_STATE
{
	bool gameUpdatePause;
	bool audioPause;
	bool scriptPause;
	bool scrollPause;
	bool consolePause;
};

static PAUSE_STATE pauseState;
static std::size_t maxFastForwardTicks = WZ_DEFAULT_MAX_FASTFORWARD_TICKS;
static bool fastForwardTicksFixedToNormalTickRate = true;
// can be set to false to "catch-up" as quickly as possible (but this may result in more jerky behavior)

static unsigned numDroids[MAX_PLAYERS];
static unsigned numMissionDroids[MAX_PLAYERS];
static unsigned numTransporterDroids[MAX_PLAYERS];
static unsigned numCommandDroids[MAX_PLAYERS];
static unsigned numConstructorDroids[MAX_PLAYERS];

static int videoMode = 0;

LOOP_MISSION_STATE loopMissionState = LMS_NORMAL;

// this is set by scrStartMission to say what type of new level is to be started
LEVEL_TYPE nextMissionType = LEVEL_TYPE::LDS_NONE;

static GAME_CODE renderLoop()
{
	if (bMultiPlayer && !NetPlay.isHostAlive && NetPlay.bComms && !NetPlay.isHost) {
		intAddInGamePopup();
	}

	audio_Update();
	wzShowMouse(true);

	INT_RETVAL intRetVal = INT_NONE;
	if (!paused) {
		/* Always refresh the widgets' backing stores if needed, even if we don't process clicks below */
		intDoScreenRefresh();

		/* Run the in game interface and see if it grabbed any mouse clicks */
		if (!getRotActive() && getWidgetsStatus() &&
        dragBox3D.status != DRAG_DRAGGING &&
        wallDrag.status != DRAG_DRAGGING) {
			intRetVal = intRunWidgets();
			screen_FlipIfBackDropTransition();

			// Send droid orders, if any. (Should do between intRunWidgets() calls, to avoid droid orders getting mixed up, in the case of multiple orders given while the game freezes due to net lag.)
			sendQueuedDroidInfo();
		}

		//don't process the object lists if paused or about to quit to the front end
		if (!gameUpdatePaused() && intRetVal != INT_QUIT) {
			if (dragBox3D.status != DRAG_DRAGGING && wallDrag.status != DRAG_DRAGGING
				&& (intRetVal == INT_INTERCEPT || isMouseOverRadar())) {
				// Using software cursors (when on) for these menus due to a bug in SDL's SDL_ShowCursor()
				wzSetCursor(CURSOR_DEFAULT);
			}

#ifdef DEBUG
			// check all flag positions for duplicate delivery points
			checkFactoryFlags();
#endif

			//handles callbacks for positioning of DP's
			process3DBuilding();
			processDeliveryRepos();

			if (bMultiPlayer) {
				multiPlayerLoop();
			}

			for (auto i = 0; i < MAX_PLAYERS; i++)
			{
				for (auto& psCurr : apsDroidLists[i])
				{
					// Don't copy the next pointer - if droids somehow get destroyed in the graphics rendering loop, who cares if we crash.
					calcDroidIllumination(&psCurr);
				}
			}
		}

		if (!consolePaused()) {
			/* Process all the console messages */
			updateConsoleMessages();
		}
		if (!scrollPaused() &&
        dragBox3D.status != DRAG_DRAGGING &&
        intMode != INT_INGAMEOP) {
			displayRenderLoop();
		}
	}
	else // paused
	{
		// Using software cursors (when on) for these menus due to a bug in SDL's SDL_ShowCursor()
		wzSetCursor(CURSOR_DEFAULT);

		if (dragBox3D.status != DRAG_DRAGGING) {
			displayRenderLoop();
		}

		if (InGameOpUp || isInGamePopupUp) // ingame options menu up, run it!
		{
			auto const& triggers = widgRunScreen(psWScreen);
			unsigned widgval = triggers.empty() ? 0 : triggers.front().widget->id;
			// Just use first click here, since the next click could be on another menu.

			intProcessInGameOptions(widgval);
			if (widgval == INTINGAMEOP_QUIT || widgval == INTINGAMEOP_POPUP_QUIT) {
				if (gamePaused()) {
					kf_TogglePauseMode();
				}
				intRetVal = INT_QUIT;
			}
		}

		if (bLoadSaveUp && runLoadSave(true) && strlen(sRequestResult)) {
			debug(LOG_NEVER, "Returned %s", sRequestResult);
			if (bRequestLoad) {
				loopMissionState = LMS_LOADGAME;
				NET_InitPlayers(); // otherwise alliances were not cleared
				sstrcpy(saveGameName, sRequestResult);
			}
			else {
				char msgbuffer[256] = {'\0'};

				if (saveInMissionRes()) {
					if (saveGame(sRequestResult, GTYPE_SAVE_START)) {
						sstrcpy(msgbuffer, _("GAME SAVED: "));
						sstrcat(msgbuffer, sRequestResult);
						addConsoleMessage(msgbuffer, CONSOLE_TEXT_JUSTIFICATION::LEFT, NOTIFY_MESSAGE);
					}
					else {
						ASSERT(false, "Mission Results: saveGame Failed");
						sstrcpy(msgbuffer, _("Could not save game!"));
						addConsoleMessage(msgbuffer, CONSOLE_TEXT_JUSTIFICATION::LEFT,
                              NOTIFY_MESSAGE);
						deleteSaveGame_classic(sRequestResult);
					}
				}
				else if (bMultiPlayer || saveMidMission()) {
					if (saveGame(sRequestResult, GTYPE_SAVE_MIDMISSION)) //mid mission from [esc] menu
					{
						sstrcpy(msgbuffer, _("GAME SAVED: "));
						sstrcat(msgbuffer, sRequestResult);
						addConsoleMessage(msgbuffer,
                              CONSOLE_TEXT_JUSTIFICATION::LEFT,
                              NOTIFY_MESSAGE);
					}
					else {
						ASSERT(!"saveGame(sRequestResult, GTYPE_SAVE_MIDMISSION) failed",
						       "Mid Mission: saveGame Failed");
						sstrcpy(msgbuffer, _("Could not save game!"));
						addConsoleMessage(msgbuffer,
                              CONSOLE_TEXT_JUSTIFICATION::LEFT,
                              NOTIFY_MESSAGE);
						deleteSaveGame_classic(sRequestResult);
					}
				}
				else {
					ASSERT(false, "Attempt to save game with incorrect load/save mode");
				}
			}
		}
	}

	/* Check for quit */
	bool quitting = false;
	if (intRetVal == INT_QUIT) {
		if (!loop_GetVideoStatus()) {
			//quitting from the game to the front end
			//so get a new backdrop
			quitting = true;

			pie_LoadBackDrop(SCREEN_RANDOMBDROP);
		}
	}
	if (!loop_GetVideoStatus() && !quitting && !headlessGameMode()) {
		if (!gameUpdatePaused()) {
			if (dragBox3D.status != DRAG_DRAGGING
				&& wallDrag.status != DRAG_DRAGGING
				&& intRetVal != INT_INTERCEPT) {
				ProcessRadarInput();
			}
			processInput();

			//no key clicks or in Intelligence Screen
			if (!isMouseOverRadar() && !isDraggingInGameNotification() &&
          !isMouseClickDownOnScreenOverlayChild() &&
				intRetVal == INT_NONE && !InGameOpUp && !isInGamePopupUp) {
				processMouseClickInput();
			}
			bRender3DOnly = false;
			displayWorld();
		}
		wzPerfBegin(PERF_GUI, "User interface");
		/* Display the in game interface */
		pie_SetFogStatus(false);

		if (bMultiPlayer && bDisplayMultiJoiningStatus) {
			intDisplayMultiJoiningStatus(bDisplayMultiJoiningStatus);
			setWidgetsStatus(false);
		}

		if (getWidgetsStatus())	{
			intDisplayWidgets();
		}
		pie_SetFogStatus(true);
		wzPerfEnd(PERF_GUI);
	}

	pie_GetResetCounts(&loopPieCount, &loopPolyCount);

	// deal with the mission state
	switch (loopMissionState) {
	  case LMS_CLEAROBJECTS:
	  	missionDestroyObjects();
	  	setScriptPause(true);
	  	loopMissionState = LMS_SETUPMISSION;
	  	break;

	  case LMS_NORMAL:
	  	// default
	  	break;
	  case LMS_SETUPMISSION:
	  	setScriptPause(false);
	  	if (!setUpMission(nextMissionType)) {
	  		return GAME_CODE::QUITGAME;
	  	}
	  	break;
	  case LMS_SAVECONTINUE:
	  	// just wait for this to be changed when the new mission starts
	  	break;
	  case LMS_NEWLEVEL:
	  	nextMissionType = LEVEL_TYPE::LDS_NONE;
	  	return GAME_CODE::NEWLEVEL;
	  	break;
	  case LMS_LOADGAME:
	  	return GAME_CODE::LOADGAME;
	  	break;
	  default:
	  	ASSERT(false, "unknown loopMissionState");
	  	break;
	}

	int clearMode = 0;
	if (getDrawShadows()) {
		clearMode |= CLEAR_SHADOW;
	}
	if (quitting || loopMissionState == LMS_SAVECONTINUE) {
		pie_SetFogStatus(false);
		clearMode = CLEAR_BLACK;
	}
	pie_ScreenFlip(clearMode); //gameloopflip

	if (quitting) {
		return GAME_CODE::QUITGAME;
	}
	else if (loop_GetVideoStatus()) {
		audio_StopAll();
		return GAME_CODE::PLAYVIDEO;
	}

	return GAME_CODE::CONTINUE;
}

// Carry out the various counting operations we perform each loop
void countUpdate(bool synch)
{
	for (auto i = 0; i < MAX_PLAYERS; i++)
	{
		//set the flag for each player
		setSatUplinkExists(false, i);

		numCommandDroids[i] = 0;
		numConstructorDroids[i] = 0;
		numDroids[i] = 0;
		numMissionDroids[i] = 0;
		numTransporterDroids[i] = 0;

		for (auto& psCurr : apsDroidLists[i])
		{
			numDroids[i]++;
			switch (psCurr.getType()) {
        using enum DROID_TYPE;
			  case COMMAND:
			  	numCommandDroids[i] += 1;
			  	break;
			  case CONSTRUCT:
			  case CYBORG_CONSTRUCT:
			  	numConstructorDroids[i] += 1;
			  	break;
			  case TRANSPORTER:
			  case SUPER_TRANSPORTER:
			  	droidCountsInTransporter(&psCurr, i);
			  	break;
			  default:
			  	break;
			}
		}
		for (auto& psCurr : mission.apsDroidLists[i])
		{
			numMissionDroids[i]++;
			switch (psCurr->getType()) {
        using enum DROID_TYPE;
			  case COMMAND:
			  	numCommandDroids[i] += 1;
			  	break;
			  case CONSTRUCT:
			  case CYBORG_CONSTRUCT:
			  	numConstructorDroids[i] += 1;
			  	break;
			  case TRANSPORTER:
			  case SUPER_TRANSPORTER:
			  	droidCountsInTransporter(psCurr, i);
			  	break;
			  default:
			  	break;
			}
		}
		for (auto& psCurr : apsLimboDroids[i])
		{
			// count the type of units
			switch (psCurr->getType()) {
        using enum DROID_TYPE;
		  	case COMMAND:
		  		numCommandDroids[i] += 1;
		  		break;
		  	case CONSTRUCT:
		  	case CYBORG_CONSTRUCT:
		  		numConstructorDroids[i] += 1;
		  		break;
		  	default:
		  		break;
		  	}
		}
		// FIXME: These for-loops are code duplicationo
		setLasSatExists(false, i);
		for (auto& psCBuilding : apsStructLists[i])
		{
			if (psCBuilding->getStats().type == STRUCTURE_TYPE::SAT_UPLINK &&
          psCBuilding->getState() == STRUCTURE_STATE::BUILT) {
				setSatUplinkExists(true, i);
			}
			//don't wait for the Las Sat to be built - can't build another if one is partially built
			if (asWeaponStats[psCBuilding->asWeaps[0].nStat].weaponSubClass == WEAPON_SUBCLASS::LAS_SAT) {
				setLasSatExists(true, i);
			}
		}
		for (auto& psCBuilding : mission.apsStructLists[i])
		{
			if (psCBuilding->getStats().type == STRUCTURE_TYPE::SAT_UPLINK &&
          psCBuilding->getState() == STRUCTURE_STATE::BUILT) {
				setSatUplinkExists(true, i);
			}
			//don't wait for the Las Sat to be built - can't build another if one is partially built
			if (asWeaponStats[psCBuilding->asWeaps[0].nStat].weaponSubClass == WEAPON_SUBCLASS::LAS_SAT) {
				setLasSatExists(true, i);
			}
		}
		if (synch) {
			syncDebug("counts[%d] = {droid: %d, command: %d, constructor: %d, mission: %d, transporter: %d}", i,
			          numDroids[i], numCommandDroids[i], numConstructorDroids[i], numMissionDroids[i],
			          numTransporterDroids[i]);
		}
	}
}

static void gameStateUpdate()
{
	syncDebug(
		"map = \"%s\", pseudorandom 32-bit integer = 0x%08X, allocated = %d %d %d %d %d %d %d %d %d %d, position = %d %d %d %d %d %d %d %d %d %d",
		game.map, gameRandU32(),
		NetPlay.players[0].allocated, NetPlay.players[1].allocated, NetPlay.players[2].allocated,
		NetPlay.players[3].allocated, NetPlay.players[4].allocated, NetPlay.players[5].allocated,
		NetPlay.players[6].allocated, NetPlay.players[7].allocated, NetPlay.players[8].allocated,
		NetPlay.players[9].allocated,
		NetPlay.players[0].position, NetPlay.players[1].position, NetPlay.players[2].position,
		NetPlay.players[3].position, NetPlay.players[4].position, NetPlay.players[5].position,
		NetPlay.players[6].position, NetPlay.players[7].position, NetPlay.players[8].position,
		NetPlay.players[9].position
	);
	for (unsigned n = 0; n < MAX_PLAYERS; ++n)
	{
		syncDebug("Player %d = \"%s\"", n, NetPlay.players[n].name);
	}

	// Add version string to desynch logs. Different version strings will not trigger a desynch dump per se, due to the syncDebug{Get, Set}Crc guard.
	auto crc = syncDebugGetCrc();
	syncDebug("My client version = %s", version_getVersionString());
	syncDebugSetCrc(crc);

	// Actually send pending droid orders.
	sendQueuedDroidInfo();

	sendPlayerGameTime();
	NETflush(); // Make sure the game time tick message is really sent over the network.

	if (!paused && !scriptPaused()) {
		updateScripts();
	}

	// Update abandoned structures
	handleAbandonedStructures();

	// Update the visibility change stuff
	visUpdateLevel();

	// Put all droids/structures/features into the grid.
	gridReset();

	// Check which objects are visible.
	processVisibility();

	// Update the map.
	mapUpdate();

	//update the findpath system
	fpathUpdate();

	// update the command droids
	cmdDroidUpdate();

	for (auto i = 0; i < MAX_PLAYERS; i++)
	{
		//update the current power available for a player
		updatePlayerPower(i);

		for (auto& psCurr : apsDroidLists[i])
		{
			droidUpdate(&psCurr);
		}

		for (auto& psCurr : mission.apsDroidLists[i])
		{
			missionDroidUpdate(psCurr);
		}

		// FIXME: These for-loops are code duplication
		Structure* psNBuilding;
		for (auto& psCBuilding : apsStructLists[i])
		{
			psCBuilding->structureUpdate(false);
		}
		for (auto& psCBuilding : mission.apsStructLists[i])
		{
			psCBuilding->structureUpdate(true); // update for mission
		}
	}

	missionTimerUpdate();
	proj_UpdateAll();

	for (auto& psCFeat : apsFeatureLists)
	{
		featureUpdate(&psCFeat);
	}

	// Free dead droid memory.
	objmemUpdate();

	// Must end update, since we may or may not have ticked, and some message queue processing code may vary depending on whether it's in an update.
	gameTimeUpdateEnd();

	// Must be at the end of gameStateUpdate, since countUpdate is also called randomly (unsynchronised) between gameStateUpdate calls, but should have no effect if we already called it, and recvMessage requires consistent counts on all clients.
	countUpdate(true);
}

std::size_t getMaxFastForwardTicks()
{
	return maxFastForwardTicks;
}

void setMaxFastForwardTicks(optional<std::size_t> value, bool fixedToNormalTickRate)
{
	maxFastForwardTicks = value.value_or(WZ_DEFAULT_MAX_FASTFORWARD_TICKS);
	fastForwardTicksFixedToNormalTickRate = fixedToNormalTickRate;
}

/* The main game loop */
GAME_CODE gameLoop()
{
	static uint32_t lastFlushTime = 0;

	static std::size_t numForcedUpdatesLastCall = 0;
	static int renderBudget = 0; // Scaled time spent rendering minus scaled time spent updating.
	static bool previousUpdateWasRender = false;
	const Rational renderFraction(2, 5); // Minimum fraction of time spent rendering.
	const Rational updateFraction = Rational(1) - renderFraction;

	// Shouldn't this be when initialising the game, rather than randomly called between ticks?
	countUpdate(false); // kick off with correct counts

	std::size_t numRegularUpdatesTicks = 0;
	std::size_t numFastForwardTicks = 0;
	gameTimeUpdateBegin();
	while (true)
	{
		// Receive NET_BLAH messages.
		// Receive GAME_BLAH messages, and if it's time, process exactly as many GAME_BLAH messages as required to be able to tick the gameTime.
		recvMessage();

		bool selectedPlayerIsSpectator = bMultiPlayer && NetPlay.players[selectedPlayer].isSpectator;
		bool multiplayerHostDisconnected = bMultiPlayer && !NetPlay.isHostAlive && NetPlay.bComms && !NetPlay.isHost;
		// do not fast-forward after the host has disconnected
		bool canFastForwardGameTime =
			selectedPlayerIsSpectator // current player must be a spectator
			&& !NetPlay.isHost // AND NOT THE HOST (!)
			&& !multiplayerHostDisconnected // and the multiplayer host must not be disconnected ("host quit")
			&& numFastForwardTicks < maxFastForwardTicks
			// and the number of forced updates this call of gameLoop must not exceed the max allowed
			&& checkPlayerGameTime(NET_ALL_PLAYERS);
		// and there must be a new game tick available to process from all players

		bool forceTryGameTickUpdate = canFastForwardGameTime && ((!fastForwardTicksFixedToNormalTickRate &&
			numForcedUpdatesLastCall > 0) || numRegularUpdatesTicks > 0) && NETgameIsBehindPlayersByAtLeast(4);

		// Update gameTime and graphicsTime, and corresponding deltas. Note that gameTime and graphicsTime pause, if we aren't getting our GAME_GAME_TIME messages.
		auto timeUpdateResult = gameTimeUpdate(renderBudget > 0 || previousUpdateWasRender, forceTryGameTickUpdate);

		if (timeUpdateResult == GameTimeUpdateResult::GAME_TIME_UPDATED_FORCED)
		{
			numFastForwardTicks++;
			// FUTURE TODO: Could trigger the display of a UI icon here (if (numFastForwardTicks == maxFastForwardTicks)?) to indicate that the game is fast-forwarding substantially
		}
		else if (timeUpdateResult == GameTimeUpdateResult::GAME_TIME_UPDATED)
		{
			numRegularUpdatesTicks++;
		}

		if (deltaGameTime == 0)
		{
			break; // Not doing a game state update.
		}

		ASSERT(!paused && !gameUpdatePaused(), "Nonsensical pause values.");

		unsigned before = wzGetTicks();
		syncDebug("Begin game state update, gameTime = %d", gameTime);
		gameStateUpdate();
		syncDebug("End game state update, gameTime = %d", gameTime);
		unsigned after = wzGetTicks();

		renderBudget -= (after - before) * renderFraction.n;
		renderBudget = std::max(renderBudget, (-updateFraction * 500).floor());
		previousUpdateWasRender = false;

		ASSERT(deltaGraphicsTime == 0, "Shouldn't update graphics and game state at once.");
	}
	numForcedUpdatesLastCall = numFastForwardTicks;

	if (realTime - lastFlushTime >= 400u)
	{
		lastFlushTime = realTime;
		NETflush(); // Make sure that we aren't waiting too long to send data.
	}

	unsigned before = wzGetTicks();
	GAME_CODE renderReturn = renderLoop();
	unsigned after = wzGetTicks();

	renderBudget += (after - before) * updateFraction.n;
	renderBudget = std::min(renderBudget, (renderFraction * 500).floor());
	previousUpdateWasRender = true;

	if (headlessGameMode() && autogame_enabled())
	{
		// Output occasional stats to stdout
		stdOutGameSummary();
	}

	return renderReturn;
}

/* The video playback loop */
void videoLoop()
{
	bool videoFinished;

	ASSERT(videoMode == 1, "videoMode out of sync");

	// display a frame of the FMV
	videoFinished = !seq_UpdateFullScreenVideo(nullptr);
	pie_ScreenFlip(CLEAR_BLACK);

	if (skipCounter <= SEQUENCE_MIN_SKIP_DELAY)
	{
		skipCounter += 1; // "time" is stopped so we will count via loop iterations.
	}

	// should we stop playing?
	if (videoFinished || (skipCounter > SEQUENCE_MIN_SKIP_DELAY && (keyPressed(KEY_ESC) || mouseReleased(MOUSE_LMB))))
	{
		seq_StopFullScreenVideo();

		//set the next video off - if any
		if (videoFinished && seq_AnySeqLeft())
		{
			seq_StartNextFullScreenVideo();
		}
		else
		{
			// remove the intelligence screen if necessary
			if (messageIsImmediate())
			{
				intResetScreen(true);
				setMessageImmediate(false);
			}
			if (!bMultiPlayer && getScriptWinLoseVideo())
			{
				displayGameOver(getScriptWinLoseVideo() == PLAY_WIN, false);
			}
			triggerEvent(TRIGGER_VIDEO_QUIT);
		}
	}
}


void loop_SetVideoPlaybackMode()
{
	skipCounter = 0;
	videoMode += 1;
	paused = true;
	video = true;
	gameTimeStop();
	pie_SetFogStatus(false);
	audio_StopAll();
	wzShowMouse(false);
	screen_StopBackDrop();
	pie_ScreenFlip(CLEAR_BLACK);
}


void loop_ClearVideoPlaybackMode()
{
	skipCounter = 0;
	videoMode -= 1;
	paused = false;
	video = false;
	gameTimeStart();
	pie_SetFogStatus(true);
	cdAudio_Resume();
	wzShowMouse(true);
	ASSERT(videoMode == 0, "loop_ClearVideoPlaybackMode: out of sync.");
}


int loop_GetVideoMode()
{
	return videoMode;
}

bool loop_GetVideoStatus()
{
	return video;
}

bool gamePaused()
{
	return paused;
}

void setGamePauseStatus(bool val)
{
	paused = val;
}

bool gameUpdatePaused()
{
	return pauseState.gameUpdatePause;
}

bool audioPaused()
{
	return pauseState.audioPause;
}

bool scriptPaused()
{
	return pauseState.scriptPause;
}

bool scrollPaused()
{
	return pauseState.scrollPause;
}

bool consolePaused()
{
	return pauseState.consolePause;
}

void setGameUpdatePause(bool state)
{
	pauseState.gameUpdatePause = state;
}

void setAudioPause(bool state)
{
	pauseState.audioPause = state;
}

void setScriptPause(bool state)
{
	pauseState.scriptPause = state;
}

void setScrollPause(bool state)
{
	pauseState.scrollPause = state;
}

void setConsolePause(bool state)
{
	pauseState.consolePause = state;
}

//set all the pause states to the state value
void setAllPauseStates(bool state)
{
	setGameUpdatePause(state);
	setAudioPause(state);
	setScriptPause(state);
	setScrollPause(state);
	setConsolePause(state);
}

unsigned getNumDroids(unsigned player)
{
	return numDroids[player];
}

unsigned getNumTransporterDroids(unsigned player)
{
	return numTransporterDroids[player];
}

unsigned getNumMissionDroids(unsigned player)
{
	return numMissionDroids[player];
}

unsigned getNumCommandDroids(unsigned player)
{
	return numCommandDroids[player];
}

unsigned getNumConstructorDroids(unsigned player)
{
	return numConstructorDroids[player];
}

// increase the droid counts - used by update factory to keep the counts in sync
void adjustDroidCount(Droid* droid, int delta)
{
	auto player = droid->getPlayer();
	syncDebug("numDroids[%d]:%d=%d→%d", player, droid->getType(),
            numDroids[player], numDroids[player] + delta);
	numDroids[player] += delta;
  
	switch (droid->getType()) {
    using enum DROID_TYPE;
	  case COMMAND:
	  	numCommandDroids[player] += delta;
	  	break;
	  case CONSTRUCT:
	  case CYBORG_CONSTRUCT:
	  	numConstructorDroids[player] += delta;
	  	break;
	  default:
	  	break;
	}
}

// Increase counts of droids in a transporter
void droidCountsInTransporter(Droid* droid, unsigned player)
{
	if (!isTransporter(*droid) || droid->group == nullptr) {
		return;
	}

	numTransporterDroids[player] += droid->group->refCount - 1;

	// and count the units inside it...
	for (auto psDroid = droid->group->members; psDroid != nullptr && psDroid != droid; psDroid = psDroid->psGrpNext)
	{
    using enum DROID_TYPE;
		if (psDroid->type == CYBORG_CONSTRUCT || 
        psDroid->type == CONSTRUCT) {
			numConstructorDroids[player] += 1;
		}
		if (psDroid->type == COMMAND) {
			numCommandDroids[player] += 1;
		}
	}
}
