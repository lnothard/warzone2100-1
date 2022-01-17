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

#include "lib/framework/trig.h"
#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "console.h"

#include "move.h"

#include "objects.h"
#include "visibility.h"
#include "map.h"
#include "fpath.h"
#include "loop.h"
#include "geometry.h"
#include "action.h"
#include "order.h"
#include "astar.h"
#include "mapgrid.h"
#include "display.h"
#include "effects.h"
#include "feature.h"
#include "power.h"
#include "scores.h"
#include "multiplay.h"
#include "multigifts.h"
#include "random.h"
#include "mission.h"
#include "qtscript.h"


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
  using enum MOVE_STATUS;
	switch (status) {
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
bool moveDroidTo(Droid* psDroid, unsigned x, unsigned y, FPATH_MOVETYPE moveType)
{
	return psDroid->moveDroidToBase(x, y, true, moveType);
}

/**
 * Move a droid to a location, not joining a formation
 * @see moveDroidToBase() for the parameter and return value specification
 */
bool moveDroidToNoFormation(Droid* psDroid, unsigned x, unsigned y, FPATH_MOVETYPE moveType)
{
	ASSERT_OR_RETURN(false, x > 0 && y > 0, "Bad movement position");
	return psDroid->moveDroidToBase(x, y, false, moveType);
}

struct BLOCKING_CALLBACK_DATA
{
	PROPULSION_TYPE propulsionType;
	bool blocking;
	Vector2i src;
	Vector2i dst;
};

static bool moveBlockingTileCallback(Vector2i pos, int32_t dist, void* data_)
{
	auto* data = (BLOCKING_CALLBACK_DATA*)data_;
	data->blocking |= pos != data->src && pos != data->dst && fpathBlockingTile(
		map_coord(pos.x), map_coord(pos.y), data->propulsionType);
	return !data->blocking;
}

// see if a Droid has run over a person
static void moveCheckSquished(Droid* psDroid, int emx, int emy)
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
			if (psDroid->getPlayer() != psObj->getPlayer() &&
          !aiCheckAlliances(psDroid->getPlayer(), psObj->getPlayer())) {
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
	const int mx = *pMx;
	const int my = *pMy;
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

static void moveOpenGates(Droid* psDroid, Vector2i tile)
{
	// is the new tile a gate?
	if (!worldOnMap(tile.x, tile.y)) {
		return;
	}
	auto psTile = mapTile(tile);
	if (!isFlying(psDroid) && psTile && psTile->psObject &&
      dynamic_cast<Structure*>(psTile->psObject) &&
      aiCheckAlliances( psTile->psObject->getPlayer(), psDroid->getPlayer())) {
		requestOpenGate(dynamic_cast<Structure*>(psTile->psObject));
		// If it's a friendly gate, open it. (It would be impolite to open an enemy gate.)
	}
}

static void moveOpenGates(Droid* psDroid)
{
	Vector2i pos = psDroid->getPosition().xy() + iSinCosR(
          psDroid->getMovementData().moveDir,
          psDroid->getMovementData().speed * SAS_OPEN_SPEED / GAME_TICKS_PER_SEC);
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
	Vector2i target = psDroid->getMovementData().target;
	Vector2i dest = target - src;

	// Transporters don't need to avoid obstacles, but everyone else should
	if (!isTransporter(*psDroid))
	{
		dest = psDroid->moveGetObstacleVector(dest);
	}

	return iAtan2(dest);
}

// Check if a droid has got to a way point
static bool moveReachedWayPoint(Droid* psDroid)
{
	// Calculate the vector to the droid
	const Vector2i droid = Vector2i(psDroid->pos.xy()) - psDroid->movement.target;
	const bool last = psDroid->movement.pathIndex == (int)psDroid->movement.path.size();
	int sqprecision = last ? ((TILE_UNITS / 4) * (TILE_UNITS / 4)) : ((TILE_UNITS / 2) * (TILE_UNITS / 2));

	if (last && psDroid->movement.bumpTime != 0)
	{
		// Make waypoint tolerance 1 tile after 0 seconds, 2 tiles after 3 seconds, X tiles after (X + 1)² seconds.
		sqprecision = (gameTime - psDroid->movement.bumpTime + GAME_TICKS_PER_SEC) * (TILE_UNITS * TILE_UNITS /
                                                                                  GAME_TICKS_PER_SEC);
	}

	// Else check current waypoint
	return dot(droid, droid) < sqprecision;
}

#define MAX_SPEED_PITCH  60

/** Calculate the new speed for a droid based on factors like pitch.
 *  @todo Remove hack for steep slopes not properly marked as blocking on some maps.
 */
SDWORD moveCalcDroidSpeed(Droid* psDroid)
{
	const uint16_t maxPitch = DEG(MAX_SPEED_PITCH);
	UDWORD mapX, mapY;
	int speed, pitch;
	WeaponStats* psWStats;

	CHECK_DROID(psDroid);

	// NOTE: This screws up since the transporter is offscreen still (on a mission!), and we are trying to find terrainType of a tile (that is offscreen!)
	if (psDroid->type == DROID_SUPERTRANSPORTER && missionIsOffworld())
	{
		PropulsionStats* propulsion = asPropulsionStats + psDroid->asBits[COMP_PROPULSION];
		speed = propulsion->maxSpeed;
	}
	else
	{
		mapX = map_coord(psDroid->pos.x);
		mapY = map_coord(psDroid->pos.y);
		speed = calcDroidSpeed(psDroid->base_speed, terrainType(mapTile(mapX, mapY)), psDroid->asBits[COMP_PROPULSION],
                           getDroidEffectiveLevel(psDroid));
	}


	// now offset the speed for the slope of the droid
	pitch = angleDelta(psDroid->rot.pitch);
	speed = (maxPitch - pitch) * speed / maxPitch;
	if (speed <= 10)
	{
		// Very nasty hack to deal with buggy maps, where some cliffs are
		// not properly marked as being cliffs, but too steep to drive over.
		// This confuses the heck out of the path-finding code! - Per
		speed = 10;
	}

	// stop droids that have just fired a no fire while moving weapon
	if (psDroid->numWeaps > 0)
	{
		if (psDroid->asWeaps[0].nStat > 0 && psDroid->asWeaps[0].timeLastFired + FOM_MOVEPAUSE > gameTime)
		{
			psWStats = asWeaponStats + psDroid->asWeaps[0].nStat;
			if (!psWStats->fireOnMove)
			{
				speed = 0;
			}
		}
	}

	// slow down shuffling VTOLs
	if (isVtolDroid(psDroid) &&
      (psDroid->movement.status == MOVESHUFFLE) &&
      (speed > MIN_END_SPEED))
	{
		speed = MIN_END_SPEED;
	}

	CHECK_DROID(psDroid);

	return speed;
}

/** Determine whether a droid has stopped moving.
 *  @return true if the droid doesn't move, false if it's moving.
 */
static bool moveDroidStopped(Droid* psDroid, SDWORD speed)
{
	if (psDroid->movement.status == MOVEINACTIVE && speed == 0 && psDroid->movement.speed == 0)
	{
		return true;
	}
	else
	{
		return false;
	}
}

// Direction is target direction.
static void moveUpdateDroidDirection(Droid* psDroid, SDWORD* pSpeed, uint16_t direction,
                                     uint16_t iSpinAngle, int iSpinSpeed, int iTurnSpeed, uint16_t* pDroidDir)
{
	*pDroidDir = psDroid->rot.direction;

	// don't move if in MOVEPAUSE state
	if (psDroid->movement.status == MOVEPAUSE)
	{
		return;
	}

	int diff = angleDelta(direction - *pDroidDir);
	// Turn while moving - slow down speed depending on target angle so that we can turn faster
	*pSpeed = std::max<int>(*pSpeed * (iSpinAngle - abs(diff)) / iSpinAngle, 0);

	// iTurnSpeed is turn speed at max velocity, increase turn speed up to iSpinSpeed when slowing down
	int turnSpeed = std::min<int>(iTurnSpeed + int64_t(iSpinSpeed - iTurnSpeed) * abs(diff) / iSpinAngle, iSpinSpeed);

	// Calculate the maximum change in direction
	int maxChange = gameTimeAdjustedAverage(turnSpeed);

	// Move *pDroidDir towards target, by at most maxChange.
	*pDroidDir += clip(diff, -maxChange, maxChange);
}


// Calculate current speed perpendicular to droids direction
static int moveCalcPerpSpeed(Droid* psDroid, uint16_t iDroidDir, SDWORD iSkidDecel)
{
	int adiff = angleDelta(iDroidDir - psDroid->movement.moveDir);
	int perpSpeed = iSinR(abs(adiff), psDroid->movement.speed);

	// decelerate the perpendicular speed
	perpSpeed = MAX(0, perpSpeed - gameTimeAdjustedAverage(iSkidDecel));

	return perpSpeed;
}


static void moveCombineNormalAndPerpSpeeds(Droid* psDroid, int fNormalSpeed, int fPerpSpeed, uint16_t iDroidDir)
{
	int16_t adiff;
	int relDir;
	int finalSpeed;

	/* set current direction */
	psDroid->rot.direction = iDroidDir;

	/* set normal speed and direction if perpendicular speed is zero */
	if (fPerpSpeed == 0)
	{
		psDroid->movement.speed = fNormalSpeed;
		psDroid->movement.moveDir = iDroidDir;
		return;
	}

	finalSpeed = iHypot(fNormalSpeed, fPerpSpeed);

	// calculate the angle between the droid facing and movement direction
	relDir = iAtan2(fPerpSpeed, fNormalSpeed);

	// choose the finalDir on the same side as the old movement direction
	adiff = angleDelta(iDroidDir - psDroid->movement.moveDir);

	psDroid->movement.moveDir = adiff < 0 ? iDroidDir + relDir : iDroidDir - relDir; // Cast wrapping intended.
	psDroid->movement.speed = finalSpeed;
}


// Calculate the current speed in the droids normal direction
static int moveCalcNormalSpeed(Droid* psDroid, int fSpeed, uint16_t iDroidDir, SDWORD iAccel, SDWORD iDecel)
{
	uint16_t adiff;
	int normalSpeed;

	adiff = (uint16_t)(iDroidDir - psDroid->movement.moveDir); // Cast wrapping intended.
	normalSpeed = iCosR(adiff, psDroid->movement.speed);

	if (normalSpeed < fSpeed)
	{
		// accelerate
		normalSpeed += gameTimeAdjustedAverage(iAccel);
		if (normalSpeed > fSpeed)
		{
			normalSpeed = fSpeed;
		}
	}
	else
	{
		// decelerate
		normalSpeed -= gameTimeAdjustedAverage(iDecel);
		if (normalSpeed < fSpeed)
		{
			normalSpeed = fSpeed;
		}
	}

	return normalSpeed;
}

static void moveGetDroidPosDiffs(Droid* psDroid, int32_t* pDX, int32_t* pDY)
{
	int32_t move = psDroid->movement.speed * EXTRA_PRECISION; // high precision

	*pDX = iSinR(psDroid->movement.moveDir, move);
	*pDY = iCosR(psDroid->movement.moveDir, move);
}

// see if the droid is close to the final way point
static void moveCheckFinalWaypoint(Droid* psDroid, SDWORD* pSpeed)
{
	int minEndSpeed = (*pSpeed + 2) / 3;
	minEndSpeed = std::min(minEndSpeed, MIN_END_SPEED);

	// don't do this for VTOLs doing attack runs
	if (isVtolDroid(psDroid) && (psDroid->action == DACTION_VTOLATTACK))
	{
		return;
	}

	if (psDroid->movement.status != MOVESHUFFLE &&
      psDroid->movement.pathIndex == (int)psDroid->movement.path.size())
	{
		Vector2i diff = psDroid->pos.xy() - psDroid->movement.target;
		int distSq = dot(diff, diff);
		if (distSq < END_SPEED_RANGE * END_SPEED_RANGE)
		{
			*pSpeed = (*pSpeed - minEndSpeed) * distSq / (END_SPEED_RANGE * END_SPEED_RANGE) + minEndSpeed;
		}
	}
}

static void moveUpdateDroidPos(Droid* psDroid, int32_t dx, int32_t dy)
{
	CHECK_DROID(psDroid);

	if (psDroid->movement.status == MOVEPAUSE || isDead((PersistentObject*)psDroid))
	{
		// don't actually move if the move is paused
		return;
	}

	psDroid->pos.x += gameTimeAdjustedAverage(dx, EXTRA_PRECISION);
	psDroid->pos.y += gameTimeAdjustedAverage(dy, EXTRA_PRECISION);

	/* impact if about to go off map else update coordinates */
	if (worldOnMap(psDroid->pos.x, psDroid->pos.y) == false)
	{
		/* transporter going off-world will trigger next map, and is ok */
		ASSERT(isTransporter(psDroid), "droid trying to move off the map!");
		if (!isTransporter(psDroid))
		{
			/* dreadful last-ditch crash-avoiding hack - sort this! - GJ */
			destroyDroid(psDroid, gameTime);
			return;
		}
	}

	// lovely hack to keep transporters just on the map
	// two weeks to go and the hacks just get better !!!
	if (isTransporter(psDroid))
	{
		if (psDroid->pos.x == 0)
		{
			psDroid->pos.x = 1;
		}
		if (psDroid->pos.y == 0)
		{
			psDroid->pos.y = 1;
		}
	}
	CHECK_DROID(psDroid);
}

/* Update a tracked droids position and speed given target values */
static void moveUpdateGroundModel(Droid* psDroid, SDWORD speed, uint16_t direction)
{
	int fPerpSpeed, fNormalSpeed;
	uint16_t iDroidDir;
	uint16_t slideDir;
	PropulsionStats* psPropStats;
	int32_t spinSpeed, spinAngle, turnSpeed, dx, dy, bx, by;

	CHECK_DROID(psDroid);

	// nothing to do if the droid is stopped
	if (moveDroidStopped(psDroid, speed))
	{
		return;
	}

	psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION];
	spinSpeed = psDroid->base_speed * psPropStats->spinSpeed;
	turnSpeed = psDroid->base_speed * psPropStats->turnSpeed;
	spinAngle = DEG(psPropStats->spinAngle);

	moveCheckFinalWaypoint(psDroid, &speed);

	moveUpdateDroidDirection(psDroid, &speed, direction, spinAngle, spinSpeed, turnSpeed, &iDroidDir);

	fNormalSpeed = moveCalcNormalSpeed(psDroid, speed, iDroidDir, psPropStats->acceleration, psPropStats->deceleration);
	fPerpSpeed = moveCalcPerpSpeed(psDroid, iDroidDir, psPropStats->skidDeceleration);

	moveCombineNormalAndPerpSpeeds(psDroid, fNormalSpeed, fPerpSpeed, iDroidDir);
	moveGetDroidPosDiffs(psDroid, &dx, &dy);
	moveOpenGates(psDroid);
	moveCheckSquished(psDroid, dx, dy);
	moveCalcDroidSlide(psDroid, &dx, &dy);
	bx = dx;
	by = dy;
	moveCalcBlockingSlide(psDroid, &bx, &by, direction, &slideDir);
	if (bx != dx || by != dy)
	{
		moveUpdateDroidDirection(psDroid, &speed, slideDir, spinAngle, psDroid->base_speed * DEG(1),
                             psDroid->base_speed * DEG(1) / 3, &iDroidDir);
		psDroid->rot.direction = iDroidDir;
	}

	moveUpdateDroidPos(psDroid, bx, by);

	//set the droid height here so other routines can use it
	psDroid->pos.z = map_Height(psDroid->pos.x, psDroid->pos.y); //jps 21july96
	updateDroidOrientation(psDroid);
}

/* Update a persons position and speed given target values */
static void moveUpdatePersonModel(Droid* psDroid, SDWORD speed, uint16_t direction)
{
	int fPerpSpeed, fNormalSpeed;
	int32_t spinSpeed, turnSpeed, dx, dy;
	uint16_t iDroidDir;
	uint16_t slideDir;
	PropulsionStats* psPropStats;

	CHECK_DROID(psDroid);

	// if the droid is stopped, only make sure animations are set correctly
	if (moveDroidStopped(psDroid, speed))
	{
		if (psDroid->type == DROID_PERSON &&
				(psDroid->action == DACTION_ATTACK ||
				psDroid->action == DACTION_ROTATETOATTACK)
				&& psDroid->animationEvent != ANIM_EVENT_DYING
				&& psDroid->animationEvent != ANIM_EVENT_FIRING)
		{
			psDroid->timeAnimationStarted = gameTime;
			psDroid->animationEvent = ANIM_EVENT_FIRING;
		}
		else if (psDroid->animationEvent == ANIM_EVENT_ACTIVE)
		{
			psDroid->timeAnimationStarted = 0; // turn off movement animation, since we stopped
			psDroid->animationEvent = ANIM_EVENT_NONE;
		}
		return;
	}

	psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION];
	spinSpeed = psDroid->base_speed * psPropStats->spinSpeed;
	turnSpeed = psDroid->base_speed * psPropStats->turnSpeed;

	moveUpdateDroidDirection(psDroid, &speed, direction, DEG(psPropStats->spinAngle), spinSpeed, turnSpeed, &iDroidDir);

	fNormalSpeed = moveCalcNormalSpeed(psDroid, speed, iDroidDir, psPropStats->acceleration, psPropStats->deceleration);

	/* people don't skid at the moment so set zero perpendicular speed */
	fPerpSpeed = 0;

	moveCombineNormalAndPerpSpeeds(psDroid, fNormalSpeed, fPerpSpeed, iDroidDir);
	moveGetDroidPosDiffs(psDroid, &dx, &dy);
	moveOpenGates(psDroid);
	moveCalcDroidSlide(psDroid, &dx, &dy);
	moveCalcBlockingSlide(psDroid, &dx, &dy, direction, &slideDir);
	moveUpdateDroidPos(psDroid, dx, dy);

	//set the droid height here so other routines can use it
	psDroid->pos.z = map_Height(psDroid->pos.x, psDroid->pos.y); //jps 21july96

	/* update anim if moving */
	if (psDroid->type == DROID_PERSON && speed != 0 && (psDroid->animationEvent != ANIM_EVENT_ACTIVE && psDroid->
		animationEvent != ANIM_EVENT_DYING))
	{
		psDroid->timeAnimationStarted = gameTime;
		psDroid->animationEvent = ANIM_EVENT_ACTIVE;
	}

	CHECK_DROID(psDroid);
}

static void moveUpdateVtolModel(Droid* psDroid, SDWORD speed, uint16_t direction)
{
	int fPerpSpeed, fNormalSpeed;
	uint16_t iDroidDir;
	uint16_t slideDir;
	int32_t spinSpeed, turnSpeed, iMapZ, iSpinSpeed, iTurnSpeed, dx, dy;
	uint16_t targetRoll;
	PropulsionStats* psPropStats;

	CHECK_DROID(psDroid);

	// nothing to do if the droid is stopped
	if (moveDroidStopped(psDroid, speed))
	{
		return;
	}

	psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION];
	spinSpeed = DEG(psPropStats->spinSpeed);
	turnSpeed = DEG(psPropStats->turnSpeed);

	moveCheckFinalWaypoint(psDroid, &speed);

	if (isTransporter(*psDroid))
	{
		moveUpdateDroidDirection(psDroid, &speed, direction, DEG(psPropStats->spinAngle), spinSpeed, turnSpeed,
		                         &iDroidDir);
	}
	else
	{
		iSpinSpeed = std::max<int>(psDroid->base_speed * DEG(1) / 2, spinSpeed);
		iTurnSpeed = std::max<int>(psDroid->base_speed * DEG(1) / 8, turnSpeed);
		moveUpdateDroidDirection(psDroid, &speed, direction, DEG(psPropStats->spinAngle), iSpinSpeed, iTurnSpeed,
		                         &iDroidDir);
	}

	fNormalSpeed = moveCalcNormalSpeed(psDroid, speed, iDroidDir, psPropStats->acceleration, psPropStats->deceleration);
	fPerpSpeed = moveCalcPerpSpeed(psDroid, iDroidDir, psPropStats->skidDeceleration);

	moveCombineNormalAndPerpSpeeds(psDroid, fNormalSpeed, fPerpSpeed, iDroidDir);

	moveGetDroidPosDiffs(psDroid, &dx, &dy);

	/* set slide blocking tile for map edge */
	if (!isTransporter(*psDroid))
	{
		moveCalcBlockingSlide(psDroid, &dx, &dy, direction, &slideDir);
	}

	moveUpdateDroidPos(psDroid, dx, dy);

	/* update vtol orientation */
	targetRoll = clip(4 * angleDelta(psDroid->movement.moveDir - psDroid->rot.direction), -DEG(60), DEG(60));
	psDroid->rot.roll = psDroid->rot.roll + (uint16_t)gameTimeAdjustedIncrement(
		3 * angleDelta(targetRoll - psDroid->rot.roll));

	/* do vertical movement - only if on the map */
	if (worldOnMap(psDroid->pos.x, psDroid->pos.y))
	{
		iMapZ = map_Height(psDroid->pos.x, psDroid->pos.y);
		psDroid->pos.z = MAX(iMapZ, psDroid->pos.z + gameTimeAdjustedIncrement(psDroid->movement.vertical_speed));
		moveAdjustVtolHeight(psDroid, iMapZ);
	}
}

static void moveUpdateCyborgModel(Droid* psDroid, int moveSpeed, uint16_t moveDir, uint8_t oldStatus)
{
	// nothing to do if the droid is stopped
	if (moveDroidStopped(psDroid, moveSpeed))
	{
		if (psDroid->animationEvent == ANIM_EVENT_ACTIVE)
		{
			psDroid->timeAnimationStarted = 0;
			psDroid->animationEvent = ANIM_EVENT_NONE;
		}
		return;
	}

	if (psDroid->animationEvent == ANIM_EVENT_NONE)
	{
		psDroid->timeAnimationStarted = gameTime;
		psDroid->animationEvent = ANIM_EVENT_ACTIVE;
	}

	/* use baba person movement */
	moveUpdatePersonModel(psDroid, moveSpeed, moveDir);

	psDroid->rot.pitch = 0;
	psDroid->rot.roll = 0;
}


bool moveCheckDroidMovingAndVisible(void* psObj)
{
	auto psDroid = static_cast<Droid*>(psObj);

	if (psDroid == nullptr) {
		return false;
	}

	/* check for dead, not moving or invisible to player */
	if (psDroid->died || moveDroidStopped(psDroid, 0) ||
		(isTransporter(*psDroid) && psDroid->getOrder().type == ORDER_TYPE::NONE) ||
		!(psDroid->visibleToSelectedPlayer())) {
		psDroid->iAudioID = NO_SOUND;
		return false;
	}

	return true;
}

static bool moveDroidStartCallback(void* psObj)
{
	auto* psDroid = (Droid*)psObj;

	if (psDroid == nullptr) {
		return false;
	}
	movePlayDroidMoveAudio(psDroid);
	return true;
}

static bool pickupOilDrum(int toPlayer, int fromPlayer)
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
	if ((!isHumanPlayer(psDroid->getPlayer()) &&
       psDroid->getOrder().type != ORDER_TYPE::RECOVER) ||
       psDroid->isVtol() ||
		isTransporter(*psDroid)) { // VTOLs or transporters can't pick up features
		return;
	}

	// scan the neighbours
	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(psDroid->getPosition().x, psDroid->getPosition().y, DROIDDIST);
	for (auto psObj : gridList)
	{
			bool pickedUp = false;

		if (dynamic_cast<Feature*>(psObj) && !psObj->died)
		{
			switch (dynamic_cast<Feature*>(psObj)->getStats()->subType)
			{
        using enum FEATURE_TYPE;
			case OIL_DRUM:
				pickedUp = pickupOilDrum(psDroid->getPlayer(), psObj->getPlayer());
				triggerEventPickup(dynamic_cast<Feature*>(psObj), psDroid);
				break;
			case GEN_ARTE:
				pickedUp = pickupArtefact(psDroid->getPlayer(), psObj->getPlayer());
				triggerEventPickup(dynamic_cast<Feature*>(psObj), psDroid);
				break;
			default:
				break;
			}
		}

		if (!pickedUp)
		{
			// Object is not a living oil drum or artefact.
			continue;
		}

		turnOffMultiMsg(true);
		removeFeature(dynamic_cast<Feature*>(psObj)); // remove artifact+.
		turnOffMultiMsg(false);
	}
}


/* Frame update for the movement of a tracked droid */
void moveUpdateDroid(Droid* psDroid)
{
	UDWORD oldx, oldy;
	UBYTE oldStatus = psDroid->movement.status;
	SDWORD moveSpeed;
	uint16_t moveDir;
	PropulsionStats* psPropStats;
	Vector3i pos(0, 0, 0);
	bool bStarted = false, bStopped;

	CHECK_DROID(psDroid);

	psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION];
	ASSERT_OR_RETURN(, psPropStats != nullptr, "Invalid propulsion stats pointer");

	// If the droid has been attacked by an EMP weapon, it is temporarily disabled
	if (psDroid->lastHitWeapon == WSC_EMP)
	{
		if (gameTime - psDroid->timeLastHit < EMP_DISABLE_TIME)
		{
			// Get out without updating
			return;
		}
	}

	/* save current motion status of droid */
	bStopped = moveDroidStopped(psDroid, 0);

	moveSpeed = 0;
	moveDir = psDroid->rot.direction;

	switch (psDroid->movement.status)
	{
	case MOVEINACTIVE:
		if (psDroid->animationEvent == ANIM_EVENT_ACTIVE)
		{
			psDroid->timeAnimationStarted = 0;
			psDroid->animationEvent = ANIM_EVENT_NONE;
		}
		break;
	case MOVESHUFFLE:
		if (moveReachedWayPoint(psDroid) || (psDroid->movement.shuffleStart + MOVE_SHUFFLETIME) < gameTime)
		{
			if (psPropStats->propulsionType == PROPULSION_TYPE_LIFT)
			{
				psDroid->movement.status = MOVEHOVER;
			}
			else
			{
				psDroid->movement.status = MOVEINACTIVE;
			}
		}
		else
		{
			// Calculate a target vector
			moveDir = moveGetDirection(psDroid);

			moveSpeed = moveCalcDroidSpeed(psDroid);
		}
		break;
	case MOVEWAITROUTE:
		moveDroidTo(psDroid, psDroid->movement.destination.x, psDroid->movement.destination.y);
		moveSpeed = MAX(0, psDroid->movement.speed - 1);
		if (psDroid->movement.status != MOVENAVIGATE)
		{
			break;
		}
	// fallthrough
	case MOVENAVIGATE:
		// Get the next control point
		if (!moveNextTarget(psDroid))
		{
			// No more waypoints - finish
			if (psPropStats->propulsionType == PROPULSION_TYPE_LIFT)
			{
				psDroid->movement.status = MOVEHOVER;
			}
			else
			{
				psDroid->movement.status = MOVEINACTIVE;
			}
			break;
		}

		if (isVtolDroid(psDroid))
		{
			psDroid->rot.pitch = 0;
		}

		psDroid->movement.status = MOVEPOINTTOPOINT;
		psDroid->movement.bumpTime = 0;
		moveSpeed = MAX(0, psDroid->movement.speed - 1);

	/* save started status for movePlayAudio */
		if (psDroid->movement.speed == 0)
		{
			bStarted = true;
		}
	// fallthrough
	case MOVEPOINTTOPOINT:
	case MOVEPAUSE:
		// moving between two way points
		if (psDroid->movement.path.size() == 0)
		{
			debug(LOG_WARNING, "No path to follow, but psDroid->sMove.Status = %d", psDroid->movement.status);
		}

	// Get the best control point.
		if (psDroid->movement.path.size() == 0 || !moveBestTarget(psDroid))
		{
			// Got stuck somewhere, can't find the path.
			moveDroidTo(psDroid, psDroid->movement.destination.x, psDroid->movement.destination.y);
		}

	// See if the target point has been reached
		if (moveReachedWayPoint(psDroid))
		{
			// Got there - move onto the next waypoint
			if (!moveNextTarget(psDroid))
			{
				// No more waypoints - finish
				if (psPropStats->propulsionType == PROPULSION_TYPE_LIFT)
				{
					// check the location for vtols
					Vector2i tar = psDroid->pos.xy();
					if (psDroid->order.type != DORDER_PATROL && psDroid->order.type != DORDER_CIRCLE
						// Not doing an order which means we never land (which means we might want to land).
						&& psDroid->action != DACTION_MOVETOREARM && psDroid->action != DACTION_MOVETOREARMPOINT
						&& actionVTOLLandingPos(psDroid, &tar) // Can find a sensible place to land.
						&& map_coord(tar) != map_coord(psDroid->movement.destination))
					// We're not at the right place to land.
					{
						psDroid->movement.destination = tar;
						moveDroidTo(psDroid, psDroid->movement.destination.x, psDroid->movement.destination.y);
					}
					else
					{
						psDroid->movement.status = MOVEHOVER;
					}
				}
				else
				{
					psDroid->movement.status = MOVETURN;
				}
				objTrace(psDroid->id, "Arrived at destination!");
				break;
			}
		}

		moveDir = moveGetDirection(psDroid);
		moveSpeed = moveCalcDroidSpeed(psDroid);

		if ((psDroid->movement.bumpTime != 0) &&
        (psDroid->movement.pauseTime + psDroid->movement.bumpTime + BLOCK_PAUSETIME < gameTime))
		{
			if (psDroid->movement.status == MOVEPOINTTOPOINT)
			{
				psDroid->movement.status = MOVEPAUSE;
			}
			else
			{
				psDroid->movement.status = MOVEPOINTTOPOINT;
			}
			psDroid->movement.pauseTime = (UWORD)(gameTime - psDroid->movement.bumpTime);
		}

		if ((psDroid->movement.status == MOVEPAUSE) &&
        (psDroid->movement.bumpTime != 0) &&
        (psDroid->movement.lastBump > psDroid->movement.pauseTime) &&
        (psDroid->movement.lastBump + psDroid->movement.bumpTime + BLOCK_PAUSERELEASE < gameTime))
		{
			psDroid->movement.status = MOVEPOINTTOPOINT;
		}

		break;
	case MOVETURN:
		// Turn the droid to it's final facing
		if (psPropStats->propulsionType == PROPULSION_TYPE_LIFT)
		{
			psDroid->movement.status = MOVEPOINTTOPOINT;
		}
		else
		{
			psDroid->movement.status = MOVEINACTIVE;
		}
		break;
	case MOVETURNTOTARGET:
		moveSpeed = 0;
		moveDir = iAtan2(psDroid->movement.target - psDroid->pos.xy());
		break;
	case MOVEHOVER:
		moveDescending(psDroid);
		break;

	default:
		ASSERT(false, "unknown move state");
		return;
		break;
	}

	// Update the movement model for the droid
	oldx = psDroid->pos.x;
	oldy = psDroid->pos.y;

	if (psDroid->type == DROID_PERSON)
	{
		moveUpdatePersonModel(psDroid, moveSpeed, moveDir);
	}
	else if (isCyborg(psDroid))
	{
		moveUpdateCyborgModel(psDroid, moveSpeed, moveDir, oldStatus);
	}
	else if (psPropStats->propulsionType == PROPULSION_TYPE_LIFT)
	{
		moveUpdateVtolModel(psDroid, moveSpeed, moveDir);
	}
	else
	{
		moveUpdateGroundModel(psDroid, moveSpeed, moveDir);
	}

	if (map_coord(oldx) != map_coord(psDroid->pos.x)
		|| map_coord(oldy) != map_coord(psDroid->pos.y))
	{
		visTilesUpdate((PersistentObject*)psDroid);

		// object moved from one tile to next, check to see if droid is near stuff.(oil)
		checkLocalFeatures(psDroid);

		triggerEventDroidMoved(psDroid, oldx, oldy);
	}

	// See if it's got blocked
	if ((psPropStats->propulsionType != PROPULSION_TYPE_LIFT) && moveBlocked(psDroid))
	{
		objTrace(psDroid->id, "status: id %d blocked", (int)psDroid->id);
		psDroid->movement.status = MOVETURN;
	}

	//	// If were in drive mode and the droid is a follower then stop it when it gets within
	//	// range of the driver.
	//	if(driveIsFollower(psDroid)) {
	//		if(DoFollowRangeCheck) {
	//			if(driveInDriverRange(psDroid)) {
	//				psDroid->sMove.Status = MOVEINACTIVE;
	////				ClearFollowRangeCheck = true;
	//			} else {
	//				AllInRange = false;
	//			}
	//		}
	//	}

	/* If it's sitting in water then it's got to go with the flow! */
	if (worldOnMap(psDroid->pos.x, psDroid->pos.y) && terrainType(
		mapTile(map_coord(psDroid->pos.x), map_coord(psDroid->pos.y))) == TER_WATER)
	{
		updateDroidOrientation(psDroid);
	}

	if (psDroid->movement.status == MOVETURNTOTARGET && psDroid->rot.direction == moveDir)
	{
		if (psPropStats->propulsionType == PROPULSION_TYPE_LIFT)
		{
			psDroid->movement.status = MOVEPOINTTOPOINT;
		}
		else
		{
			psDroid->movement.status = MOVEINACTIVE;
		}
		objTrace(psDroid->id, "MOVETURNTOTARGET complete");
	}

	if (psDroid->periodicalDamageStart != 0 && psDroid->type != DROID_PERSON && psDroid->visibleForLocalDisplay())
	// display-only check for adding effect
	{
		pos.x = psDroid->pos.x + (18 - rand() % 36);
		pos.z = psDroid->pos.y + (18 - rand() % 36);
		pos.y = psDroid->pos.z + (psDroid->sDisplay.imd->max.y / 3);
		addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL, false, nullptr, 0, gameTime - deltaGameTime + 1);
	}

	movePlayAudio(psDroid, bStarted, bStopped, moveSpeed);
	ASSERT(droidOnMap(psDroid), "%s moved off map (%u, %u)->(%u, %u)", droidGetName(psDroid), oldx, oldy,
	       (unsigned)psDroid->pos.x, (unsigned)psDroid->pos.y);
}
