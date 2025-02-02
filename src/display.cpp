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
 * @file display.cpp
 * Display routines
 */

#include "lib/framework/math_ext.h"
#include "lib/framework/wzapp.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "action.h"
#include "animation.h"
#include "baseobject.h"
#include "cmddroid.h"
#include "display.h"
#include "display3d.h"
#include "edit3d.h"
#include "displaydef.h"
#include "fpath.h"
#include "geometry.h"
#include "ingameop.h"
#include "intimage.h"
#include "intorder.h"
#include "keybind.h"
#include "levels.h"
#include "loop.h"
#include "mapgrid.h"
#include "multiplay.h"
#include "objmem.h"
#include "projectile.h"
#include "qtscript.h"
#include "radar.h"
#include "transporter.h"
#include "warcam.h"
#include "warzoneconfig.h"
#include "wrappers.h"

InputManager gInputManager;
KeyFunctionConfiguration gKeyFuncConfig;
DragBox3D dragBox3D;
WallDrag wallDrag;


static constexpr auto POSSIBLE_SELECTIONS	= 14;
static constexpr auto POSSIBLE_TARGETS = 23;

// NOTE: the external file "cursorselection" is used, so you can import
// that into a spreadsheet, and edit it there, much easier.
static const CURSOR arnMPointers[POSSIBLE_TARGETS][POSSIBLE_SELECTIONS] =
{
#include "cursorselection"
};

int scrollDirLeftRight = 0;
int scrollDirUpDown = 0;

static bool buildingDamaged(Structure* psStructure);
static bool repairDroidSelected(unsigned player);
static bool vtolDroidSelected(unsigned player);
static bool anyDroidSelected(unsigned player);
static bool cyborgDroidSelected(unsigned player);
static bool bInvertMouse = true;
static bool bRightClickOrders = false;
static bool bMiddleClickRotate = false;
static bool bDrawShadows = true;
static SELECTION_TYPE establishSelection(unsigned selectedPlayer);
static void dealWithLMB();
static void dealWithLMBDClick();
static void dealWithRMB();
static void handleDeselectionClick();
static bool mouseInBox(int x0, int y0, int x1, int y1);
static FlagPosition* findMouseDeliveryPoint();

void finishDeliveryPosition();

static unsigned currentFrame;
static unsigned StartOfLastFrame;
static int rotX;
static int rotY;
std::unique_ptr<ValueTracker> rotationHorizontalTracker = std::make_unique<ValueTracker>();
std::unique_ptr<ValueTracker> rotationVerticalTracker = std::make_unique<ValueTracker>();
static uint32_t scrollRefTime;
static float scrollSpeedLeftRight; //use two directions and add them because its simple
static float scrollStepLeftRight;
static float scrollSpeedUpDown;
static float scrollStepUpDown;
static bool mouseOverRadar = false;
static bool mouseOverConsole = false;
static bool ignoreOrder = false;
static bool ignoreRMBC = true;
static Droid* psSelectedVtol;
static Droid* psDominantSelected;
static bool bRadarDragging = false;
static bool cameraAccel = true;

bool rotActive = false;
bool gameStats = false;
bool lockCameraScrollWhileRotating = false;

/* Hackety hack hack hack */
static int screenShakeTable[100] =
{
	-2, -2, -3, -4, -3, -3, -5, -4, -4, -4,
	-4, -5, -5, -5, -5, -7, -5, -6, -8, -6,
	-7, -8, -6, -4, -8, -7, -7, -7, -6, -5,
	-6, -5, -2, -5, -6, -3, -5, -3, -2, -4,
	-5, -3, -2, -0, 1, 2, 2, 1, 0, 0,
	0, 1, 1, 3, 2, 1, 0, 2, 3, 4,
	4, 2, 6, 4, 5, 3, 7, 7, 3, 6,
	4, 7, 9, 10, 9, 8, 6, 4, 7, 5,
	5, 4, 6, 2, 4, 5, 3, 3, 2, 1,
	1, 0, -1, -1, -2, -1, 1, 0, 1, 0
};

static bool bScreenShakeActive = false;
static unsigned screenShakeStarted;
static unsigned screenShakeLength;

static const unsigned FADE_START_OF_GAME_TIME = 1000;
static unsigned fadeEndTime = 0;
static void fadeStartOfGame();
static void handleAreaDemolition();

//used to determine is a weapon droid is assigned to a sensor tower or sensor droid
static bool bSensorAssigned;
//used to determine if the player has selected a Las Sat structure
static bool bLasSatStruct;
// Local prototypes
static MOUSE_TARGET itemUnderMouse(BaseObject ** ppObjUnderCursor);
// If shaking is allowed
static bool bShakingPermitted = true;

static auto viewDistanceAnimation = Animation<float>(realTime);
static uint32_t viewDistanceIncrementCooldownTime = 0;

void animateToViewDistance(float target, float speed)
{
	viewDistanceAnimation
		.setInitialData(getViewDistance())
		.setFinalData(target)
		.setEasing(viewDistanceAnimation.isActive()
                             ? EASING_FUNCTION::EASE_OUT 
                             : EASING_FUNCTION::EASE_IN_OUT)
		.setDuration(speed <= 0 ? 0
			             : static_cast<uint32_t>(glm::log(std::abs(target - getViewDistance())) * 100 *
				             DEFAULT_VIEW_DISTANCE_ANIMATION_SPEED / speed))
		.start();
}

void incrementViewDistance(float amount)
{
	if (InGameOpUp || bDisplayMultiJoiningStatus || isInGamePopupUp || realTime < viewDistanceIncrementCooldownTime)
	{
		return;
	}

	viewDistanceIncrementCooldownTime = realTime + GAME_TICKS_PER_SEC / 50;
	const DebugInputManager& dbgInputManager = gInputManager.debugManager();
	auto target = (viewDistanceAnimation.isActive() ? viewDistanceAnimation.getFinalData() : getViewDistance()) +
                amount;
	if (!dbgInputManager.debugMappingsAllowed())
	{
		CLIP(target, MINDISTANCE, (!NETisReplay()) ? MAXDISTANCE : MAXDISTANCE_REPLAY);
	}

	animateToViewDistance(target);
}

static void updateViewDistanceAnimation()
{
	if (viewDistanceAnimation.isActive())
	{
		viewDistanceAnimation.update();
		setViewDistance(viewDistanceAnimation.getCurrent());
	}
}

bool getShakeStatus()
{
	return bShakingPermitted;
}

void setShakeStatus(bool val)
{
	bShakingPermitted = val;
}

void shakeStart(unsigned int length)
{
  if (!bShakingPermitted || bScreenShakeActive)
    return;

  bScreenShakeActive = true;
  screenShakeStarted = gameTime;
  screenShakeLength = length;
}

void shakeStop()
{
	bScreenShakeActive = false;
	playerPos.r.z = 0;
}

static void shakeUpdate()
{
	unsigned screenShakePercentage;

	/* Check if we're shaking the screen or not */
  if (!bScreenShakeActive) {
    if (!getWarCamStatus()) {
      playerPos.r.z = 0;
    }
    return;
  }

  screenShakePercentage = PERCENT(gameTime - screenShakeStarted, screenShakeLength);
  if (screenShakePercentage < 100) {
    playerPos.r.z = 0 + DEG(screenShakeTable[screenShakePercentage]);
  }

  if (gameTime > (screenShakeStarted + screenShakeLength)) {
    bScreenShakeActive = false;
    playerPos.r.z = 0;
  }
}

bool isMouseOverRadar()
{
	return mouseOverRadar;
}

bool getCameraAccel()
{
	return cameraAccel;
}

void setCameraAccel(bool val)
{
	cameraAccel = val;
}

bool getInvertMouseStatus()
{
  return bInvertMouse;
}

void setInvertMouseStatus(bool val)
{
	bInvertMouse = val;
}


#define MOUSE_ORDER (bRightClickOrders?MOUSE_RMB:MOUSE_LMB)
#define MOUSE_SELECT (bRightClickOrders?MOUSE_LMB:MOUSE_RMB)
#define MOUSE_ROTATE (bMiddleClickRotate?MOUSE_MMB:MOUSE_RMB)
#define MOUSE_PAN (bMiddleClickRotate?MOUSE_RMB:MOUSE_MMB)

bool getRightClickOrders()
{
	return bRightClickOrders;
}

void setRightClickOrders(bool val)
{
	bRightClickOrders = val;
}

bool getMiddleClickRotate()
{
	return bMiddleClickRotate;
}

void setMiddleClickRotate(bool val)
{
	bMiddleClickRotate = val;
}

bool getDrawShadows()
{
	return (bDrawShadows);
}

void setDrawShadows(bool val)
{
	bDrawShadows = val;
	pie_setShadows(val);
}

void ProcessRadarInput()
{
	int PosX, PosY;
	int x = mouseX();
	int y = mouseY();
	unsigned temp1, temp2;

	/* Only allow jump-to-area-of-map if radar is on-screen */
	mouseOverRadar = false;
  if (!radarVisible() || !CoordInRadar(x, y))
    return;

  mouseOverRadar = true;

  if (mousePressed(MOUSE_ORDER)) {
    x = mousePressPos_DEPRECATED(MOUSE_ORDER).x;
    y = mousePressPos_DEPRECATED(MOUSE_ORDER).y;

    /* If we're tracking a droid, then cancel that */
    CalcRadarPosition(x, y, &PosX, &PosY);
    if (mouseOverRadar && selectedPlayer < MAX_PLAYERS) {
      // MARKER
      // Send all droids to that location
      orderSelectedLoc(selectedPlayer, (PosX * TILE_UNITS) + TILE_UNITS / 2,
                       (PosY * TILE_UNITS) + TILE_UNITS / 2, ctrlShiftDown());
      // ctrlShiftDown() = ctrl clicked a destination, add an order
    }
    CheckScrollLimits();
    audio_PlayTrack(ID_SOUND_MESSAGEEND);
  }

  if (mouseDrag(MOUSE_SELECT, &temp1, &temp2) && !rotActive) {
    CalcRadarPosition(x, y, &PosX, &PosY);
    setViewPos(PosX, PosY, true);
    bRadarDragging = true;
    if (ctrlShiftDown()) {
      playerPos.r.y = 0;
    }
    return;
  }

  if (!mousePressed(MOUSE_SELECT))
    return;

  x = mousePressPos_DEPRECATED(MOUSE_SELECT).x;
  y = mousePressPos_DEPRECATED(MOUSE_SELECT).y;
  CalcRadarPosition(x, y, &PosX, &PosY);

  if (war_GetRadarJump()) {
    /* Go instantly */
    setViewPos(PosX, PosY, true);
    return;
  }

  /* Pan to it */
  requestRadarTrack(PosX * TILE_UNITS, PosY * TILE_UNITS);
}

// reset the input state
void resetInput()
{
	rotActive = false;
	dragBox3D.status = DRAG_INACTIVE;
	wallDrag.status = DRAG_INACTIVE;
	gInputManager.contexts().resetStates();
}

static bool localPlayerHasSelection()
{
	if (selectedPlayer >= MAX_PLAYERS) {
		return false;
	}

	for (auto& psDroid : playerList[selectedPlayer].droids)
	{
		if (psDroid.damageManager->isSelected()) {
			return true;
		}
	}

	for (auto& psStruct : playerList[selectedPlayer].structures)
	{
		if (psStruct.damageManager->isSelected()) {
			return true;
		}
	}

	return false;
}

/* Process the user input. This just processes the key input and jumping around the radar*/
void processInput()
{
	if (InGameOpUp || isInGamePopupUp) {
		dragBox3D.status = DRAG_INACTIVE; // disengage the dragging since it stops menu input
	}


	StartOfLastFrame = currentFrame;
	currentFrame = frameGetFrameNumber();
	ignoreRMBC = false;

	const bool mOverConstruction = CoordInBuild(mouseX(), mouseY());
	const bool mouseIsOverScreenOverlayChild = isMouseOverScreenOverlayChild(mouseX(), mouseY());

	if (!mouseIsOverScreenOverlayChild) {
		mouseOverConsole = mouseOverHistoryConsoleBox();

		/* Process all of our key mappings */
		if (mOverConstruction) {
			if (mousePressed(MOUSE_WUP)) {
				kf_BuildPrevPage();
			}

			if (mousePressed(MOUSE_WDN)) {
				kf_BuildNextPage();
			}
		}
	}

	gInputManager.contexts().set(
		InputContext::DEBUG_HAS_SELECTION,
		localPlayerHasSelection() ? InputContext::State::ACTIVE : InputContext::State::INACTIVE
	);
	gInputManager.contexts().updatePriorityStatus();

	if (!isInTextInputMode()) {
		const bool allowMouseWheelEvents = !mouseIsOverScreenOverlayChild && !mouseOverConsole && !mOverConstruction;
		gInputManager.processMappings(allowMouseWheelEvents);
	}

	/* Allow the user to clear the (Active) console if need be */
	if (!mouseIsOverScreenOverlayChild && mouseOverConsoleBox() && mousePressed(MOUSE_LMB)) {
		clearActiveConsole();
	}
}

static bool OverRadarAndNotDragging()
{
	return mouseOverRadar && dragBox3D.status != DRAG_DRAGGING && wallDrag.status != DRAG_DRAGGING;
}

static void CheckFinishedDrag(SELECTION_TYPE selection)
{
  if (!mouseReleased(MOUSE_LMB) && !mouseDown(MOUSE_RMB))
    return;

  selectAttempt = false;
  if (dragBox3D.status != DRAG_DRAGGING) {
    dragBox3D.status = DRAG_INACTIVE;
    wallDrag.status = DRAG_INACTIVE;
    return;
  }

  if (wallDrag.status == DRAG_DRAGGING && (buildState == BUILD3D_VALID || buildState == BUILD3D_FINISHED) &&
      sBuildDetails.psStats->hasType(STAT_STRUCTURE) && canLineBuild()) {
    wallDrag.pos2 = mousePos;
    wallDrag.status = DRAG_RELEASED;
  }

  /* Only clear if shift isn't down - this is for the drag selection box for units*/
  if (!ctrlShiftDown() && wallDrag.status == DRAG_INACTIVE) {
    clearSelection();
  }

  dragBox3D.status = DRAG_RELEASED;
  dragBox3D.x2 = mouseX();
  dragBox3D.y2 = mouseY();
  if (selection == SC_DROID_DEMOLISH && ctrlShiftDown()) {
    handleAreaDemolition();
  }
}

/**
 * Demolish all structures in given area
 * Note: Does not attempt to optimize movement paths,
 * so demolishing can be a little out of order
*/
static void handleAreaDemolition()
{
	const Vector2i pt1(dragBox3D.x1, dragBox3D.y1);
	const Vector2i pt2(dragBox3D.x2, dragBox3D.y2);
	Vector2i worldCoord1(0, 0), worldCoord2(0, 0), tmp;
	int notused1 = 0, notused2 = 0;
	screenCoordToWorld(pt1, worldCoord1, notused1, notused2);
	screenCoordToWorld(pt2, worldCoord2, notused1, notused2);
	// swap coordinates to be in increasing order.. otherwise gridIterate doesn't work
	tmp = worldCoord1;
	worldCoord1.x = worldCoord1.x < worldCoord2.x ? worldCoord1.x : worldCoord2.x;
	worldCoord1.y = worldCoord1.y < worldCoord2.y ? worldCoord1.y : worldCoord2.y;
	worldCoord2.x = worldCoord2.x > tmp.x ? worldCoord2.x : tmp.x;
	worldCoord2.y = worldCoord2.y > tmp.y ? worldCoord2.y : tmp.y;

	debug(LOG_INFO, "demolish everything in the area (%i %i) -> (%i %i)", worldCoord1.x, worldCoord1.y, worldCoord2.x,
	      worldCoord2.y);
	std::vector<BaseObject *> gridList = gridStartIterateArea(worldCoord1.x, worldCoord1.y, worldCoord2.x,
                                                                 worldCoord2.y);
	for (auto psObj : gridList)
	{
		if (dynamic_cast<Structure*>(psObj) &&
        psObj->playerManager->isSelectedPlayer()) {
			// add demolish order to queue for every selected unit
			orderSelectedObjAdd(selectedPlayer, psObj, true);
		}
	}
}

static void CheckStartWallDrag()
{
  if (!mousePressed(MOUSE_LMB))
    return;

  /* Store away the details if we're building */
  // You can start dragging walls from invalid locations so check for
  // BUILD3D_POS or BUILD3D_VALID, used tojust check for BUILD3D_VALID.
  if ((buildState == BUILD3D_POS || buildState == BUILD3D_VALID) &&
      sBuildDetails.psStats->hasType(STAT_STRUCTURE)) {
    if (!canLineBuild())
      return;

    wallDrag.pos = wallDrag.pos2 = mousePos;
    wallDrag.status = DRAG_PLACING;
    debug(LOG_NEVER, "Start Wall Drag\n");
    return;
  }

  if (intBuildSelectMode()) { //if we were in build select mode
    //uhoh no place to build here
    audio_PlayBuildFailedOnce();
  }
}

//this function is called when a location has been chosen to place a structure or a DP
static bool CheckFinishedFindPosition()
{
	bool OverRadar = OverRadarAndNotDragging();

	/* Do not let the player position buildings 'under' the radar */
  if (!mouseReleased(MOUSE_LMB) || OverRadar)
    return false;

  if (deliveryReposValid()) {
    finishDeliveryPosition();
    return true;
  }

  if (buildState != BUILD3D_VALID)
    return false;

  if (sBuildDetails.psStats->hasType(STAT_STRUCTURE) && canLineBuild()) {
    wallDrag.pos2 = mousePos;
    wallDrag.status = DRAG_RELEASED;
  }

  debug(LOG_NEVER, "BUILD3D_FINISHED\n");
  buildState = BUILD3D_FINISHED;
  return true;
}

static void HandleDrag()
{
	unsigned dragX = 0, dragY = 0;

  if (!mouseDrag(MOUSE_LMB, &dragX, &dragY) ||
      mouseOverRadar || mouseDown(MOUSE_RMB))
    return;

  dragBox3D.x1 = dragX;
  dragBox3D.x2 = mouseX();
  dragBox3D.y1 = dragY;
  dragBox3D.y2 = mouseY();
  dragBox3D.status = DRAG_DRAGGING;

  if (buildState == BUILD3D_VALID && canLineBuild()) {
    wallDrag.pos2 = mousePos;
    wallDrag.status = DRAG_DRAGGING;
  }
}

// Mouse X coordinate at start of panning.
unsigned panMouseX;
// Mouse Y coordinate at start of panning.
unsigned panMouseY;
auto panXTracker = std::make_unique<ValueTracker>();
auto panZTracker = std::make_unique<ValueTracker>();
bool panActive;

//don't want to do any of these whilst in the Intelligence Screen
void processMouseClickInput()
{
	unsigned i;
	SELECTION_TYPE selection;
	MOUSE_TARGET item = MT_NOTARGET;
	bool OverRadar = OverRadarAndNotDragging();
	selection = establishSelection(selectedPlayer);
	ASSERT(selection <= POSSIBLE_SELECTIONS, "Weirdy selection!");

	ignoreOrder = CheckFinishedFindPosition();

	CheckStartWallDrag();

	HandleDrag();

	CheckFinishedDrag(selection);

	if (isMouseOverScreenOverlayChild(mouseX(), mouseY()))
	{
		// ignore clicks
		return;
	}

	if (mouseReleased(MOUSE_LMB) && !OverRadar && dragBox3D.status != DRAG_RELEASED && !ignoreOrder && !mouseOverConsole
		&& !bDisplayMultiJoiningStatus)
	{
		if (bRightClickOrders)
		{
			dealWithRMB();
		}
		else
		{
			if (!bMultiPlayer && establishSelection(selectedPlayer) == SC_DROID_SUPERTRANSPORTER)
			{
				// Never, *ever* let user control the transport in SP games--it breaks the scripts!
				ASSERT(game.type == LEVEL_TYPE::CAMPAIGN, "Game type was set incorrectly!");
			}
			else
			{
				dealWithLMB();
			}
		}
	}

	if (mouseDClicked(MOUSE_LMB))
	{
		dealWithLMBDClick();
	}

	if (mouseReleased(MOUSE_RMB) && !rotActive && !panActive && !ignoreRMBC)
	{
		dragBox3D.status = DRAG_INACTIVE;
		// Pretty sure we wan't set walldrag status here aswell.
		wallDrag.status = DRAG_INACTIVE;
		bRadarDragging = false;
		if (bRightClickOrders)
		{
			dealWithLMB();
		}
		else
		{
			dealWithRMB();
		}
		// Why?
		if (getWarCamStatus())
		{
			camToggleStatus();
		}
	}

	if (!mouseDrag(MOUSE_SELECT, (unsigned*)&rotX, (unsigned*)&rotY) && bRadarDragging)
	{
		bRadarDragging = false;
	}

	/* Right mouse click kills a building placement */
	if (!rotActive && mouseReleased(MOUSE_RMB) &&
		(buildState == BUILD3D_POS || buildState == BUILD3D_VALID))
	{
		/* Stop the placement */
		kill3DBuilding();
		bRadarDragging = false;
	}
	if (mouseReleased(MOUSE_RMB))
	{
		cancelDeliveryRepos();
	}
	if (mouseDrag(MOUSE_ROTATE, (unsigned*)&rotX, (unsigned*)&rotY) && !rotActive && !bRadarDragging && !
		getRadarTrackingStatus())
	{
		rotationVerticalTracker->start((UWORD) playerPos.r.x);
		rotationHorizontalTracker->start((UWORD) playerPos.r.y);
		// negative values caused problems with float conversion
		rotActive = true;
	}
	if (mouseDrag(MOUSE_PAN, (unsigned*)&panMouseX, (unsigned*)&panMouseY) && !rotActive && !panActive && !bRadarDragging &&
		!getRadarTrackingStatus())
	{
		panXTracker->start(playerPos.p.x);
		panZTracker->start(playerPos.p.z);
		panActive = true;
	}


	if (gamePaused())
	{
		wzSetCursor(CURSOR_DEFAULT);
	}
	if (buildState == BUILD3D_VALID) {
		// special casing for building
		wzSetCursor(CURSOR_BUILD);
	}
	else if (buildState == BUILD3D_POS) {
		// special casing for building - can't build here
		wzSetCursor(CURSOR_NOTPOSSIBLE);
	}
	else if (selection != SC_INVALID) {
    BaseObject * ObjUnderMouse;
		bool ObjAllied;

		item = itemUnderMouse(&ObjUnderMouse);
		ASSERT(item < POSSIBLE_TARGETS, "Weirdy target!");

		ASSERT(selectedPlayer < MAX_PLAYERS, "selectedPlayer is too high: %" PRIu32 "", selectedPlayer);
		ObjAllied = (ObjUnderMouse && selectedPlayer != ObjUnderMouse->playerManager->getPlayer() && aiCheckAlliances(
			selectedPlayer, ObjUnderMouse->playerManager->getPlayer()));

		if (item != MT_NOTARGET) {
			// exceptions to the lookup table.
			if (ctrlShiftDown() &&
          ObjUnderMouse != nullptr &&
          ObjUnderMouse->playerManager->getPlayer() == selectedPlayer &&
          dynamic_cast<Droid*>(ObjUnderMouse)) {
				item = MT_OWNDROID;
			}
			else if (specialOrderKeyDown() &&
				(ObjUnderMouse != nullptr) &&
				ObjUnderMouse->playerManager->getPlayer() == selectedPlayer)
			{
				if (selection == SC_DROID_REPAIR)
				{
					item = MT_OWNDROIDDAM;
				}
				else
				{
					// attacking own unit
					item = MT_ENEMYDROID;
				}
			}
			else if (selection == SC_DROID_REPAIR)
			{
				// We can't repair ourselves, so change it to a blocking cursor
				for (auto& psCurr : playerList[selectedPlayer].droids)
				{
					if (psCurr.damageManager->isSelected()) {
						if ((ObjUnderMouse != nullptr) && ObjUnderMouse->playerManager->getPlayer() == selectedPlayer && psCurr.getId() ==
							ObjUnderMouse->getId()) {
							item = MT_BLOCKING;
						}
						break;
					}
				}
			}
			else if (selection == SC_DROID_DEMOLISH) {
				// Can't demolish allied objects, or something that isn't built yet
				if (ObjAllied || (ObjUnderMouse &&
             !dynamic_cast<Structure*>(ObjUnderMouse) ||
                (dynamic_cast<Structure*>(ObjUnderMouse)->getState() == STRUCTURE_STATE::BLUEPRINT_PLANNED))) {

					item = MT_BLOCKING;
				}
			}
			// in multiPlayer check for what kind of unit can use it (TODO)
			else if (bMultiPlayer && item == MT_TRANDROID) {
				if (!ObjUnderMouse->playerManager->isSelectedPlayer()) {
					item = MT_OWNDROID;
				}
			}
			else if (selection == SC_DROID_CONSTRUCT) {
				// We don't allow the build cursor under certain circumstances ....
				// can't build if res extractors arent available.
				if (item == MT_RESOURCE) {
					for (i = 0; i < numStructureStats && asStructureStats[i].type != STRUCTURE_TYPE::RESOURCE_EXTRACTOR; i++)
					{
					} // find resource stat
					if (i < numStructureStats && apStructTypeLists[selectedPlayer][i] != AVAILABLE)
					// check if you can build it! {
						item = MT_BLOCKING; // don't allow build pointer.
					}
				}
				// repair instead of sensor/guard with cons. droids.
				else if (item == MT_SENSOR) {
					if (ObjUnderMouse // something valid
						  && (getObjectType(ObjUnderMouse) == OBJECT_TYPE::STRUCTURE)) { // check if struct
						if (buildingDamaged(dynamic_cast<Structure*>(ObjUnderMouse))) {
							item = MT_OWNSTRDAM; // replace guard/sense with usual icons.
						}
						else {
							item = MT_OWNSTROK;
						}
					}
				}
			}
			else if (item == MT_SENSOR
				&& selection == SC_DROID_INDIRECT
				&& (keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT))) {
				selection = SC_DROID_SENSOR;
			}

			// check the type of sensor for indirect weapons
			else if ((item == MT_SENSOR || item == MT_SENSORSTRUCT || item == MT_SENSORSTRUCTDAM)
				&& selection == SC_DROID_INDIRECT) {
				if (ObjUnderMouse && !droidSensorDroidWeapon(ObjUnderMouse, psDominantSelected)) {
					item = MT_BLOCKING;
				}
			}

			//check for VTOL droids being assigned to a sensor droid/structure
			else if ((item == MT_SENSOR || item == MT_SENSORSTRUCT || item == MT_SENSORSTRUCTDAM)
				&& selection == SC_DROID_DIRECT
				&& vtolDroidSelected((UBYTE)selectedPlayer)) {
				// NB. psSelectedVtol was set by vtolDroidSelected - yes I know its horrible, but it
				// only smells as much as the rest of display.c so I don't feel so bad
				if (ObjUnderMouse && droidSensorDroidWeapon(ObjUnderMouse, psSelectedVtol)) {
					selection = SC_DROID_INDIRECT;
				} else {
					item = MT_BLOCKING;
				}
			}

			//vtols cannot pick up artifacts
			else if (item == MT_ARTIFACT
				&& selection == SC_DROID_DIRECT
				&& vtolDroidSelected((UBYTE)selectedPlayer))
			{
				item = MT_BLOCKING;
			}

			if (item == MT_TERRAIN
				&& terrainType(mapTile(mouseTileX, mouseTileY)) == TER_CLIFFFACE)
			{
				item = MT_BLOCKING;
			}
			// special droid at full health
			if (arnMPointers[item][selection] == CURSOR_FIX &&
          ObjUnderMouse && dynamic_cast<Droid*>(ObjUnderMouse) &&
          !droidIsDamaged((Droid*)ObjUnderMouse)) {
				item = MT_OWNDROID;
			}
			if ((arnMPointers[item][selection] == CURSOR_SELECT ||
				arnMPointers[item][selection] == CURSOR_EMBARK ||
				arnMPointers[item][selection] == CURSOR_ATTACH ||
				arnMPointers[item][selection] == CURSOR_LOCKON ||
				arnMPointers[item][selection] == CURSOR_DEST) && ObjAllied)
			{
				// If you want to do these things, just gift your unit to your ally.
				item = MT_BLOCKING;
			}

			if (specialOrderKeyDown() && (selection == SC_DROID_TRANSPORTER || selection == SC_DROID_SUPERTRANSPORTER)
				&&
				arnMPointers[item][selection] == CURSOR_MOVE && bMultiPlayer)
			{
				// Alt+move = disembark transporter
				wzSetCursor(CURSOR_DISEMBARK);
			}
			else if (specialOrderKeyDown() && selection == SC_DROID_DIRECT &&
				arnMPointers[item][selection] == CURSOR_MOVE) {
				// Alt+move = scout
				wzSetCursor(CURSOR_SCOUT);
			}
			else if (arnMPointers[item][selection] == CURSOR_NOTPOSSIBLE &&
               ObjUnderMouse && ObjUnderMouse->playerManager->isSelectedPlayer() &&
               getObjectType(ObjUnderMouse) == OBJECT_TYPE::STRUCTURE &&
               dynamic_cast<Structure*>(ObjUnderMouse)->weaponManager->weapons[0].stats &&
							 dynamic_cast<Structure*>(ObjUnderMouse)->weaponManager->weapons[0].stats->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT) {
				wzSetCursor(CURSOR_SELECT); // Special casing for LasSat
			}
			else {
				wzSetCursor(arnMPointers[item][selection]);
			}
		}
		else
		{
			wzSetCursor(CURSOR_DEFAULT);
		}
	}
	else {
    BaseObject * ObjUnderMouse;
		item = itemUnderMouse(&ObjUnderMouse);

		//exceptions, exceptions...AB 10/06/99
		if (bMultiPlayer && bLasSatStruct)
		{
			ASSERT(item < POSSIBLE_TARGETS, "Weirdy target!");
			if (item == MT_ENEMYDROID || item == MT_ENEMYSTR || item == MT_DAMFEATURE)
			{
				//display attack cursor
				wzSetCursor(CURSOR_ATTACK);
			}
			else if (ObjUnderMouse && ObjUnderMouse->getPlayer() == selectedPlayer && (ObjUnderMouse->type == OBJ_DROID ||
				(ObjUnderMouse->type == OBJ_STRUCTURE && lasSatStructSelected((Structure*)ObjUnderMouse))))
			{
				// Special casing for selectables
				wzSetCursor(CURSOR_SELECT);
			}
			else if (ObjUnderMouse && ObjUnderMouse->getPlayer() == selectedPlayer && ObjUnderMouse->type == OBJ_STRUCTURE)
			{
				wzSetCursor(CURSOR_DEFAULT);
			}
			else
			{
				//display block cursor
				wzSetCursor(CURSOR_NOTPOSSIBLE);
			}
		}
		else if (ObjUnderMouse && (ObjUnderMouse->getPlayer() == selectedPlayer) &&
			((ObjUnderMouse->type == OBJ_STRUCTURE && ((Structure*)ObjUnderMouse)->asWeaps[0].nStat
					&& (asWeaponStats[((Structure*)ObjUnderMouse)->asWeaps[0].nStat].weaponSubClass == WSC_LAS_SAT))
				|| ObjUnderMouse->type == OBJ_DROID))
		{
			wzSetCursor(CURSOR_SELECT); // Special casing for LasSat or own unit
		}
	}
}

static void calcScroll(float* y, float* dydt, float accel, float decel, float targetVelocity, float dt)
{
	double tMid;

	// Stop instantly, if trying to change direction.
	if (targetVelocity * *dydt < -1e-8f)
	{
		*dydt = 0;
	}

	if (targetVelocity < *dydt)
	{
		accel = -accel;
		decel = -decel;
	}

	// Decelerate if needed.
	tMid = (0 - *dydt) / decel;
	CLIP(tMid, 0, dt);
	*y += static_cast<float>(*dydt * tMid + decel / 2 * tMid * tMid);
	if (cameraAccel)
	{
		*dydt += static_cast<float>(decel * tMid);
	}
	dt -= static_cast<float>(tMid);

	// Accelerate if needed.
	tMid = (targetVelocity - *dydt) / accel;
	CLIP(tMid, 0, dt);
	*y += static_cast<float>(*dydt * tMid + accel / 2 * tMid * tMid);
	if (cameraAccel)
	{
		*dydt += static_cast<float>(accel * tMid);
	}
	else
	{
		*dydt = targetVelocity;
	}
	dt -= static_cast<float>(tMid);

	// Continue at target velocity.
	*y += *dydt * dt;
}

static void handleCameraScrolling()
{
	int xDif, yDif;
	uint32_t timeDiff;
	float scroll_zoom_factor = 1 + 2 * ((getViewDistance() - MINDISTANCE) / ((float)(MAXDISTANCE - MINDISTANCE)));

	float scaled_max_scroll_speed = scroll_zoom_factor * (
		cameraAccel ? war_GetCameraSpeed() : war_GetCameraSpeed() / 2);
	float scaled_accel = scaled_max_scroll_speed / 2;

	if (InGameOpUp || bDisplayMultiJoiningStatus || isInGamePopupUp) // cant scroll when menu up. or when over radar
	{
		return;
	}

	if (lockCameraScrollWhileRotating && rotActive && (scrollDirUpDown == 0 && scrollDirLeftRight == 0))
	{
		resetScroll();
		return;
	}

	if (wzMouseInWindow())
	{
		if (mouseY() < BOUNDARY_Y)
		{
			scrollDirUpDown++;
			wzSetCursor(CURSOR_UARROW);
		}
		if (mouseY() > (pie_GetVideoBufferHeight() - BOUNDARY_Y))
		{
			scrollDirUpDown--;
			wzSetCursor(CURSOR_DARROW);
		}
		if (mouseX() < BOUNDARY_X)
		{
			wzSetCursor(CURSOR_LARROW);
			scrollDirLeftRight--;
		}
		if (mouseX() > (pie_GetVideoBufferWidth() - BOUNDARY_X))
		{
			wzSetCursor(CURSOR_RARROW);
			scrollDirLeftRight++;
		}
	}
	CLIP(scrollDirLeftRight, -1, 1);
	CLIP(scrollDirUpDown, -1, 1);

	if (scrollDirLeftRight != 0 || scrollDirUpDown != 0)
	{
		setWarCamActive(false); // Don't let this thing override the user trying to scroll.
	}

	// Apparently there's stutter if using deltaRealTime, so we have our very own delta time here, just for us.
	timeDiff = wzGetTicks() - scrollRefTime;
	scrollRefTime += timeDiff;
	timeDiff = std::min<unsigned>(timeDiff, 500);
	// Since we're using our own time variable, which isn't updated when dragging a box, clamp the time here so releasing the box doesn't scroll to the edge of the map suddenly.

	scrollStepLeftRight = 0;
	scrollStepUpDown = 0;
	calcScroll(&scrollStepLeftRight, &scrollSpeedLeftRight, scaled_accel, 2 * scaled_accel,
	           scrollDirLeftRight * scaled_max_scroll_speed, (float)timeDiff / GAME_TICKS_PER_SEC);
	calcScroll(&scrollStepUpDown, &scrollSpeedUpDown, scaled_accel, 2 * scaled_accel,
	           scrollDirUpDown * scaled_max_scroll_speed, (float)timeDiff / GAME_TICKS_PER_SEC);

	/* Get x component of movement */
	xDif = (int)(cos(-playerPos.r.y * (M_PI / 32768)) * scrollStepLeftRight + sin(-playerPos.r.y * (M_PI / 32768)) *
		scrollStepUpDown);
	/* Get y component of movement */
	yDif = (int)(sin(-playerPos.r.y * (M_PI / 32768)) * scrollStepLeftRight - cos(-playerPos.r.y * (M_PI / 32768)) *
		scrollStepUpDown);

	/* Adjust player's position by these components */
	playerPos.p.x += xDif;
	playerPos.p.z += yDif;

	CheckScrollLimits();

	// Reset scroll directions
	scrollDirLeftRight = 0;
	scrollDirUpDown = 0;
}

void displayRenderLoop()
{
	handleCameraScrolling();
	updateViewDistanceAnimation();
}

/*
 * Reset scrolling, so we don't jump around after unpausing.
 */
void resetScroll()
{
	scrollRefTime = wzGetTicks();
	scrollSpeedUpDown = 0.0f;
	scrollSpeedLeftRight = 0.0f;
	scrollDirLeftRight = 0;
	scrollDirUpDown = 0;
}

// Check a coordinate is within the scroll limits, int version.
// Returns true if edge hit.
//
bool CheckInScrollLimits(int* xPos, int* zPos)
{
	bool EdgeHit = false;
	int minX, minY, maxX, maxY;

	minX = world_coord(scrollMinX);
	maxX = world_coord(scrollMaxX - 1);
	minY = world_coord(scrollMinY);
	maxY = world_coord(scrollMaxY - 1);

	//scroll is limited to what can be seen for current campaign
	if (*xPos < minX)
	{
		*xPos = minX;
		EdgeHit = true;
	}
	else if (*xPos >= maxX)
	{
		*xPos = maxX;
		EdgeHit = true;
	}

	if (*zPos < minY)
	{
		*zPos = minY;
		EdgeHit = true;
	}
	else if (*zPos >= maxY)
	{
		*zPos = maxY;
		EdgeHit = true;
	}

	return EdgeHit;
}

// Check the view is within the scroll limits,
// Returns true if edge hit.
//
bool CheckScrollLimits()
{
	int xp = playerPos.p.x;
	int zp = playerPos.p.z;
	bool ret = CheckInScrollLimits(&xp, &zp);

	playerPos.p.x = xp;
	playerPos.p.z = zp;

	return ret;
}

/* Do the 3D display */
void displayWorld()
{
	if (headlessGameMode())
	{
		return;
	}

	Vector3i pos;

	shakeUpdate();

	if (panActive)
	{
		if (!mouseDown(MOUSE_PAN))
		{
			panActive = false;
		}
		else
		{
			int mouseDeltaX = mouseX() - panMouseX;
			int mouseDeltaY = mouseY() - panMouseY;

			int panningSpeed = std::min(mapWidth, mapHeight) / 10;

      panXTracker->set_target_delta(mouseDeltaX * panningSpeed);
      panXTracker->update();
			float horizontalMovement = panXTracker->get_current_delta();
      panZTracker->set_target_delta(mouseDeltaY * panningSpeed);
      panZTracker->update();
			float verticalMovement = -1 * panZTracker->get_current_delta();

			playerPos.p.x = static_cast<int>(panXTracker->get_initial()
				+ cos(-playerPos.r.y * (M_PI / 32768)) * horizontalMovement
				+ sin(-playerPos.r.y * (M_PI / 32768)) * verticalMovement);
			playerPos.p.z = static_cast<int>(panZTracker->get_initial()
				+ sin(-playerPos.r.y * (M_PI / 32768)) * horizontalMovement
				- cos(-playerPos.r.y * (M_PI / 32768)) * verticalMovement);
			CheckScrollLimits();
		}
	}

	if (mouseDown(MOUSE_ROTATE) && rotActive) {
		float mouseDeltaX = mouseX() - rotX;
		float mouseDeltaY = mouseY() - rotY;

    rotationHorizontalTracker->set_target_delta(static_cast<int>(DEG(-mouseDeltaX) / 4));
    rotationHorizontalTracker->update();
		playerPos.r.y = rotationHorizontalTracker->get_current();

		if (bInvertMouse) {
			mouseDeltaY *= -1;
		}
    rotationVerticalTracker->set_target_delta(static_cast<int>(DEG(mouseDeltaY) / 4));
    rotationVerticalTracker->update();
		playerPos.r.x = rotationVerticalTracker->get_current();
		playerPos.r.x = glm::clamp(playerPos.r.x, DEG(360 + MIN_PLAYER_X_ANGLE), DEG(360 + MAX_PLAYER_X_ANGLE));
	}

	if (!mouseDown(MOUSE_ROTATE) && rotActive) {
		rotActive = false;
		ignoreRMBC = true;
		pos.x = playerPos.r.x;
		pos.y = playerPos.r.y;
		pos.z = playerPos.r.z;
		camInformOfRotation(&pos);
		bRadarDragging = false;
	}

	draw3DScene();

	if (fadeEndTime)
	{
		if (graphicsTime < fadeEndTime)
		{
			fadeStartOfGame();
		}
		else
		{
			// ensure the fade only happens once (per call to transitionInit() & graphicsTime init) - i.e. at game start - regardless of graphicsTime wrap-around
			fadeEndTime = 0;
		}
	}
}

bool transitionInit()
{
	fadeEndTime = FADE_START_OF_GAME_TIME;
	return true;
}

static void fadeStartOfGame()
{
	PIELIGHT color = WZCOL_BLACK;
	float delta = (static_cast<float>(graphicsTime) / static_cast<float>(fadeEndTime) - 1.f);
	color.byte.a = static_cast<uint8_t>(std::min<uint32_t>(
		255, static_cast<uint32_t>(std::ceil(255.f * (1.f - (delta * delta * delta + 1.f)))))); // cubic easing
	pie_UniTransBoxFill(0, 0, pie_GetVideoBufferWidth(), pie_GetVideoBufferHeight(), color);
}

static bool mouseInBox(int x0, int y0, int x1, int y1)
{
	return mouseX() > x0 && mouseX() < x1 && mouseY() > y0 && mouseY() < y1;
}

bool DrawnInLastFrame(int32_t frame)
{
	return frame >= (int32_t)StartOfLastFrame;
}


/*
	Returns what the mouse was clicked on. Only called if there was a mouse pressed message
	on MOUSE_LMB. We aren't concerned here with setting selection flags - just what it
	actually was
*/
BaseObject * mouseTarget()
{
  BaseObject * psReturn = nullptr;
	int dispX, dispY, dispR;

	if (mouseTileX < 0 || mouseTileY < 0 || mouseTileX > mapWidth - 1 || mouseTileY > mapHeight - 1)
	{
		return (nullptr);
	}

	/* First have a look through the droid lists */
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		/* Note the !psObject check isn't really necessary as the goto will jump out */
		for (auto& psDroid : playerList[i].droids)
		{
			dispX = psDroid.getDisplayData()->screen_x;
			dispY = psDroid.getDisplayData()->screen_y;
			dispR = psDroid.getDisplayData()->screen_r;

			// Has the droid been drawn since the start of the last frame
			if (psDroid.isVisibleToSelectedPlayer() && DrawnInLastFrame(psDroid.getDisplayData()->frame_number)) {
				if (mouseInBox(dispX - dispR, dispY - dispR, dispX + dispR, dispY + dispR))
				{
					/* We HAVE clicked on droid! */
					psReturn = &psDroid;
					/* There's no point in checking other object types */
					return psReturn;
				}
			}
		}
	} // end of checking for droids

	/*	Not a droid, so maybe a structure or feature?
		If still NULL after this then nothing */
	psReturn = getTileOccupier(mouseTileX, mouseTileY);

	if (psReturn == nullptr)
	{
		psReturn = getTileBlueprintStructure(mouseTileX, mouseTileY);
	}

	/* Send the result back - if it's null then we clicked on an area of terrain */
	return psReturn;
}

// Start repositioning a delivery point.
//
static FlagPosition flagPos;
static int flagStructId;
static bool flagReposVarsValid;
static bool flagReposFinished;

void startDeliveryPosition(FlagPosition* psFlag)
{
	if (tryingToGetLocation()) // if we're placing a building don't place
	{
		return;
	}

	ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "Invalid player (selectedPlayer: %" PRIu32 ")", selectedPlayer);

	//clear the selected delivery point
	for (auto psFlagPos : apsFlagPosLists[selectedPlayer])
	{
		psFlagPos->selected = false;
	}

	//set this object position to be highlighted
	psFlag->selected = true;
	flagPos = *psFlag;

	Structure* psStruct = findDeliveryFactory(psFlag);
	if (!psStruct)
	{
		flagStructId = 0; // not a struct, just a flag.
	}
	else
	{
		flagStructId = psStruct->getId();
	}
	flagReposVarsValid = true;
	flagReposFinished = false;

	triggerEvent(TRIGGER_DELIVERY_POINT_MOVING, psStruct);
}

// Finished repositioning a delivery point.
//
void finishDeliveryPosition()
{
	Structure* psStruct = nullptr;

	ASSERT_OR_RETURN(, selectedPlayer < MAX_PLAYERS, "Invalid player (selectedPlayer: %" PRIu32 ")", selectedPlayer);

	if (flagStructId)
	{
		flagReposVarsValid = false;
		psStruct = IdToStruct(flagStructId, selectedPlayer);
		if (psStruct)
		{
			if (StructIsFactory(psStruct) &&
				  dynamic_cast<Factory*>(psStruct)->getAssemblyPoint()) {
				setAssemblyPoint(psStruct->pFunctionality->factory.psAssemblyPoint,
				                 flagPos.coords.x, flagPos.coords.y, selectedPlayer, true);
			}
			else if (psStruct->getStats()->type == STRUCTURE_TYPE::REPAIR_FACILITY) {
				setAssemblyPoint(psStruct->pFunctionality->repairFacility.psDeliveryPoint,
				                 flagPos.coords.x, flagPos.coords.y, selectedPlayer, true);
			}
		}
		//deselect once moved
		for (auto psFlagPos : apsFlagPosLists[selectedPlayer])
		{
			psFlagPos->selected = false;
		}
	}
	triggerEvent(TRIGGER_DELIVERY_POINT_MOVED, psStruct);
	flagReposFinished = true;
}

// Is there a valid delivery point repositioning going on.
bool deliveryReposValid()
{
	if (!flagReposVarsValid)
	{
		return false;
	}

	ASSERT_OR_RETURN(false, selectedPlayer < MAX_PLAYERS, "Invalid player (selectedPlayer: %" PRIu32 ")",
	                 selectedPlayer);

	Vector2i map = map_coord(flagPos.coords.xy());

	//make sure we are not too near map edge
	if (map.x < scrollMinX + TOO_NEAR_EDGE || map.x + 1 > scrollMaxX - TOO_NEAR_EDGE ||
		map.y < scrollMinY + TOO_NEAR_EDGE || map.y + 1 > scrollMaxY - TOO_NEAR_EDGE)
	{
		return false;
	}

	// cant place on top of a delivery point...
	for (auto const psCurrFlag : apsFlagPosLists[selectedPlayer])
	{
		Vector2i flagTile = map_coord(psCurrFlag->coords.xy());
		if (flagTile == map)
		{
			return false;
		}
	}

	if (fpathBlockingTile(map.x, map.y, PROPULSION_TYPE::WHEELED))
	{
		return false;
	}

	return true;
}

bool deliveryReposFinished(FlagPosition* psFlag)
{
	if (!flagReposVarsValid)
	{
		return false;
	}

	if (psFlag)
	{
		*psFlag = flagPos;
	}
	return flagReposFinished;
}

void processDeliveryRepos()
{
	if (!flagReposVarsValid)
	{
		return;
	}

	int bX = clip<int>(mouseTileX, 2, mapWidth - 3);
	int bY = clip<int>(mouseTileY, 2, mapHeight - 3);

	flagPos.coords = Vector3i(world_coord(Vector2i(bX, bY)) + Vector2i(TILE_UNITS / 2, TILE_UNITS / 2),
	                          map_TileHeight(bX, bY) + 2 * ASSEMBLY_POINT_Z_PADDING);
}

// Cancel repositioning of the delivery point without moving it.
//
void cancelDeliveryRepos()
{
	flagReposVarsValid = false;
}

void renderDeliveryRepos(const glm::mat4& viewMatrix)
{
	if (flagReposVarsValid) {
		::renderDeliveryPoint(&flagPos, true, viewMatrix);
	}
}

// check whether a clicked on droid is in a command group or assigned to a sensor
static bool droidHasLeader(Droid* psDroid)
{
  BaseObject * psLeader;

	if (psDroid->getType() == DROID_TYPE::COMMAND ||
			psDroid->getType() == DROID_TYPE::SENSOR)
	{
		return false;
	}

	if (hasCommander(psDroid))
	{
		psLeader = (BaseObject *)psDroid->group->psCommander;
	}
	else
	{
		//psLeader can be either a droid or a structure
		psLeader = orderStateObj(psDroid, ORDER_TYPE::FIRE_SUPPORT);
	}

	if (psLeader != nullptr)
	{
		if (auto droid = dynamic_cast<Droid*>(psLeader))
		{
			SelectDroid(droid);
		}
		assignSensorTarget(psLeader);
		return true;
	}

	return false;
}


// deal with selecting a droid
void dealWithDroidSelect(Droid* psDroid, bool bDragBox)
{
	/*	Toggle selection on and off - allows you drag around a big
		area of droids and then exclude certain individuals */
	if (!bDragBox && psDroid->damageManager->isSelected()) {
		DeSelectDroid(psDroid);
	}
	else if (ctrlShiftDown() || !droidHasLeader(psDroid)) {
		if (specialOrderKeyDown()) {
			/* We only want to select weapon units if ALT is down on a drag */
			if (psDroid->asWeaps[0].nStat > 0) {
				SelectDroid(psDroid);
			}
		}
		else {
			SelectDroid(psDroid);
		}
	}
}

static void FeedbackOrderGiven()
{
	static unsigned LastFrame = 0;
	unsigned ThisFrame = frameGetFrameNumber();

	// Ensure only played once per game cycle.
	if (ThisFrame != LastFrame)
	{
		audio_PlayTrack(ID_SOUND_SELECT);
		LastFrame = ThisFrame;
	}
}

// check whether the queue order keys are pressed
bool ctrlShiftDown()
{
	return keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL) || keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT);
}

void AddDerrickBurningMessage()
{
	if (addConsoleMessageDebounced(
		_("Cannot Build. Oil Resource Burning."), CONSOLE_TEXT_JUSTIFICATION::DEFAULT, SYSTEM_MESSAGE, CANNOT_BUILD_BURNING))
	{
		audio_PlayBuildFailedOnce();
	}
}

static void printDroidClickInfo(Droid* psDroid)
{
	const DebugInputManager& dbgInputManager = gInputManager.debugManager();
	if (dbgInputManager.debugMappingsAllowed()) // cheating on, so output debug info
	{
		console(
            "%s - Hitpoints %d/%d - ID %d - experience %f, %s - order %s - action %s - sensor range %hu - ECM %u - pitch %.0f - frust %u - kills %d",
            droidGetName(psDroid), psDroid->damageManager->getHp(), psDroid->damageManager->getOriginalHp(), psDroid->getId(),
            psDroid->getExperience() / 65536.f, getDroidLevelName(psDroid).c_str(), getDroidOrderName(psDroid->getOrder()->type).c_str(),
            actionToString(psDroid->getAction()).c_str(),
            droidSensorRange(psDroid), objJammerPower(psDroid), UNDEG(psDroid->getRotation().pitch), psDroid->getLastFrustratedTime(),
            psDroid->getKills());
		FeedbackOrderGiven();
	}
	else if (!psDroid->damageManager->isSelected())
	{
		console(_("%s - Hitpoints %d/%d - Experience %.1f, %s, Kills %d"), droidGetName(psDroid), psDroid->damageManager->getHp(),
		        psDroid->damageManager->getOriginalHp(),
		        psDroid->getExperience() / 65536.f, getDroidLevelName(psDroid).c_str(), psDroid->getKills());
		FeedbackOrderGiven();
	}
	clearSelection();
	dealWithDroidSelect(psDroid, false);
}

static void dealWithLMBDroid(Droid* psDroid, SELECTION_TYPE selection)
{
	bool ownDroid; // Not an allied droid

	if (selectedPlayer >= MAX_PLAYERS) {
		return; // no-op
	}

	if (!aiCheckAlliances(selectedPlayer, psDroid->playerManager->getPlayer())) {
		memset(DROIDDOING, 0x0, sizeof(DROIDDOING)); // take over the other players droid by debug menu.
		/* We've clicked on enemy droid */
		const DebugInputManager& dbgInputManager = gInputManager.debugManager();
		if (dbgInputManager.debugMappingsAllowed())
		{
			console(_(
				"(Enemy!) %s - Hitpoints %d/%d - ID %d - experience %f, %s - order %s - action %s - sensor range %d - ECM %d - pitch %.0f"),
              droidGetName(psDroid), psDroid->damageManager->getHp(), psDroid->damageManager->getOriginalHp(), psDroid->getId(),
			        psDroid->getExperience() / 65536.f, getDroidLevelName(psDroid).c_str(), getDroidOrderName(psDroid->getOrder()->type).c_str(),
              actionToString(psDroid->getAction()).c_str(), droidSensorRange(psDroid), objJammerPower(psDroid),
              UNDEG(psDroid->getRotation().pitch));
			FeedbackOrderGiven();
		}
		orderSelectedObjAdd(selectedPlayer, (BaseObject *)psDroid, ctrlShiftDown());

		//lasSat structure can select a target - in multiPlayer only
		if (bMultiPlayer && bLasSatStruct)
		{
			orderStructureObj(selectedPlayer, (BaseObject *)psDroid);
		}

		FeedbackOrderGiven();
		return;
	}

	ownDroid = psDroid->playerManager->isSelectedPlayer();
	// Hack to detect if sensor was assigned
	bSensorAssigned = true;
	if (!bRightClickOrders && ctrlShiftDown() && ownDroid)
	{
		// select/deselect etc. the droid
		dealWithDroidSelect(psDroid, false);
	}
	else if (specialOrderKeyDown() && ownDroid)
	{
		// try to attack your own unit
		orderSelectedObjAdd(selectedPlayer, (BaseObject *)psDroid, ctrlShiftDown());
		FeedbackOrderGiven();
	}
	else if (isTransporter(*psDroid))
	{
		if (selection == SC_INVALID)
		{
			//in multiPlayer mode we RMB to get the interface up
			if (bMultiPlayer && !bRightClickOrders)
			{
				psDroid->damageManager->setSelected(true);
				triggerEventSelected();
			}
			else
			{
				intResetScreen(false);
				if (!getWidgetsStatus())
				{
					setWidgetsStatus(true);
				}
				addTransporterInterface(psDroid, false);
			}
		}
		else
		{
			// We can order all units to use the transport now
			if (cyborgDroidSelected(selectedPlayer))
			{
				// TODO add special processing for cyborgDroids
			}
			orderSelectedObj(selectedPlayer, (BaseObject *)psDroid);
			FeedbackOrderGiven();
		}
	}
	// Clicked on a commander? Will link to it.
	else if (psDroid->getType() == DROID_TYPE::COMMAND && selection != SC_INVALID &&
		selection != SC_DROID_COMMAND &&
		selection != SC_DROID_CONSTRUCT &&
					 !ctrlShiftDown() && ownDroid)
	{
		turnOffMultiMsg(true);
		orderSelectedObj(selectedPlayer, (BaseObject *)psDroid);
		FeedbackOrderGiven();
		clearSelection();
		assignSensorTarget((BaseObject *)psDroid);
		dealWithDroidSelect(psDroid, false);
		turnOffMultiMsg(false);
	}
	// Clicked on a sensor? Will assign to it.
	else if (psDroid->getType() == DROID_TYPE::SENSOR)
	{
		bSensorAssigned = false;
		for (auto& psCurr : playerList[selectedPlayer].droids)
		{
			//must be indirect weapon droid or VTOL weapon droid
			if (psCurr.getType() == DROID_TYPE::WEAPON &&
          psCurr.damageManager->isSelected() &&
					(!proj_Direct(psCurr.weaponManager->weapons[0].stats.get()) ||
           psCurr.isVtol()) &&
					droidSensorDroidWeapon((BaseObject *)psDroid, &psCurr)) {
				bSensorAssigned = true;
				orderDroidObj(&psCurr, ORDER_TYPE::FIRE_SUPPORT, (BaseObject *)psDroid, ModeQueue);
				FeedbackOrderGiven();
			}
		}
		if (bSensorAssigned) {
			clearSelection();
			assignSensorTarget((BaseObject *)psDroid);
		}
	}
	// Hack to detect if anything was done with sensor
	else
	{
		bSensorAssigned = false;
	}
	if (bSensorAssigned)
	{
		return;
	}
	// Clicked on a construction unit? Will guard it.
	else if ((psDroid->getType() == DROID_TYPE::CONSTRUCT || psDroid->getType() == DROID_TYPE::SENSOR ||
						psDroid->getType() == DROID_TYPE::COMMAND) && selection == SC_DROID_DIRECT) {
		orderSelectedObj(selectedPlayer, psDroid);
		FeedbackOrderGiven();
	}
	// Clicked on a damaged unit? Will repair it.
	else if (droidIsDamaged(psDroid) && repairDroidSelected(selectedPlayer))
	{
		assignDestTarget();
		orderSelectedObjAdd(selectedPlayer, psDroid, ctrlShiftDown());
		FeedbackOrderGiven();
	}
	else if (bRightClickOrders && ownDroid) {
		if (!(psDroid->damageManager->isSelected())) {
			clearSelection();
			SelectDroid(psDroid);
		}
		intObjectSelected((BaseObject *)psDroid);
	}
	// Just plain clicked on?
	else if (ownDroid) {
		printDroidClickInfo(psDroid);
	}
	else // Clicked on allied unit with no other possible actions
	{
		console(_("%s - Allied - Hitpoints %d/%d - Experience %d, %s"),
            droidGetName(psDroid), psDroid->damageManager->getHp(),
		        psDroid->damageManager->getOriginalHp(),
		        psDroid->getExperience() / 65536, getDroidLevelName(psDroid).c_str());
		FeedbackOrderGiven();
	}
}

static void dealWithLMBStructure(Structure* psStructure, SELECTION_TYPE selection)
{
	bool ownStruct = psStructure->playerManager->isSelectedPlayer();

	if (selectedPlayer < MAX_PLAYERS &&
      !aiCheckAlliances(psStructure->playerManager->getPlayer(), selectedPlayer)) {
		/* We've clicked on an enemy building */
		const DebugInputManager& dbgInputManager = gInputManager.debugManager();
		if (dbgInputManager.debugMappingsAllowed())
		{
			// TRANSLATORS: "ref" is an internal unique id of the item (can leave untranslated as a technical term)
			console(_("(Enemy!) %s, ref: %d, ID: %d Hitpoints: %d/%d"), getID(psStructure->getStats()),
			        psStructure->getStats()->ref,
			        psStructure->getId(), psStructure->damageManager->getHp(),
			        psStructure->getStats()->upgraded_stats[psStructure->playerManager->getPlayer()].hitPoints);
		}
		orderSelectedObjAdd(selectedPlayer, psStructure, ctrlShiftDown());
		//lasSat structure can select a target - in multiPlayer only
		if (bMultiPlayer && bLasSatStruct) {
			orderStructureObj(selectedPlayer, psStructure);
		}
		FeedbackOrderGiven();
		return;
	}

	/* We've clicked on allied or own building */

	//print some info at the top of the screen for the specific structure
	if (!bRightClickOrders)
	{
		printStructureInfo(psStructure);
	}

	if (selectedPlayer >= MAX_PLAYERS)
	{
		return; // do not proceed
	}

	/* Got to be built. Also, you can't 'select' derricks */
	if (!specialOrderKeyDown() && psStructure->getState() == STRUCTURE_STATE::BUILT &&
      !psStructure->testFlag(static_cast<size_t>(OBJECT_FLAG::UNSELECTABLE)) &&
		  psStructure->getStats()->type != STRUCTURE_TYPE::RESOURCE_EXTRACTOR && ownStruct) {
		if (bRightClickOrders) {
			if (StructIsFactory(psStructure) && selection != SC_DROID_CONSTRUCT) {
				intAddFactoryOrder(psStructure);
			}
		}
		else {
			auto shouldDisplayInterface = !anyDroidSelected(selectedPlayer);
			if (selection == SC_INVALID) {
				/* Clear old building selection(s) - should only be one */
				for (auto& psCurr : playerList[selectedPlayer].structures)
				{
					psCurr.damageManager->setSelected(false);
				}
				/* Establish new one */
				psStructure->damageManager->setSelected(true);
				triggerEventSelected();
				jsDebugSelected(psStructure);
			}
			//determine if LasSat structure has been selected
			bLasSatStruct = lasSatStructSelected(psStructure);

			if (shouldDisplayInterface) {
				intObjectSelected(psStructure);
				FeedbackOrderGiven();
			}
		}
	}
	else if ((psStructure->getState() == STRUCTURE_STATE::BUILT) && 
           !psStructure->testFlag(static_cast<size_t>(OBJECT_FLAG::UNSELECTABLE)) &&
		       (psStructure->getStats()->type == STRUCTURE_TYPE::RESOURCE_EXTRACTOR) &&
		       selection == SC_INVALID && ownStruct) {
		/* Clear old building selection(s) - should only be one */
		for (auto& psCurr : playerList[selectedPlayer].structures)
		{
			psCurr.damageManager->setSelected(false);
		}
		/* Establish new one */
		psStructure->damageManager->setSelected(true);
		triggerEventSelected();
		jsDebugSelected(psStructure);
	}
	bSensorAssigned = false;
	orderSelectedObjAdd(selectedPlayer, psStructure, ctrlShiftDown());
	FeedbackOrderGiven();
	if (bSensorAssigned)
	{
		clearSelection();
		assignSensorTarget(psStructure);
	}
	if (intDemolishSelectMode())
	{
		// we were demolishing something - now we're done
		if (ctrlShiftDown())
		{
			quickQueueMode = true;
		}
		else
		{
			intDemolishCancel();
		}
	}
}

static void dealWithLMBFeature(Feature* psFeature)
{
	if (selectedPlayer >= MAX_PLAYERS)
	{
		goto debugOuput;
	}

	//go on to check for
	if (psFeature->getStats()->damageable)
	{
		orderSelectedObjAdd(selectedPlayer, (BaseObject *)psFeature, ctrlShiftDown());
		//lasSat structure can select a target - in multiPlayer only
		if (bMultiPlayer && bLasSatStruct)
		{
			orderStructureObj(selectedPlayer, (BaseObject *)psFeature);
		}
		FeedbackOrderGiven();
	}

	//clicking an oil field should start a build..
	if (psFeature->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE) {
		unsigned i;
		// find any construction droids. and order them to build an oil resource.

		// first find the derrick.
		for (i = 0; (i < numStructureStats) &&
                (asStructureStats[i].type != STRUCTURE_TYPE::RESOURCE_EXTRACTOR); ++i)
		{
		}

		if ((i < numStructureStats) &&
			(apStructTypeLists[selectedPlayer][i] == AVAILABLE)) // don't go any further if no derrick stat found.
		{
			// for each droid
			for (auto& psCurr : playerList[selectedPlayer].droids)
			{
				if ((psCurr.getType() == DROID_TYPE::CONSTRUCT ||
             droidType(&psCurr) == DROID_TYPE::CYBORG_CONSTRUCT) &&
             psCurr.damageManager->isSelected()) {
					if (fireOnLocation(psFeature->getPosition().x, psFeature->getPosition().y)) {
						// Can't build because it's burning
						AddDerrickBurningMessage();
					}

					sendDroidInfo(
									&psCurr, Order(ORDER_TYPE::BUILD, &asStructureStats[i], psFeature->getPosition().xy(), playerPos.r.y),
									ctrlShiftDown());
					FeedbackOrderGiven();
				}
			}
		}
	}
	else
	{
		switch (psFeature->getStats()->subType)
		{
		case FEATURE_TYPE::GEN_ARTE:
		case FEATURE_TYPE::OIL_DRUM:
			{
				Droid* psNearestUnit = getNearestDroid(mouseTileX * TILE_UNITS + TILE_UNITS / 2,
				                                       mouseTileY * TILE_UNITS + TILE_UNITS / 2, true);
				/* If so then find the nearest unit! */
				if (psNearestUnit) // bloody well should be!!!
				{
					sendDroidInfo(psNearestUnit, Order(ORDER_TYPE::RECOVER, *psFeature), ctrlShiftDown());
					FeedbackOrderGiven();
				}
				break;
			}
		case FEATURE_TYPE::BOULDER:
		case FEATURE_TYPE::OIL_RESOURCE:
		case FEATURE_TYPE::VEHICLE:
		default:
			break;
		}
	}

debugOuput:
	const DebugInputManager& dbgInputManager = gInputManager.debugManager();
	if (dbgInputManager.debugMappingsAllowed())
	{
		console("(Feature) %s ID: %d ref: %d Hitpoints: %d/%d", getID(psFeature->getStats()), psFeature->getId(),
		        psFeature->getStats()->ref, psFeature->getStats()->body, psFeature->damageManager->getHp());
	}
}

static void dealWithLMBObject(BaseObject* psClickedOn)
{
	SELECTION_TYPE selection = establishSelection(selectedPlayer);

	switch (getObjectType(psClickedOn)) {
    case OBJECT_TYPE::DROID:
		dealWithLMBDroid(dynamic_cast<Droid*>(psClickedOn), selection);
		break;
    case OBJECT_TYPE::STRUCTURE:
		dealWithLMBStructure(dynamic_cast<Structure*>(psClickedOn), selection);
		break;
    case OBJECT_TYPE::FEATURE:
		dealWithLMBFeature(dynamic_cast<Feature*>(psClickedOn));
		break;
	default:
		break;
	}
}

void dealWithLMB()
{
  BaseObject * psClickedOn;
	Structure* psStructure;

	/* Don't process if in game options are on screen */
	if (mouseOverRadar || InGameOpUp || widgGetFromID(psWScreen, INTINGAMEOP)) {
		return;
	}

	/* What have we clicked on? */
	psClickedOn = mouseTarget();
	if (psClickedOn)
	{
		dealWithLMBObject(psClickedOn);

		return;
	}

	if (selectedPlayer >= MAX_PLAYERS)
	{
		return; // shortcut the rest (for now)
	}

	if (auto deliveryPoint = findMouseDeliveryPoint())
	{
		if (selNumSelected(selectedPlayer) == 0)
		{
			if (bRightClickOrders)
			{
				//centre the view on the owning Factory
				psStructure = findDeliveryFactory(deliveryPoint);
				if (psStructure)
				{
					setViewPos(map_coord(psStructure->getPosition().x), map_coord(psStructure->getPosition().y), true);
				}
			}
			else
			{
				startDeliveryPosition(deliveryPoint);
			}
			return;
		}
	}

	// now changed to use the multiple order stuff
	// clicked on a destination.
	orderSelectedLoc(selectedPlayer, mousePos.x, mousePos.y, ctrlShiftDown());
	// ctrlShiftDown() = ctrl clicked a destination, add an order
	/* Otherwise send them all */
	if (getNumDroidsSelected())
	{
		assignDestTarget();
		audio_PlayTrack(ID_SOUND_SELECT);
	}

	const DebugInputManager& dbgInputManager = gInputManager.debugManager();
	if (dbgInputManager.debugMappingsAllowed() && tileOnMap(mouseTileX, mouseTileY))
	{
		Tile* psTile = mapTile(mouseTileX, mouseTileY);
		uint8_t aux = auxTile(mouseTileX, mouseTileY, selectedPlayer);

		console("%s tile %d, %d [%d, %d] continent(l%d, h%d) level %g illum %d %s %s w=%d s=%d j=%d",
		        tileIsExplored(psTile) ? "Explored" : "Unexplored",
		        mouseTileX, mouseTileY, world_coord(mouseTileX), world_coord(mouseTileY),
		        (int)psTile->limitedContinent, (int)psTile->hoverContinent, psTile->level, (int)psTile->illumination,
		        aux & AUXBITS_DANGER ? "danger" : "", aux & AUXBITS_THREAT ? "threat" : "",
		        (int)psTile->watchers[selectedPlayer], (int)psTile->sensors[selectedPlayer],
		        (int)psTile->jammers[selectedPlayer]);
	}
}

bool getRotActive()
{
	return (rotActive);
}

// process LMB double clicks
static void dealWithLMBDClick()
{
	/* What have we clicked on? */
	auto psClickedOn = mouseTarget();
	/* If not NULL, then it's a droid or a structure */
  if (psClickedOn == nullptr) {
    return;
  }
  /* We've got a droid or a structure */
  if (auto psDroid = dynamic_cast<Droid*>(psClickedOn)) {
    /* We clicked on droid */
    if (psDroid->playerManager->isSelectedPlayer()) {
      // Now selects all of same type on screen
      selDroidSelection(selectedPlayer, SELECTION_CLASS::DS_BY_TYPE, SELECTIONTYPE::DST_ALL_SAME, true);
    }
  }
  else if (auto psStructure = dynamic_cast<Structure*>(psClickedOn)) {
    /* We clicked on structure */
    if (psStructure->playerManager->isSelectedPlayer() && !structureIsBlueprint(psStructure)) {
      if (auto factory = dynamic_cast<Factory*>(psStructure)) {
        setViewPos(map_coord(factory->getAssemblyPoint()->coords.x),
                   map_coord(factory->getAssemblyPoint()->coords.y),
                   true);
      }
      else if (auto repair = dynamic_cast<RepairFacility*>(psStructure)) {
        setViewPos(map_coord(repair->getDeliveryPoint()->coords.x),
                   map_coord(repair->getDeliveryPoint()->coords.y),
                   true);
      }
    }
  }
}

/**
 * Find a delivery point, owned by `selectedPlayer`, pointed by the mouse.
 */
static FlagPosition* findMouseDeliveryPoint()
{
	if (selectedPlayer >= MAX_PLAYERS)
	{
		return nullptr;
	}

	for (auto psPoint : apsFlagPosLists[selectedPlayer])
	{
		if (psPoint->type != POSITION_TYPE::POS_DELIVERY)
		{
			continue;
		}

		auto dispX = psPoint->screenX;
		auto dispY = psPoint->screenY;
		auto dispR = psPoint->screenR;
		if (DrawnInLastFrame(psPoint->frameNumber) == true) // Only check DP's that are on screen
		{
			if (mouseInBox(dispX - dispR, dispY - dispR, dispX + dispR, dispY + dispR))
			{
				// We HAVE clicked on DP!
				return psPoint;
			}
		}
	}

	return nullptr;
}

static void dealWithRMB()
{
  BaseObject* psClickedOn;

	if (mouseOverRadar || InGameOpUp || widgGetFromID(psWScreen, INTINGAMEOP))
	{
		return;
	}

	/* What have we clicked on? */
	psClickedOn = mouseTarget();
	/* If not NULL, then it's a droid or a structure */
	if (psClickedOn != nullptr)
	{
		/* We've got a droid or a structure */
		if (auto psDroid = dynamic_cast<Droid*>(psClickedOn))
		{
			/* We clicked on droid */
			if (psDroid->playerManager->isSelectedPlayer()) {
				if (bRightClickOrders && ctrlShiftDown())
				{
					dealWithDroidSelect(psDroid, false);
				}
				// Not a transporter
				else if (!isTransporter(*psDroid))
				{
					if (bRightClickOrders)
					{
						/* We've clicked on one of our own droids */
						printDroidClickInfo(psDroid);
					}
					else
					{
						if (!(psDroid->damageManager->isSelected()))
						{
							clearSelection();
							SelectDroid(psDroid);
						}
						intObjectSelected((BaseObject *)psDroid);
					}
				}
				// Transporter
				else
				{
					if (bMultiPlayer)
					{
						if (bRightClickOrders && !psDroid->damageManager->isSelected())
						{
							clearSelection();
							SelectDroid(psDroid);
						}
						else
						{
							intResetScreen(false);
							if (!getWidgetsStatus())
							{
								setWidgetsStatus(true);
							}
							addTransporterInterface(psDroid, false);
						}
					}
				}
			}
			else if (bMultiPlayer && isHumanPlayer(psDroid->playerManager->getPlayer())) {
				console("%s", droidGetName(psDroid));
				FeedbackOrderGiven();
			}
		} // end if its a droid
		else if (auto psStructure = dynamic_cast<Structure*>(psClickedOn)) {
			/* We clicked on structure */
			if (psStructure->playerManager->isSelectedPlayer()) {
				/* We've clicked on our own building */
				if (bRightClickOrders && intDemolishSelectMode()) {
					orderSelectedObjAdd(selectedPlayer, psClickedOn, ctrlShiftDown());
					FeedbackOrderGiven();
					// we were demolishing something - now we're done
					if (ctrlShiftDown()) {
						quickQueueMode = true;
					}
					else {
						intDemolishCancel();
					}
				}
				else if (psStructure->damageManager->isSelected()) {
					psStructure->damageManager->setSelected(false);
					intObjectSelected(nullptr);
					triggerEventSelected();
					jsDebugSelected(psStructure);
				}
				else if (!structureIsBlueprint(psStructure)) {
					clearSelection();

					if (bRightClickOrders) {
						if ((psStructure->getState() == STRUCTURE_STATE::BUILT) &&
							(psStructure->getStats()->type != STRUCTURE_TYPE::RESOURCE_EXTRACTOR)) {
							printStructureInfo(psStructure);

							psStructure->damageManager->setSelected(true);
							jsDebugSelected(psStructure);

							// Open structure menu
							intObjectSelected((BaseObject *)psStructure);
							FeedbackOrderGiven();

							bLasSatStruct = lasSatStructSelected(psStructure);
							triggerEventSelected();
						}
					}
					else if (StructIsFactory(psStructure))
					{
						//pop up the order interface for the factory
						intAddFactoryOrder(psStructure);
					}
					else
					{
						intObjectSelected((BaseObject *)psStructure);
					}
				}
			}
		} // end if its a structure
		else
		{
			/* And if it's not a feature, then we're in trouble! */
			ASSERT(getObjectType(psClickedOn) == OBJECT_TYPE::FEATURE,
             "Weird selection from RMB - type of clicked object is %d",
			       (int)OBJECT_TYPE::FEATURE);
		}
	}
	else
	{
		if (auto deliveryPoint = findMouseDeliveryPoint())
		{
			if (bRightClickOrders)
			{
				startDeliveryPosition(deliveryPoint);
			}
			else
			{
				//centre the view on the owning Factory
				auto psStructure = findDeliveryFactory(deliveryPoint);
				if (psStructure)
				{
					setViewPos(map_coord(psStructure->getPosition().x), map_coord(psStructure->getPosition().y), true);
				}
			}
		}
		else
		{
			handleDeselectionClick();
		}
	}
}

/* if there is a valid object under the mouse this routine returns not only the type of the object in the
return code, but also a pointer to the SimpleObject) ... well if your going to be "object orientated" you might as well do it right
- it sets it to null if we don't find anything
*/
static MOUSE_TARGET itemUnderMouse(BaseObject * * ppObjectUnderMouse)
{
	unsigned i;
	MOUSE_TARGET retVal;
  BaseObject * psNotDroid;
	unsigned dispX, dispY, dispR;
	Structure* psStructure;

	*ppObjectUnderMouse = nullptr;

	if (mouseTileX < 0 || mouseTileY < 0 || mouseTileX > (int)(mapWidth - 1) || mouseTileY > (int)(mapHeight - 1)) {
		retVal = MT_BLOCKING;
		return retVal;
	}

	/* We haven't found anything yet */
	retVal = MT_NOTARGET;

	/* First have a look through the droid lists */
	for (i = 0; i < MAX_PLAYERS; i++)
	{
		/* Note the !psObject check isn't really necessary as the goto will jump out */
		for (auto& psDroid : playerList[i].droids)
		{
      if ((retVal) != MT_NOTARGET) break;
			dispX = psDroid.getDisplayData()->screen_x;
			dispY = psDroid.getDisplayData()->screen_y;
			dispR = psDroid.getDisplayData()->screen_r;
			/* Only check droids that're on screen */
			if (psDroid.getDisplayData()->frame_number + 1 == currentFrame && psDroid.isVisibleToSelectedPlayer()) {
				if (mouseInBox(dispX - dispR, dispY - dispR, dispX + dispR, dispY + dispR)) {
					/* We HAVE clicked on droid! */
					if (selectedPlayer < MAX_PLAYERS && aiCheckAlliances(psDroid.playerManager->getPlayer(), selectedPlayer)) {
						*ppObjectUnderMouse = &psDroid;
						// need to check for command droids here as well
						if (psDroid.getType() == DROID_TYPE::SENSOR) {
							if (!psDroid.playerManager->isSelectedPlayer()) {
								retVal = MT_CONSTRUCT; // Can't assign to allied units
							}
							else {
								retVal = MT_SENSOR;
							}
						}
						else if (isTransporter(psDroid) && psDroid.playerManager->isSelectedPlayer()) {
							//check the transporter is not full
							if (calcRemainingCapacity(&psDroid)) {
								retVal = MT_TRANDROID;
							}
							else {
								retVal = MT_BLOCKING;
							}
						}
						else if (psDroid.getType() == DROID_TYPE::CONSTRUCT ||
										 psDroid.getType() == DROID_TYPE::CYBORG_CONSTRUCT)
						{
							return MT_CONSTRUCT;
						}
						else if (psDroid.getType() == DROID_TYPE::COMMAND)
						{
							if (!psDroid.playerManager->isSelectedPlayer()) {
								retVal = MT_CONSTRUCT; // Can't assign to allied units
							}
							else
							{
								retVal = MT_COMMAND;
							}
						}
						else
						{
							if (droidIsDamaged(&psDroid))
							{
								retVal = MT_OWNDROIDDAM;
							}
							else
							{
								retVal = MT_OWNDROID;
							}
						}
					}
					else
					{
						*ppObjectUnderMouse = &psDroid;
						retVal = MT_ENEMYDROID;
					}
					/* There's no point in checking other object types */
					return (retVal);
				}
			}
		}
	} // end of checking for droids

	/*	Not a droid, so maybe a structure or feature?
		If still NULL after this then nothing */
	psNotDroid = getTileOccupier(mouseTileX, mouseTileY);
	if (psNotDroid == nullptr)
	{
		psNotDroid = getTileBlueprintStructure(mouseTileX, mouseTileY);
	}

	if (psNotDroid != nullptr) {
		*ppObjectUnderMouse = (BaseObject *)psNotDroid;

		if (auto psFeat = dynamic_cast<Feature*>(psNotDroid)) {
			if (psFeat->getStats()->subType == FEATURE_TYPE::GEN_ARTE ||
          psFeat->getStats()->subType == FEATURE_TYPE::OIL_DRUM) {
				retVal = MT_ARTIFACT;
			}
			else if (psFeat->getStats()->damageable)
			//make damageable features return 'target' mouse pointer
			{
				retVal = MT_DAMFEATURE;
			}
			else if (psFeat->getStats()->subType == FEATURE_TYPE::OIL_RESOURCE)
			{
				retVal = MT_RESOURCE;
			}
			else
			{
				retVal = MT_BLOCKING;
			}
		}
		else if (auto psStructure = dynamic_cast<Structure*>(psNotDroid)) {
			if (selectedPlayer < MAX_PLAYERS && aiCheckAlliances(psNotDroid->playerManager->getPlayer(), selectedPlayer)) {
				if (psStructure->getState() == STRUCTURE_STATE::BEING_BUILT || isBlueprint(psStructure)) {
					retVal = MT_OWNSTRINCOMP;
				}
				// repair center.
				else if (psStructure->getStats()->type == STRUCTURE_TYPE::REPAIR_FACILITY) {
					if (buildingDamaged(psStructure)) {
						retVal = MT_REPAIRDAM;
					}
					else {
						retVal = MT_REPAIR;
					}
				}
				//sensor tower
				else if ((psStructure->getStats()->sensor_stats) &&
                 (psStructure->getStats()->sensor_stats->location == LOC::TURRET)) {
					if (buildingDamaged(psStructure)) {
						retVal = MT_SENSORSTRUCTDAM;
					}
					else {
						retVal = MT_SENSORSTRUCT;
					}
				}

				// standard buildings. - check for buildingDamaged BEFORE upgrades
				else if (buildingDamaged(psStructure))
				{
					retVal = MT_OWNSTRDAM;
				}
				// If this building is a factory/power generator/research facility
				// which isn't upgraded. Make the build icon available.
				else if (nextModuleToBuild(psStructure, -1) > 0)
				{
					retVal = MT_OWNSTRINCOMP;
				}
				else
				{
					/* All the different stages of construction */
					retVal = MT_OWNSTROK;
				}
			}
			else
			{
				retVal = MT_ENEMYSTR; // enemy structure
			}
		}
	}

	/* Send the result back - if it's null then we clicked on an area of terrain */
	/* make unseen objects just look like terrain. */
	if (retVal == MT_NOTARGET || psNotDroid && !psNotDroid->isVisibleToSelectedPlayer())
	{
		retVal = MT_TERRAIN;
	}
	return (retVal);
}

// Indicates the priority given to any given droid
// type in a multiple droid selection, the larger the
// number, the lower the priority. The order of entries
// corresponds to the order of droid types in the DROID_TYPE
// enum in DroidDef.h
//
#define NUM_DROID_WEIGHTS (14)
static UBYTE DroidSelectionWeights[NUM_DROID_WEIGHTS] =
{
	3, //DROID_WEAPON,
	1, //DROID_SENSOR,
	2, //DROID_ECM,
	4, //DROID_CONSTRUCT,
	3, //DROID_PERSON,
	3, //DROID_CYBORG,
	9, //DROID_TRANSPORTER,
	0, //DROID_COMMAND,
	4, //DROID_REPAIR,
	5, //DROID_DEFAULT,
	4, //DROID_CYBORG_CONSTRUCT,
	4, //DROID_CYBORG_REPAIR,
	3, //DROID_CYBORG_SUPER,
	10, //DROID_SUPERTRANSPORTER
};

/* Only deals with one type of droid being selected!!!! */
/*	We'll have to make it assess which selection is to be dominant in the case
	of multiple selections */
static SELECTION_TYPE establishSelection(unsigned _selectedPlayer)
{
	Droid* psDominant = nullptr;
	UBYTE CurrWeight = UBYTE_MAX;
	SELECTION_TYPE selectionClass = SC_INVALID;

	if (_selectedPlayer >= MAX_PLAYERS)
	{
		return SC_INVALID;
	}

	for (auto& psDroid : playerList[_selectedPlayer].droids)
	{
		// This works, uses the DroidSelectionWeights[] table to priorities the different
		// droid types and find the dominant selection.
		if (psDroid.damageManager->isSelected()) {
			ASSERT_OR_RETURN(SC_INVALID, (int)psDroid.getType() < NUM_DROID_WEIGHTS, "droidType exceeds NUM_DROID_WEIGHTS");
			if (DroidSelectionWeights[(int)psDroid.getType()] < CurrWeight)
			{
				CurrWeight = DroidSelectionWeights[(int)psDroid.getType()];
				psDominant = &psDroid;
			}
		}
	}

	if (psDominant)
	{
		psDominantSelected = psDominant;
		switch (psDominant->getType()) {
      using enum DROID_TYPE;
		case WEAPON:
			if (proj_Direct(psDominant->weaponManager->weapons[0].stats.get())) {
				selectionClass = SC_DROID_DIRECT;
			}
			else {
				selectionClass = SC_DROID_INDIRECT;
			}
			break;

		case PERSON:
			selectionClass = SC_DROID_DIRECT;
			break;
		case CYBORG:
		case CYBORG_SUPER:
			selectionClass = SC_DROID_DIRECT;
			break;
		case TRANSPORTER:
		case SUPER_TRANSPORTER:
			//can remove this is NEVER going to select the Transporter to move
			//Never say Never!! cos here we go in multiPlayer!!
			selectionClass = SC_DROID_TRANSPORTER;
			break;
		case SENSOR:
			selectionClass = SC_DROID_SENSOR;
			break;

		case ECM:
			selectionClass = SC_DROID_ECM;
			break;

		case CONSTRUCT:
		case CYBORG_CONSTRUCT:
			if (intDemolishSelectMode())
			{
				return SC_DROID_DEMOLISH;
			}
			selectionClass = SC_DROID_CONSTRUCT;
			break;

		case COMMAND:
			selectionClass = SC_DROID_COMMAND;
			break;

		case REPAIRER:
		case CYBORG_REPAIR:
			selectionClass = SC_DROID_REPAIR;
			break;

		default:
			ASSERT(!"unknown droid type", "Weirdy droid type on what you've clicked on!!!");
			break;
		}
	}
	return (selectionClass);
}

/* Just returns true if the building's present body points aren't 100 percent */
static bool buildingDamaged(Structure* psStructure)
{
	return psStructure->damageManager->getHp() < structureBody(psStructure);
}

/*Looks through the list of selected players droids to see if one is a repair droid*/
bool repairDroidSelected(unsigned player)
{
	ASSERT_OR_RETURN(false, player < MAX_PLAYERS, "Invalid player (%" PRIu32 ")", player);

	for (auto& psCurr : playerList[player].droids)
	{
		if (psCurr.damageManager->isSelected() && (
						psCurr.getType() == DROID_TYPE::REPAIRER ||
						psCurr.getType() == DROID_TYPE::CYBORG_REPAIR)) {
			return true;
		}
	}

	//didn't find one...
	return false;
}

/*Looks through the list of selected players droids to see if one is a VTOL droid*/
bool vtolDroidSelected(unsigned player)
{
	ASSERT_OR_RETURN(false, player < MAX_PLAYERS, "player: %" PRIu32 "", player);

	for (auto& psCurr : playerList[player].droids)
	{
		if (psCurr.damageManager->isSelected() && psCurr.isVtol())
		{
			// horrible hack to note one of the selected vtols
			psSelectedVtol = &psCurr;
			return true;
		}
	}
	//didn't find one...
	return false;
}

/*Looks through the list of selected players droids to see if any is selected*/
bool anyDroidSelected(unsigned player)
{
	ASSERT_OR_RETURN(false, player < MAX_PLAYERS, "Invalid player (%" PRIu32 ")", player);

	for (auto& psCurr : playerList[player].droids)
	{
		if (psCurr.damageManager->isSelected()) {
			return true;
		}
	}

	//didn't find one...
	return false;
}

/*Looks through the list of selected players droids to see if one is a cyborg droid*/
bool cyborgDroidSelected(unsigned player)
{
	ASSERT_OR_RETURN(false, player < MAX_PLAYERS, "Invalid player (%" PRIu32 ")", player);

	for (auto& psCurr : playerList[player].droids)
	{
		if (psCurr.damageManager->isSelected() && isCyborg(&psCurr)) {
			return true;
		}
	}

	//didn't find one...
	return false;
}

/* Clear the selection flag for a player */
void clearSelection()
{
	memset(DROIDDOING, 0x0, sizeof(DROIDDOING)); // clear string when deselected

	if (selectedPlayer >= MAX_PLAYERS) {
		return;
	}

	for (auto& psCurrDroid : playerList[selectedPlayer].droids)
	{
		psCurrDroid.damageManager->setSelected(false);
	}
	for (auto& psStruct : playerList[selectedPlayer].structures)
	{
		psStruct.damageManager->setSelected(false);
	}

	bLasS.Struct = false;
	//clear the Deliv Point if one
	for (auto psFlagPos : apsFlagPosLists[selectedPlayer])
	{
		psFlagPos->selected = false;
	}

	intRefreshScreen();
	triggerEventSelected();
}

static void handleDeselectionClick()
{
	clearSelection();
	intObjectSelected(nullptr);
}

//access function for bSensorAssigned variable
void setSensorAssigned()
{
	bSensorAssigned = true;
}

/* Initialise the display system */
bool dispInitialise()
{
	flagReposVarsValid = false;
	gInputManager.contexts().resetStates();
	return true;
}
