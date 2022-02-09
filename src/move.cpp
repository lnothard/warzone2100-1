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
 * @file move.cpp
 * Routines for moving units about the map
 */

#include "lib/framework/frame.h"
#include "lib/framework/math_ext.h"
#include "lib/framework/trig.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio_id.h"

#include "action.h"
#include "astar.h"
#include "console.h"
#include "feature.h"
#include "fpath.h"
#include "map.h"
#include "mapgrid.h"
#include "mission.h"
#include "move.h"
#include "multiplay.h"
#include "multigifts.h"
#include "order.h"
#include "power.h"
#include "qtscript.h"
#include "scores.h"


Movement::Movement(Vector2i src, Vector2i destination)
  : src{src}, destination{destination}
{
}

bool Movement::isStationary() const noexcept
{
  return status == INACTIVE || status == HOVER || status == SHUFFLE;
}

void Movement::set_path_vars(int target_x, int target_y)
{
  path.resize(1);
  destination = {target_x, target_y};
  path[0] = {target_x, target_y};
}

/* Function prototypes */
static void moveUpdatePersonModel(Droid* psDroid, int speed, uint16_t direction);

std::string moveDescription(MOVE_STATUS status)
{
	switch (status) {
    using enum MOVE_STATUS;
	  case INACTIVE: return "Inactive";
	  case NAVIGATE: return "Navigate";
	  case TURN: return "Turn";
	  case PAUSE: return "Pause";
	  case POINT_TO_POINT: return "P2P";
	  case TURN_TO_TARGET: return "Turn2target";
	  case HOVER: return "Hover";
	  case WAIT_FOR_ROUTE: return "Waitroute";
	  case SHUFFLE: return "Shuffle";
	}
	return "Error"; // satisfy compiler
}

/**
 * Move a droid to a location, joining a formation
 * @see moveDroidToBase() for the parameter and return value specification
 */
bool moveDroidTo(Droid* psDroid, Vector2i location, FPATH_MOVETYPE moveType)
{
	return psDroid->moveDroidToBase(location, true, moveType);
}

/**
 * Move a droid to a location, not joining a formation
 * @see moveDroidToBase() for the parameter and return value specification
 */
bool moveDroidToNoFormation(Droid* psDroid, Vector2i location, FPATH_MOVETYPE moveType)
{
	ASSERT_OR_RETURN(false, location.x > 0 && location.y > 0, "Bad movement position");
	return psDroid->moveDroidToBase(location, false, moveType);
}

static bool moveBlockingTileCallback(Vector2i pos, int dist, void* data_)
{
	auto* data = (BLOCKING_CALLBACK_DATA*)data_;
	data->blocking |= pos != data->src && pos != data->dst && fpathBlockingTile(
		map_coord(pos.x), map_coord(pos.y), data->propulsionType);
	return !data->blocking;
}

// see if a Droid has run over a person
static void moveCheckSquished(Droid const* psDroid, int emx, int emy)
{
	const auto droidR = psDroid->objRadius();
	const auto mx = gameTimeAdjustedAverage(emx, EXTRA_PRECISION);
	const auto my = gameTimeAdjustedAverage(emy, EXTRA_PRECISION);

	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(psDroid->getPosition().x,
                              psDroid->getPosition().y,
                              OBJ_MAXRADIUS);

	for (auto psObj : gridList)
	{
		if (!dynamic_cast<Droid*>(psObj) ||
        dynamic_cast<Droid*>(psObj)->getType() != DROID_TYPE::PERSON) {
			// ignore everything but people
			continue;
		}

		auto objR = psObj->objRadius();
		auto rad = droidR + objR;
		auto radSq = rad * rad;

		auto xdiff = psDroid->getPosition().x + mx - psObj->getPosition().x;
		auto ydiff = psDroid->getPosition().y + my - psObj->getPosition().y;
		auto distSq = xdiff * xdiff + ydiff * ydiff;

		if (((2 * radSq) / 3) > distSq) {
			if (psDroid->playerManager->getPlayer() != psObj->playerManager->getPlayer() &&
          !aiCheckAlliances(psDroid->playerManager->getPlayer(), psObj->playerManager->getPlayer())) {
				// run over a bloke - kill him
				destroyDroid(dynamic_cast<Droid*>(psObj), gameTime);
				scoreUpdateVar(WD_BARBARIANS_MOWED_DOWN);
			}
		}
	}
}

// Calculate the actual movement to slide around
static void moveCalcSlideVector(Droid* psDroid, int objX, int objY, int* pMx, int* pMy)
{
	const auto mx = *pMx;
	const auto my = *pMy;
	// Calculate the vector to the obstruction
	const auto obstX = psDroid->getPosition().x - objX;
	const auto obstY = psDroid->getPosition().y - objY;

	// if the target dir is the same, don't need to slide
	if (obstX * mx + obstY * my >= 0) {
		return;
	}

  int dirX, dirY;
	// Choose the tangent vector to this on the same side as the target
	auto dotRes = obstY * mx - obstX * my;
	if (dotRes >= 0) {
		dirX = obstY;
		dirY = -obstX;
	}
	else {
		dirX = -obstY;
		dirY = obstX;
		dotRes = -dotRes;
	}
	auto dirMagSq = MAX(1, dirX * dirX + dirY * dirY);

	// Calculate the component of the movement in the direction of the tangent vector
	*pMx = (int)dirX * dotRes / dirMagSq;
	*pMy = (int)dirY * dotRes / dirMagSq;
}

static void moveOpenGates(Droid const* psDroid, Vector2i tile)
{
	// is the new tile a gate?
	if (!worldOnMap(tile.x, tile.y)) return;

	auto psTile = mapTile(tile);
	if (!isFlying(psDroid) && psTile && psTile->psObject &&
      dynamic_cast<Structure*>(psTile->psObject) &&
      aiCheckAlliances(psTile->psObject->playerManager->getPlayer(),
                       psDroid->playerManager->getPlayer())) {
		requestOpenGate(dynamic_cast<Structure*>(psTile->psObject));
		// If it's a friendly gate, open it. (It would be impolite to open an enemy gate.)
	}
}

static void moveOpenGates(Droid const* psDroid)
{
	Vector2i pos = psDroid->getPosition().xy() + iSinCosR(
          psDroid->getMovementData()->moveDir,
          psDroid->getMovementData()->speed * SAS_OPEN_SPEED / GAME_TICKS_PER_SEC);

	moveOpenGates(psDroid, map_coord(pos));
}

/*!
 * Get a direction for a droid to avoid obstacles etc.
 * \param psDroid Which droid to examine
 * \return The normalised direction vector
 */
static uint16_t moveGetDirection(Droid* psDroid)
{
	Vector2i src = psDroid->getPosition().xy(); // Do not want precise precision here, would overflow.
	Vector2i target = psDroid->getMovementData()->target;
	Vector2i dest = target - src;

	// Transporters don't need to avoid obstacles, but everyone else should
	if (!isTransporter(*psDroid))
	{
		dest = psDroid->moveGetObstacleVector(dest);
	}

	return iAtan2(dest);
}

// Check if a droid has got to a way point
static bool moveReachedWayPoint(Droid const* psDroid)
{
	// Calculate the vector to the droid
	const Vector2i droid = Vector2i(psDroid->getPosition().xy()) - psDroid->getMovementData()->target;
	const bool last = psDroid->getMovementData()->pathIndex == (int)psDroid->getMovementData()->path.size();
	auto sqprecision = last ? ((TILE_UNITS / 4) * (TILE_UNITS / 4)) : ((TILE_UNITS / 2) * (TILE_UNITS / 2));

	if (last && psDroid->getMovementData()->bumpTime != 0) {
		// Make waypoint tolerance 1 tile after 0 seconds, 2 tiles after 3 seconds, X tiles after (X + 1)² seconds.
		sqprecision = (gameTime - psDroid->getMovementData()->bumpTime + GAME_TICKS_PER_SEC) * (TILE_UNITS * TILE_UNITS /
                                                                                  GAME_TICKS_PER_SEC);
	}

	// Else check current waypoint
	return dot(droid, droid) < sqprecision;
}

/** Determine whether a droid has stopped moving.
 *  @return true if the droid doesn't move, false if it's moving.
 */
static bool moveDroidStopped(Droid const* psDroid, int speed)
{
	if (psDroid->getMovementData()->status == MOVE_STATUS::INACTIVE &&
      speed == 0 && psDroid->getMovementData()->speed == 0) {
		return true;
	}
	else {
		return false;
	}
}

// Direction is target direction.
static void moveUpdateDroidDirection(Droid* psDroid, SDWORD* pSpeed, uint16_t direction,
                                     uint16_t iSpinAngle, int iSpinSpeed, int iTurnSpeed, uint16_t* pDroidDir)
{
	*pDroidDir = psDroid->getRotation().direction;

	// don't move if in MOVEPAUSE state
	if (psDroid->getMovementData()->status == MOVE_STATUS::PAUSE) {
		return;
	}

	auto diff = angleDelta(direction - *pDroidDir);
	// Turn while moving - slow down speed depending on target angle so that we can turn faster
	*pSpeed = std::max<int>(*pSpeed * (iSpinAngle - abs(diff)) / iSpinAngle, 0);

	// iTurnSpeed is turn speed at max velocity, increase turn speed up to iSpinSpeed when slowing down
	auto turnSpeed = std::min<int>(iTurnSpeed +
        int64_t(iSpinSpeed - iTurnSpeed) * abs(diff) /
                iSpinAngle, iSpinSpeed);

	// Calculate the maximum change in direction
	auto maxChange = gameTimeAdjustedAverage(turnSpeed);

	// Move *pDroidDir towards target, by at most maxChange.
	*pDroidDir += clip(diff, -maxChange, maxChange);
}


// Calculate current speed perpendicular to droids direction
static int moveCalcPerpSpeed(Droid* psDroid, uint16_t iDroidDir, int iSkidDecel)
{
	auto adiff = angleDelta(iDroidDir - psDroid->getMovementData()->moveDir);
	auto perpSpeed = iSinR(abs(adiff), psDroid->getMovementData()->speed);

	// decelerate the perpendicular speed
	perpSpeed = MAX(0, perpSpeed - gameTimeAdjustedAverage(iSkidDecel));
	return perpSpeed;
}

// Calculate the current speed in the droids normal direction
static int moveCalcNormalSpeed(Droid const* psDroid, int fSpeed, uint16_t iDroidDir, int iAccel, int iDecel)
{
	auto adiff = (uint16_t)(iDroidDir - psDroid->getMovementData()->moveDir); // Cast wrapping intended.
	auto normalSpeed = iCosR(adiff, psDroid->getMovementData()->speed);

	if (normalSpeed < fSpeed) {
		// accelerate
		normalSpeed += gameTimeAdjustedAverage(iAccel);
		if (normalSpeed > fSpeed) {
			normalSpeed = fSpeed;
		}
	}
	else {
		// decelerate
		normalSpeed -= gameTimeAdjustedAverage(iDecel);
		if (normalSpeed < fSpeed) {
			normalSpeed = fSpeed;
		}
	}
	return normalSpeed;
}

static void moveGetDroidPosDiffs(Droid const* psDroid, int* pDX, int* pDY)
{
	auto move = psDroid->getMovementData()->speed * EXTRA_PRECISION; // high precision
	*pDX = iSinR(psDroid->getMovementData()->moveDir, move);
	*pDY = iCosR(psDroid->getMovementData()->moveDir, move);
}

// see if the droid is close to the final way point
static void moveCheckFinalWaypoint(Droid const* psDroid, int* pSpeed)
{
	auto minEndSpeed = (*pSpeed + 2) / 3;
	minEndSpeed = std::min(minEndSpeed, MIN_END_SPEED);

	// don't do this for VTOLs doing attack runs
	if (psDroid->isVtol() && (psDroid->getAction() == ACTION::VTOL_ATTACK)) {
		return;
	}

	if (psDroid->getMovementData()->status != MOVE_STATUS::SHUFFLE &&
      psDroid->getMovementData()->pathIndex == (int)psDroid->getMovementData()->path.size()) {
		auto diff = psDroid->getPosition().xy() - psDroid->getMovementData()->target;
		auto distSq = dot(diff, diff);

		if (distSq < END_SPEED_RANGE * END_SPEED_RANGE) {
			*pSpeed = (*pSpeed - minEndSpeed) * distSq / (END_SPEED_RANGE * END_SPEED_RANGE) + minEndSpeed;
		}
	}
}

static void moveUpdateDroidPos(Droid* psDroid, int dx, int dy)
{
	if (psDroid->getMovementData()->status == MOVE_STATUS::PAUSE ||
      psDroid->damageManager->isDead()) {
		// don't actually move if the move is paused
		return;
	}

	auto x = psDroid->getPosition().x + gameTimeAdjustedAverage(dx, EXTRA_PRECISION);
	auto y = psDroid->getPosition().y + gameTimeAdjustedAverage(dy, EXTRA_PRECISION);
  psDroid->setPosition({x, y, psDroid->getPosition().z});

	/* impact if about to go off map else update coordinates */
	if (!worldOnMap(psDroid->getPosition().x, psDroid->getPosition().y)) {
		/* transporter going off-world will trigger next map, and is ok */
		ASSERT(isTransporter(*psDroid), "droid trying to move off the map!");

		if (!isTransporter(*psDroid)) {
			/* dreadful last-ditch crash-avoiding hack - sort this! - GJ */
			destroyDroid(psDroid, gameTime);
			return;
		}
	}

  if (!isTransporter(*psDroid)) return;
  if (psDroid->getPosition().x == 0) {
    psDroid->setPosition({1, psDroid->getPosition().y, psDroid->getPosition().z});
  }
  if (psDroid->getPosition().y == 0) {
    psDroid->setPosition({psDroid->getPosition().x, 1, psDroid->getPosition().z});
  }
}

bool moveCheckDroidMovingAndVisible(Droid* psDroid)
{
  ASSERT_OR_RETURN(false, psDroid != nullptr, "Droid pointer is null");

	/* check for dead, not moving or invisible to player */
	if (psDroid->damageManager->isDead() || moveDroidStopped(psDroid, 0) ||
      isTransporter(*psDroid) && psDroid->getOrder()->type == ORDER_TYPE::NONE ||
      !psDroid->isVisibleToSelectedPlayer()) {
		psDroid->setAudioId(NO_SOUND);
		return false;
	}
	return true;
}

static bool moveDroidStartCallback(Droid* psDroid)
{
  ASSERT_OR_RETURN(false, psDroid != nullptr, "Droid pointer is null");
	movePlayDroidMoveAudio(psDroid);
	return true;
}

static bool pickupOilDrum(unsigned toPlayer, unsigned fromPlayer)
{
	auto power = OILDRUM_POWER;
	if (!bMultiPlayer && !bInTutorial) {
		// Let Beta and Gamma campaign oil drums give a little more power
		if (getCampaignNumber() == 2) {
			power = OILDRUM_POWER + (OILDRUM_POWER / 2);
		}
		else if (getCampaignNumber() == 3) {
			power = OILDRUM_POWER * 2;
		}
	}
	addPower(toPlayer, power); // give power

	if (toPlayer == selectedPlayer) {
		CONPRINTF(_("You found %u power in an oil drum."), power);
	}
	return true;
}

#define DROIDDIST ((TILE_UNITS*5)/2)

// called when a droid moves to a new tile.
// use to pick up oil, etc..
static void checkLocalFeatures(Droid* psDroid)
{
	// NOTE: Why not do this for AI units also?
	if ((!isHumanPlayer(psDroid->playerManager->getPlayer()) &&
       psDroid->getOrder()->type != ORDER_TYPE::RECOVER) ||
       psDroid->isVtol() || isTransporter(*psDroid)) { // VTOLs or transporters can't pick up features
		return;
	}

	// scan the neighbours
	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(psDroid->getPosition().x, psDroid->getPosition().y, DROIDDIST);
	for (auto psObj : gridList)
	{
		bool pickedUp = false;

		if (dynamic_cast<Feature*>(psObj) && !psObj->damageManager->isDead()) {
			switch (dynamic_cast<Feature*>(psObj)->getStats()->subType) {
        using enum FEATURE_TYPE;
			  case OIL_DRUM:
			  	pickedUp = pickupOilDrum(psDroid->playerManager->getPlayer(),
                                   psObj->playerManager->getPlayer());

			  	triggerEventPickup(dynamic_cast<Feature*>(psObj), psDroid);
			  	break;
			  case GEN_ARTE:
			  	pickedUp = pickupArtefact(psDroid->playerManager->getPlayer(),
                                    psObj->playerManager->getPlayer());

			  	triggerEventPickup(dynamic_cast<Feature*>(psObj), psDroid);
			  	break;
			  default:
			  	break;
			}
		}

		if (!pickedUp) continue; // Object is not a living oil drum or artefact

		turnOffMultiMsg(true);
		removeFeature(dynamic_cast<Feature*>(psObj)); // remove artifact+.
		turnOffMultiMsg(false);
	}
}
