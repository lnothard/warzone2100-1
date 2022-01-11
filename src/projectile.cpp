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

#include <algorithm>
#include <cstring>
#include <functional>

#include "lib/framework/frame.h"
#include "lib/framework/trig.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/sound/audio_id.h"
#include "lib/sound/audio.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/piematrix.h"

#include "objects.h"
#include "move.h"
#include "action.h"
#include "combat.h"
#include "effects.h"
#include "map.h"
#include "order.h"
#include "projectile.h"
#include "visibility.h"
#include "group.h"
#include "cmddroid.h"
#include "feature.h"
#include "loop.h"
#include "scores.h"
#include "display.h"
#include "multiplay.h"
#include "multistat.h"
#include "mapgrid.h"
#include "random.h"
#include "display3d.h"

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <glm/gtx/transform.hpp>


Projectile::Projectile(unsigned id, unsigned player)
  : SimpleObject(id, player)
{
}

PROJECTILE_STATE Projectile::getState() const noexcept
{
  return state;
}

ObjectShape::ObjectShape(int radius)
  : size{radius, radius}
{
}

ObjectShape::ObjectShape(Vector2i size)
  : size{size}
{
}

ObjectShape::ObjectShape(int length, int width)
  : isRectangular{true}, size{length, width}
{
}

int ObjectShape::radius() const
{
  return size.x;
}

// used to create a specific ID for projectile objects to facilitate tracking them.
static const auto ProjectileTrackerID = 0xdead0000;
static auto projectileTrackerIDIncrement = 0;

/* The list of projectiles in play */
static std::vector<Projectile> psProjectileList;

// the last unit that did damage - used by script functions
SimpleObject* g_pProjLastAttacker;

static void proj_ImpactFunc(Projectile* psObj);
static void proj_PostImpactFunc(Projectile* psObj);
static void proj_checkPeriodicalDamage(Projectile* psProj);

static int objectDamage(Damage* psDamage);

//static inline void setProjectileDestination(PROJECTILE* psProj, SimpleObject* psObj)
//{
//	bool bDirect = proj_Direct(psProj->psWStats);
//#if defined( _MSC_VER )
//#pragma warning( push )
//#pragma warning( disable : 4146 ) // warning C4146: unary minus operator applied to unsigned type, result still unsigned
//#endif
//	aiObjectAddExpectedDamage(psProj->psDest, -psProj->expectedDamageCaused, bDirect);
//	// The old target shouldn't be expecting any more damage from this projectile.
//#if defined( _MSC_VER )
//#pragma warning( pop )
//#endif
//	psProj->psDest = psObj;
//	aiObjectAddExpectedDamage(psProj->psDest, psProj->expectedDamageCaused, bDirect);
//	// Let the new target know to say its prayers.
//}

bool Projectile::gfxVisible() const
{
	// already know it is visible
	if (isVisible) {
		return true;
	}

	// you fired it
	if (getPlayer() == selectedPlayer) {
		return true;
	}

	// someone else's structure firing at something you can't see
	if (source != nullptr
      && source->isAlive()
      && dynamic_cast<Structure*>(source)
      && source->getPlayer() != selectedPlayer
      && (target == nullptr
          || !target->isAlive()
          || !target->visibleForLocalDisplay())) {
		return false;
	}

	// Something you cannot see firing at a structure that isn't yours
	if (target != nullptr
      && target->isAlive()
      && dynamic_cast<Structure*>(target)
      && target->getPlayer() != selectedPlayer
      && (source == nullptr
        || !source->visibleForLocalDisplay())) {
		return false;
	}

	// You can see the source
	if (source != nullptr
      && source->isAlive()
      && source->visibleForLocalDisplay()) {
		return true;
	}

	// You can see the destination
	if (target != nullptr
      && target->isAlive()
      && target->visibleForLocalDisplay()) {
		return true;
	}

	return false;
}

void proj_InitSystem()
{
	psProjectileList.clear();
	for (int& x : experienceGain)
	{
		x = 100;
	}
	projectileTrackerIDIncrement = 0;
}

void proj_FreeAllProjectiles()
{
	psProjectileList.clear();
}

/*
 * Relates the quality of the attacker to the quality of the victim.
 * The value returned satisfies the following inequality: 0.5 <= ret/65536 <= 2.0
 */
static unsigned qualityFactor(Droid* psAttacker, Droid* psVictim)
{
	auto powerRatio = (uint64_t)65536 * calcDroidPower(
          psVictim) / calcDroidPower(psAttacker);
	auto pointsRatio = (uint64_t)65536 * calcDroidPoints(
          psVictim) / calcDroidPoints(psAttacker);

	CLIP(powerRatio, 65536 / 2, 65536 * 2);
	CLIP(pointsRatio, 65536 / 2, 65536 * 2);
	return (powerRatio + pointsRatio) / 2;
}

void setExpGain(int player, int gain)
{
	experienceGain[player] = gain;
}

int getExpGain(int player)
{
	return experienceGain[player];
}

Droid* getDesignatorAttackingObject(unsigned player, SimpleObject* target)
{
	const auto psCommander = cmdDroidGetDesignator(player);

	return psCommander != nullptr && 
         psCommander->getAction() == ACTION::ATTACK &&
         psCommander->actionTarget[0] == target
		       ? psCommander
		       : nullptr;
}

void Projectile::updateExperience(unsigned experienceInc)
{
	SimpleObject* psSensor;
	CHECK_PROJECTILE(this);

  /* update droid kills */
  if (auto psDroid = dynamic_cast<Droid*>(this)) {
		// if it is 'droid-on-droid' then modify the experience by the
    // quality factor. only do this in MP so to not unbalance the campaign
		if (target && dynamic_cast<Droid*>(target) && bMultiPlayer) {
			// modify the experience gained by the 'quality factor' of the units
			experienceInc = experienceInc *
                      qualityFactor(psDroid,
                            dynamic_cast<Droid*>(target)) / 65536;
		}
		ASSERT_OR_RETURN(, experienceInc < (int)(2.1 * 65536), "Experience increase out of range");
		psDroid->gain_experience(experienceInc);
		cmdDroidUpdateExperience(psDroid, experienceInc);

		psSensor = orderStateObj(psDroid, ORDER_TYPE::FIRE_SUPPORT);
		if (psSensor && dynamic_cast<Droid*>(psSensor)) {
			dynamic_cast<Droid*>(psSensor)->gainExperience(experienceInc);
		}
	} else if (dynamic_cast<Structure*>(source))
	{
		ASSERT_OR_RETURN(, experienceInc < (int)(2.1 * 65536), "Experience increase out of range");
		psDroid = getDesignatorAttackingObject(source->getPlayer(), target);

		if (psDroid != nullptr) {
			psDroid->gainExperience(experienceInc);
		}
	}
}

void _syncDebugProjectile(const char* function, Projectile const* psProj, char ch)
{
	int list[] =
	{
		ch,

		psProj->player,
		psProj->pos.x, psProj->pos.y, psProj->pos.z,
		psProj->rot.direction, psProj->rot.pitch, psProj->rot.roll,
		psProj->state,
		(int)psProj->expectedDamageCaused,
		(int)psProj->damaged.size(),
	};
	_syncDebugIntList(
		function, "%c projectile = p%d;pos(%d,%d,%d),rot(%d,%d,%d),state%d,expectedDamageCaused%d,numberDamaged%u",
		list, ARRAY_SIZE(list));
}

static int randomVariation(int val)
{
	// Up to ±5% random variation.
	return (int64_t)val * (95000 + gameRand(10001)) / 100000;
}

int projCalcIndirectVelocities(int32_t dx, const int32_t dz, int32_t v,
                               int32_t* vx, int32_t* vz, int min_angle)
{
	// Find values of vx and vz, which solve the equations:
	// dz = -1/2 g t² + vz t
	// dx = vx t
	// v² = vx² + vz²
	// Increases v, if needed for there to be a solution. Decreases v, if needed for vz > 0.
	// Randomly changes v by up to 2.5%, so the shots don't all follow the same path.

	const int32_t g = ACC_GRAVITY; // In units/s².
	int32_t a = randomVariation(v * v) - dz * g; // In units²/s².
	uint64_t b = g * g * ((uint64_t)dx * dx + (uint64_t)dz * dz);
	// In units⁴/s⁴. Casting to uint64_t does sign extend the int32_t.
	int64_t c = (uint64_t)a * a - b; // In units⁴/s⁴.
	if (c < 0)
	{
		// Must increase velocity, target too high. Find the smallest possible a (which corresponds to the smallest possible velocity).

		a = i64Sqrt(b) + 1; // Still in units²/s². Adding +1, since i64Sqrt rounds down.
		c = (uint64_t)a * a - b; // Still in units⁴/s⁴. Should be 0, plus possible rounding errors.
	}

	int32_t t = MAX(1, iSqrt(2 * (a - i64Sqrt(c))) * (GAME_TICKS_PER_SEC / g));
	// In ticks. Note that a - √c ≥ 0, since c ≤ a². Try changing the - to +, and watch the mini-rockets.
	*vx = dx * GAME_TICKS_PER_SEC / t; // In units/sec.
	*vz = dz * GAME_TICKS_PER_SEC / t + g * t / (2 * GAME_TICKS_PER_SEC); // In units/sec.

	STATIC_ASSERT(GAME_TICKS_PER_SEC / ACC_GRAVITY * ACC_GRAVITY == GAME_TICKS_PER_SEC);
	// On line that calculates t, must cast iSqrt to uint64_t, and remove brackets around TICKS_PER_SEC/g, if changing ACC_GRAVITY.

	if (*vz < 0)
	{
		// Don't want to shoot downwards, reduce velocity and let gravity take over.
		t = MAX(1, i64Sqrt(-2 * dz * (uint64_t)GAME_TICKS_PER_SEC * GAME_TICKS_PER_SEC / g)); // Still in ticks.
		*vx = dx * GAME_TICKS_PER_SEC / t; // Still in units/sec.
		*vz = 0; // Still in units/sec. (Wouldn't really matter if it was pigeons/inch, since it's 0 anyway.)
	}

	/* CorvusCorax: Check against min_angle */
	if (iAtan2(*vz, *vx) < min_angle)
	{
		/* set pitch to pass terrain */
		// tan(min_angle)=mytan/65536
		int64_t mytan = ((int64_t)iSin(min_angle) * 65536) / iCos(min_angle);
		t = MAX(
			1, i64Sqrt(2 * ((int64_t)dx * mytan - dz * 65536) * (int64_t)GAME_TICKS_PER_SEC * GAME_TICKS_PER_SEC / (
				int64_t)(g * 65536))); // Still in ticks.
		*vx = dx * GAME_TICKS_PER_SEC / t;
		// mytan=65536*vz/vx
		*vz = (mytan * (*vx)) / 65536;
	}

	return t;
}

bool proj_SendProjectile(Weapon* psWeap, SIMPLE_OBJECT* psAttacker, int player,
                         Vector3i target, SimpleObject* psTarget, bool bVisible,
                         int weapon_slot)
{
	return proj_SendProjectileAngled(psWeap, psAttacker, player, target, psTarget,
                                   bVisible, weapon_slot, 0, gameTime - 1);
}

bool proj_SendProjectileAngled(Weapon* psWeap, SIMPLE_OBJECT* psAttacker, int player,
                               Vector3i target, SimpleObject* psTarget, bool bVisible,
                               int weapon_slot, int min_angle, unsigned fireTime)
{
	WeaponStats* psStats = &asWeaponStats[psWeap->nStat];

	ASSERT_OR_RETURN(false, psWeap->nStat < numWeaponStats, "Invalid range referenced for numWeaponStats, %d > %d",
	                 psWeap->nStat, numWeaponStats);
	ASSERT_OR_RETURN(false, psStats != nullptr, "Invalid weapon stats");
	ASSERT_OR_RETURN(false, psTarget == nullptr || !psTarget->died, "Aiming at dead target!");

	auto psProj = std::make_unique<Projectile>(
          ProjectileTrackerID + ++projectileTrackerIDIncrement, player);

	/* get muzzle offset */
	if (psAttacker == nullptr)
	{
		// if there isn't an attacker just start at the target position
		// NB this is for the script function to fire the las sats
		psProj->origin = target;
	}
	else if (psAttacker->type == OBJ_DROID && weapon_slot >= 0)
	{
		calcDroidMuzzleLocation((Droid*)psAttacker, &psProj->origin, weapon_slot);
		/*update attack runs for VTOL droid's each time a shot is fired*/
		updateVtolAttackRun((Droid*)psAttacker, weapon_slot);
	}
	else if (psAttacker->type == OBJ_STRUCTURE && weapon_slot >= 0)
	{
		calcStructureMuzzleLocation((Structure*)psAttacker, &psProj->origin, weapon_slot);
	}
	else // incase anything wants a projectile
	{
		psProj->origin = psAttacker->pos;
	}

	/* Initialise the structure */
	psProj->weaponStats = psStats;

	psProj->pos = psProj->origin;
	psProj->destination = target;

	psProj->isVisible = false;

	// Must set ->psDest and ->expectedDamageCaused before first call to setProjectileDestination().
	psProj->target = nullptr;
	psProj->expectedDamageCaused = objGuessFutureDamage(psStats, player, psTarget);
	setProjectileDestination(psProj, psTarget);
	// Updates expected damage of psProj->psDest, using psProj->expectedDamageCaused.

	/*
	When we have been created by penetration (spawned from another projectile),
	we shall live no longer than the original projectile may have lived
	*/
	if (psAttacker && psAttacker->type == OBJ_PROJECTILE)
	{
		auto psOldProjectile = (Projectile*)psAttacker;
		psProj->born = psOldProjectile->born;
		psProj->origin = psOldProjectile->origin;

		psProj->prevSpacetime.time = psOldProjectile->time; // Have partially ticked already.
		psProj->time = gameTime;
		psProj->prevSpacetime.time -= psProj->prevSpacetime.time == psProj->time;
		// Times should not be equal, for interpolation.

		setProjectileSource(psProj, psOldProjectile->source);
		psProj->damaged = psOldProjectile->damaged;

		// TODO Should finish the tick, when penetrating.
	}
	else
	{
		psProj->born = fireTime; // Born at the start of the tick.

		psProj->prevSpacetime.time = fireTime;
		psProj->time = psProj->prevSpacetime.time;

		setProjectileSource(psProj, psAttacker);
	}

	if (psTarget)
	{
		int maxHeight = establishTargetHeight(psTarget);
		int minHeight = std::min(
			std::max(maxHeight + 2 * LINE_OF_FIRE_MINIMUM - areaOfFire(psAttacker, psTarget, weapon_slot, true), 0),
			maxHeight);
		scoreUpdateVar(WD_SHOTS_ON_TARGET);

		psProj->destination.z = psTarget->pos.z + minHeight + gameRand(std::max(maxHeight - minHeight, 1));
		/* store visible part (LOCK ON this part for homing :) */
		psProj->partVisible = maxHeight - minHeight;
	}
	else
	{
		psProj->destination.z = target.z + LINE_OF_FIRE_MINIMUM;
		scoreUpdateVar(WD_SHOTS_OFF_TARGET);
	}

	Vector3i deltaPos = psProj->destination - psProj->origin;

	/* roll never set */
	psProj->rot.roll = 0;

	psProj->rot.direction = iAtan2(deltaPos.xy());


	// Get target distance, horizontal distance only.
	uint32_t dist = iHypot(deltaPos.xy());

	if (proj_Direct(psStats))
	{
		psProj->rot.pitch = iAtan2(deltaPos.z, dist);
	}
	else
	{
		/* indirect */
		projCalcIndirectVelocities(dist, deltaPos.z, psStats->flightSpeed, &psProj->vXY, &psProj->vZ, min_angle);
		psProj->rot.pitch = iAtan2(psProj->vZ, psProj->vXY);
	}
	psProj->state = PROJ_INFLIGHT;

	// If droid or structure, set muzzle pitch.
	if (psAttacker != nullptr && weapon_slot >= 0)
	{
		if (psAttacker->type == OBJ_DROID)
		{
			((Droid*)psAttacker)->asWeaps[weapon_slot].rotation.pitch = psProj->rot.pitch;
		}
		else if (psAttacker->type == OBJ_STRUCTURE)
		{
			((Structure*)psAttacker)->asWeaps[weapon_slot].rotation.pitch = psProj->rot.pitch;
		}
	}

	/* put the projectile object in the global list */
	psProjectileList.push_back(psProj);

	/* play firing audio */
	// only play if either object is visible, i know it's a bit of a hack, but it avoids the problem
	// of having to calculate real visibility values for each projectile.
	if (bVisible || gfxVisible(psProj))
	{
		// note that the projectile is visible
		psProj->isVisible = true;

		if (psStats->iAudioFireID != NO_SOUND)
		{
			if (psProj->source)
			{
				/* firing sound emitted from source */
				audio_PlayObjDynamicTrack(psProj->source, psStats->iAudioFireID, nullptr);
				/* GJ HACK: move howitzer sound with shell */
				if (psStats->weaponSubClass == WSC_HOWITZERS)
				{
					audio_PlayObjDynamicTrack(psProj, ID_SOUND_HOWITZ_FLIGHT, nullptr);
				}
			}
			//don't play the sound for a LasSat in multiPlayer
			else if (!(bMultiPlayer && psStats->weaponSubClass == WSC_LAS_SAT))
			{
				audio_PlayObjStaticTrack(psProj, psStats->iAudioFireID);
			}
		}
	}

	if (psAttacker != nullptr && !proj_Direct(psStats))
	{
		//check for Counter Battery Sensor in range of target
		counterBatteryFire(castBaseObject(psAttacker), psTarget);
	}

	syncDebugProjectile(psProj, '*');

	CHECK_PROJECTILE(psProj);

	return true;
}

/***************************************************************************/

static INTERVAL intervalIntersection(INTERVAL i1, INTERVAL i2)
{
	INTERVAL ret = {MAX(i1.begin, i2.begin), MIN(i1.end, i2.end)};
	return ret;
}
//
//static bool intervalEmpty(INTERVAL i)
//{
//	return i.begin >= i.end;
//}

static INTERVAL collisionZ(int32_t z1, int32_t z2, int32_t height)
{
	INTERVAL ret = {-1, -1};
	if (z1 > z2)
	{
		z1 *= -1;
		z2 *= -1;
	}

	if (z1 > height || z2 < -height)
	{
		return ret; // No collision between time 1 and time 2.
	}

	if (z1 == z2)
	{
		if (z1 >= -height && z1 <= height)
		{
			ret.begin = 0;
			ret.end = 1024;
		}
		return ret;
	}

	ret.begin = 1024 * (-height - z1) / (z2 - z1);
	ret.end = 1024 * (height - z1) / (z2 - z1);
	return ret;
}

//static INTERVAL collisionXY(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius)
//{
//	// Solve (1 - t)v1 + t v2 = r.
//	int32_t dx = x2 - x1, dy = y2 - y1;
//	int64_t a = (int64_t)dx * dx + (int64_t)dy * dy; // a = (v2 - v1)²
//	int64_t b = (int64_t)x1 * dx + (int64_t)y1 * dy; // b = v1(v2 - v1)
//	int64_t c = (int64_t)x1 * x1 + (int64_t)y1 * y1 - (int64_t)radius * radius; // c = v1² - r²
//	// Equation to solve is now a t^2 + 2 b t + c = 0.
//	int64_t d = b * b - a * c; // d = b² - a c
//	// Solution is (-b ± √d)/a.
//	INTERVAL empty = {-1, -1};
//	INTERVAL full = {0, 1024};
//	INTERVAL ret;
//	if (d < 0)
//	{
//		return empty; // Missed.
//	}
//	if (a == 0)
//	{
//		return c < 0 ? full : empty; // Not moving. See if inside the target.
//	}
//
//	int32_t sd = i64Sqrt(d);
//	ret.begin = MAX(0, 1024 * (-b - sd) / a);
//	ret.end = MIN(1024, 1024 * (-b + sd) / a);
//	return ret;
//}

static int collisionXYZ(Vector3i v1, Vector3i v2, ObjectShape shape, int32_t height)
{
	auto i = collisionZ(v1.z, v2.z, height);
	if (!intervalEmpty(i)) // Don't bother checking x and y unless z passes.
	{
		if (shape.isRectangular)
		{
			i = intervalIntersection(i, collisionZ(v1.x, v2.x, shape.size.x));
			if (!intervalEmpty(i)) // Don't bother checking y unless x and z pass.
			{
				i = intervalIntersection(i, collisionZ(v1.y, v2.y, shape.size.y));
			}
		}
		else // Else is circular.
		{
			i = intervalIntersection(i, collisionXY(v1.x, v1.y, v2.x, v2.y, shape.radius()));
		}

		if (!intervalEmpty(i))
		{
			return MAX(0, i.begin);
		}
	}
	return -1;
}

static void proj_InFlightFunc(Projectile* psProj)
{
	/* we want a delay between Las-Sats firing and actually hitting in multiPlayer
	magic number but that's how long the audio countdown message lasts! */
	const unsigned int LAS_SAT_DELAY = 4;
	SimpleObject* closestCollisionObject = nullptr;
	Spacetime closestCollisionSpacetime;

	CHECK_PROJECTILE(psProj);

	int timeSoFar = gameTime - psProj->born;

	psProj->time = gameTime;
	int deltaProjectileTime = psProj->time - psProj->prevSpacetime.time;

	WeaponStats* psStats = psProj->weaponStats;
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid weapon stats pointer");

	/* we want a delay between Las-Sats firing and actually hitting in multiPlayer
	magic number but that's how long the audio countdown message lasts! */
	if (bMultiPlayer && psStats->weaponSubClass == WSC_LAS_SAT &&
		(unsigned)timeSoFar < LAS_SAT_DELAY * GAME_TICKS_PER_SEC)
	{
		return;
	}

	/* Calculate movement vector: */
	int32_t currentDistance = 0;
	switch (psStats->movementModel)
	{
	case MM_DIRECT: // Go in a straight line.
		{
			Vector3i delta = psProj->destination - psProj->origin;
			if (psStats->weaponSubClass == WSC_LAS_SAT)
			{
				// LASSAT doesn't have a z
				delta.z = 0;
			}
			int targetDistance = std::max(iHypot(delta.xy()), 1);
			currentDistance = timeSoFar * psStats->flightSpeed / GAME_TICKS_PER_SEC;
			psProj->pos = psProj->origin + delta * currentDistance / targetDistance;
			break;
		}
	case MM_INDIRECT: // Ballistic trajectory.
		{
			Vector3i delta = psProj->destination - psProj->origin;
			delta.z = (psProj->vZ - (timeSoFar * ACC_GRAVITY / (GAME_TICKS_PER_SEC * 2))) * timeSoFar /
				GAME_TICKS_PER_SEC; // '2' because we reach our highest point in the mid of flight, when "vZ is 0".
			int targetDistance = std::max(iHypot(delta.xy()), 1);
			currentDistance = timeSoFar * psProj->vXY / GAME_TICKS_PER_SEC;
			psProj->pos = psProj->origin + delta * currentDistance / targetDistance;
			psProj->pos.z = psProj->origin.z + delta.z; // Use raw z value.
			psProj->rot.pitch = iAtan2(psProj->vZ - (timeSoFar * ACC_GRAVITY / GAME_TICKS_PER_SEC), psProj->vXY);
			break;
		}
	case MM_HOMINGDIRECT: // Fly towards target, even if target moves.
	case MM_HOMINGINDIRECT: // Fly towards target, even if target moves. Avoid terrain.
		{
			if (psProj->target != nullptr)
			{
				if (psStats->movementModel == MM_HOMINGDIRECT)
				{
					// If it's homing and has a target (not a miss)...
					// Home at the centre of the part that was visible when firing.
					psProj->destination = psProj->target->pos + Vector3i(
						0, 0, establishTargetHeight(psProj->target) - psProj->partVisible / 2);
				}
				else
				{
					psProj->destination = psProj->target->pos + Vector3i(0, 0, establishTargetHeight(psProj->target) / 2);
				}
				Droid* targetDroid = castDroid(psProj->target);
				if (targetDroid != nullptr)
				{
					// Do target prediction.
					Vector3i delta = psProj->destination - psProj->pos;
					int flightTime = iHypot(delta.xy()) * GAME_TICKS_PER_SEC / psStats->flightSpeed;
					psProj->destination += Vector3i(iSinCosR(targetDroid->movement.moveDir,
					                                 std::min<int>(targetDroid->movement.speed,
					                                               psStats->flightSpeed * 3 / 4) * flightTime /
					                                 GAME_TICKS_PER_SEC), 0);
				}
				psProj->destination.x = clip(psProj->destination.x, 0, world_coord(mapWidth) - 1);
				psProj->destination.y = clip(psProj->destination.y, 0, world_coord(mapHeight) - 1);
			}
			if (psStats->movementModel == MM_HOMINGINDIRECT)
			{
				if (psProj->target == nullptr)
				{
					psProj->destination.z = map_Height(psProj->pos.xy()) - 1;
					// Target missing, so just home in on the ground under where the target was.
				}
				int horizontalTargetDistance = iHypot((psProj->destination - psProj->pos).xy());
				int terrainHeight = std::max(map_Height(psProj->pos.xy()),
				                             map_Height(psProj->pos.xy() + iSinCosR(
					                             iAtan2((psProj->destination - psProj->pos).xy()),
					                             psStats->flightSpeed * 2 * deltaProjectileTime / GAME_TICKS_PER_SEC)));
				int desiredMinHeight = terrainHeight +
					std::min(horizontalTargetDistance / 4, HOMINGINDIRECT_HEIGHT_MIN);
				int desiredMaxHeight = std::max(psProj->destination.z, terrainHeight + HOMINGINDIRECT_HEIGHT_MAX);
				int heightError = psProj->pos.z - clip(psProj->pos.z, desiredMinHeight, desiredMaxHeight);
				psProj->destination.z -= horizontalTargetDistance * heightError * 2 / HOMINGINDIRECT_HEIGHT_MIN;
			}
			Vector3i delta = psProj->destination - psProj->pos;
			int targetDistance = std::max(iHypot(delta), 1);
			if (psProj->target == nullptr && targetDistance < 10000 && psStats->movementModel == MM_HOMINGDIRECT)
			{
				psProj->destination = psProj->pos + delta * 10; // Target missing, so just keep going in a straight line.
			}
			currentDistance = timeSoFar * psStats->flightSpeed / GAME_TICKS_PER_SEC;
			Vector3i step = quantiseFraction(delta * int32_t(psStats->flightSpeed), GAME_TICKS_PER_SEC * targetDistance,
			                                 psProj->time, psProj->prevSpacetime.time);
			if (psStats->movementModel == MM_HOMINGINDIRECT && psProj->target != nullptr)
			{
				for (int tries = 0; tries < 10 && map_LineIntersect(psProj->prevSpacetime.pos, psProj->pos + step,
				                                                    iHypot(step)) < targetDistance - 1u; ++tries)
				{
					psProj->destination.z += iHypot((psProj->destination - psProj->pos).xy());
					// Would collide with terrain this tick, change trajectory.
					// Recalculate delta, targetDistance and step.
					delta = psProj->destination - psProj->pos;
					targetDistance = std::max(iHypot(delta), 1);
					step = quantiseFraction(delta * int32_t(psStats->flightSpeed), GAME_TICKS_PER_SEC * targetDistance,
					                        psProj->time, psProj->prevSpacetime.time);
				}
			}
			psProj->pos += step;
			psProj->rot.direction = iAtan2(delta.xy());
			psProj->rot.pitch = iAtan2(delta.z, targetDistance);
			break;
		}
	}

	closestCollisionSpacetime.time = 0xFFFFFFFF;

	/* Check nearby objects for possible collisions */
	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(psProj->pos.x, psProj->pos.y, PROJ_NEIGHBOUR_RANGE);
	for (GridIterator gi = gridList.begin(); gi != gridList.end(); ++gi)
	{
		SimpleObject* psTempObj = *gi;
		CHECK_OBJECT(psTempObj);

		if (std::find(psProj->damaged.begin(), psProj->damaged.end(), psTempObj) != psProj->damaged.end())
		{
			// Dont damage one target twice
			continue;
		}
		else if (psTempObj->died)
		{
			// Do not damage dead objects further
			ASSERT(psTempObj->type < OBJ_NUM_TYPES, "Bad pointer! type=%u", psTempObj->type);
			continue;
		}
		else if (psTempObj->type == OBJ_FEATURE && !((Feature*)psTempObj)->psStats->damageable)
		{
			// Ignore oil resources, artifacts and other pickups
			continue;
		}
		else if (aiCheckAlliances(psTempObj->player, psProj->player) && psTempObj != psProj->target)
		{
			// No friendly fire unless intentional
			continue;
		}
		else if (!(psStats->surfaceToAir & SHOOT_ON_GROUND) &&
			(psTempObj->type == OBJ_STRUCTURE ||
				psTempObj->type == OBJ_FEATURE ||
				(psTempObj->type == OBJ_DROID && !isFlying((Droid*)psTempObj))
			))
		{
			// AA weapons should not hit buildings and non-vtol droids
			continue;
		}

		Vector3i psTempObjPrevPos = isDroid(psTempObj) ? castDroid(psTempObj)->previousLocation.pos : psTempObj->pos;

		const Vector3i diff = psProj->pos - psTempObj->pos;
		const Vector3i prevDiff = psProj->prevSpacetime.pos - psTempObjPrevPos;
		const unsigned int targetHeight = establishTargetHeight(psTempObj);
		const ObjectShape targetShape = establishTargetShape(psTempObj);
		const int32_t collision = collisionXYZ(prevDiff, diff, targetShape, targetHeight);
		const uint32_t collisionTime = psProj->prevSpacetime.time + (psProj->time - psProj->prevSpacetime.time) *
			collision / 1024;

		if (collision >= 0 && collisionTime < closestCollisionSpacetime.time)
		{
			// We hit!
			closestCollisionSpacetime = interpolateObjectSpacetime(psProj, collisionTime);
			closestCollisionObject = psTempObj;

			// Keep testing for more collisions, in case there was a closer target.
		}
	}

	unsigned terrainIntersectTime = map_LineIntersect(psProj->prevSpacetime.pos, psProj->pos,
	                                                  psProj->time - psProj->prevSpacetime.time);
	if (terrainIntersectTime != UINT32_MAX)
	{
		const uint32_t collisionTime = psProj->prevSpacetime.time + terrainIntersectTime;
		if (collisionTime < closestCollisionSpacetime.time)
		{
			// We hit the terrain!
			closestCollisionSpacetime = interpolateObjectSpacetime(psProj, collisionTime);
			closestCollisionObject = nullptr;
		}
	}

	if (closestCollisionSpacetime.time != 0xFFFFFFFF)
	{
		// We hit!
		setSpacetime(psProj, closestCollisionSpacetime);
		psProj->time = std::max(psProj->time, gameTime - deltaGameTime + 1);
		// Make sure .died gets set in the interval [gameTime - deltaGameTime + 1; gameTime].
		if (psProj->time == psProj->prevSpacetime.time)
		{
			--psProj->prevSpacetime.time;
		}
		setProjectileDestination(psProj, closestCollisionObject); // We hit something.

		// Buildings and terrain cannot be penetrated and we need a penetrating weapon, and projectile should not have already travelled further than 1.25 * maximum range.
		if (closestCollisionObject != nullptr && closestCollisionObject->type == OBJ_DROID && psStats->penetrate &&
			currentDistance < static_cast<int>(1.25 * proj_GetLongRange(psStats, psProj->player)))
		{
			Weapon asWeap;
			asWeap.nStat = psStats - asWeaponStats;

			// Assume we damaged the chosen target
			psProj->damaged.push_back(closestCollisionObject);

			proj_SendProjectile(&asWeap, psProj, psProj->player, psProj->destination, nullptr, true, -1);
		}

		psProj->state = PROJ_IMPACT;

		return;
	}

	if (currentDistance * 100 >= proj_GetLongRange(psStats, psProj->player) * psStats->distanceExtensionFactor)
	{
		// We've travelled our maximum range.
		psProj->state = PROJ_IMPACT;
		setProjectileDestination(psProj, nullptr); /* miss registered if NULL target */
		return;
	}

	/* Paint effects if visible */
	if (gfxVisible(psProj))
	{
		uint32_t effectTime;
		for (effectTime = ((psProj->prevSpacetime.time + 31) & ~31); effectTime < psProj->time; effectTime += 32)
		{
			Spacetime st = interpolateObjectSpacetime(psProj, effectTime);
			Vector3i posFlip = st.pos.xzy();
			switch (psStats->weaponSubClass)
			{
			case WSC_FLAME:
				posFlip.z -= 8; // Why?
				effectGiveAuxVar(PERCENT(currentDistance, proj_GetLongRange(psStats, psProj->player)));
				addEffect(&posFlip, EFFECT_EXPLOSION, EXPLOSION_TYPE_FLAMETHROWER, false, nullptr, 0, effectTime);
				break;
			case WSC_COMMAND:
			case WSC_ELECTRONIC:
			case WSC_EMP:
				posFlip.z -= 8; // Why?
				effectGiveAuxVar(PERCENT(currentDistance, proj_GetLongRange(psStats, psProj->player)) / 2);
				addEffect(&posFlip, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER, false, nullptr, 0, effectTime);
				break;
			case WSC_ROCKET:
			case WSC_MISSILE:
			case WSC_SLOWROCKET:
			case WSC_SLOWMISSILE:
				posFlip.z += 8; // Why?
				addEffect(&posFlip, EFFECT_SMOKE, SMOKE_TYPE_TRAIL, false, nullptr, 0, effectTime);
				break;
			default:
				// Add smoke trail to indirect weapons, even if firing directly.
				if (!proj_Direct(psStats))
				{
					posFlip.z += 4; // Why?
					addEffect(&posFlip, EFFECT_SMOKE, SMOKE_TYPE_TRAIL, false, nullptr, 0, effectTime);
				}
			// Otherwise no effect.
				break;
			}
		}
	}
}

/***************************************************************************/

static void proj_ImpactFunc(Projectile* psObj)
{
	WeaponStats* psStats;
	SDWORD iAudioImpactID;
	int32_t relativeDamage;
	Vector3i position, scatter;
	iIMDShape* imd;
	SimpleObject* temp;

	ASSERT_OR_RETURN(, psObj != nullptr, "Invalid pointer");
	CHECK_PROJECTILE(psObj);

	psStats = psObj->weaponStats;
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid weapon stats pointer");

	// note the attacker if any
	g_pProjLastAttacker = psObj->source;

	/* play impact audio */
	if (gfxVisible(psObj))
	{
		if (psStats->iAudioImpactID == NO_SOUND)
		{
			/* play richochet if MG */
			if (psObj->target != nullptr && psStats->weaponSubClass == WSC_MGUN
          && ONEINTHREE)
			{
				iAudioImpactID = ID_SOUND_RICOCHET_1 + (rand() % 3);
				audio_PlayStaticTrack(psObj->target->pos.x, psObj->target->pos.y, iAudioImpactID);
			}
		}
		else
		{
			audio_PlayStaticTrack(psObj->pos.x, psObj->pos.y, psStats->iAudioImpactID);
		}

		/* Shouldn't need to do this check but the stats aren't all at a value yet... */ // FIXME
		if (psStats->upgraded[psObj->player].periodicalDamageRadius != 0 && psStats->upgraded[psObj->player].
			periodicalDamageTime != 0)
		{
			position.x = psObj->pos.x;
			position.z = psObj->pos.y; // z = y [sic] intentional
			position.y = map_Height(position.x, position.z);
			effectGiveAuxVar(psStats->upgraded[psObj->player].periodicalDamageRadius);
			effectGiveAuxVarSec(psStats->upgraded[psObj->player].periodicalDamageTime);
			addEffect(&position, EFFECT_FIRE, FIRE_TYPE_LOCALISED, false, nullptr, 0, psObj->time);
		}

		// may want to add both a fire effect and the las sat effect
		if (psStats->weaponSubClass == WSC_LAS_SAT)
		{
			position.x = psObj->pos.x;
			position.z = psObj->pos.y; // z = y [sic] intentional
			position.y = map_Height(position.x, position.z);
			addEffect(&position, EFFECT_SAT_LASER, SAT_LASER_STANDARD, false, nullptr, 0, psObj->time);

			if (clipXY(psObj->pos.x, psObj->pos.y))
			{
				shakeStart(1800); // takes out lots of stuff so shake length is greater
			}
		}
	}

	if (psStats->upgraded[psObj->player].periodicalDamageRadius && psStats->upgraded[psObj->player].periodicalDamageTime)
	{
		tileSetFire(psObj->pos.x, psObj->pos.y, psStats->upgraded[psObj->player].periodicalDamageTime);
	}

	// Set the effects position and radius
	position.x = psObj->pos.x;
	position.z = psObj->pos.y; // z = y [sic] intentional
	position.y = psObj->pos.z; // y = z [sic] intentional
	scatter.x = psStats->upgraded[psObj->player].radius;
	scatter.y = 0;
	scatter.z = psStats->upgraded[psObj->player].radius;

	// If the projectile missed its target (or the target died)
	if (psObj->target == nullptr)
	{
		if (gfxVisible(psObj))
		{
			// Get if we are facing or not
			EFFECT_TYPE facing = (psStats->facePlayer ? EXPLOSION_TYPE_SPECIFIED : EXPLOSION_TYPE_NOT_FACING);

			// The graphic to show depends on if we hit water or not
			if (terrainType(mapTile(map_coord(psObj->pos.x), map_coord(psObj->pos.y))) == TER_WATER)
			{
				imd = psStats->pWaterHitGraphic;
			}
			// We did not hit water, the regular miss graphic will do the trick
			else
			{
				imd = psStats->pTargetMissGraphic;
			}

			addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, facing, true, imd, psStats->numExplosions,
			               psStats->lightWorld, psStats->effectSize, psObj->time);

			// If the target was a VTOL hit in the air add smoke
			if ((psStats->surfaceToAir & SHOOT_IN_AIR)
				&& !(psStats->surfaceToAir & SHOOT_ON_GROUND))
			{
				addMultiEffect(&position, &scatter, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 3, 0, 0,
				               psObj->time);
			}
		}
	}
	// The projectile hit its intended target
	else
	{
		CHECK_OBJECT(psObj->target);

		if (psObj->target->type == OBJ_FEATURE
			&& ((Feature*)psObj->target)->psStats->damageable == 0)
		{
			debug(LOG_NEVER, "proj_ImpactFunc: trying to damage non-damageable target,projectile removed");
			psObj->state = PROJ_INACTIVE;
			return;
		}

		if (gfxVisible(psObj))
		{
			// Get if we are facing or not
			EFFECT_TYPE facing = (psStats->facePlayer ? EXPLOSION_TYPE_SPECIFIED : EXPLOSION_TYPE_NOT_FACING);

			// If we hit a VTOL with an AA gun use the miss graphic and add some smoke
			if ((psStats->surfaceToAir & SHOOT_IN_AIR)
				&& !(psStats->surfaceToAir & SHOOT_ON_GROUND)
				&& psStats->weaponSubClass == WSC_AAGUN)
			{
				imd = psStats->pTargetMissGraphic;
				addMultiEffect(&position, &scatter, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING, false, nullptr, 3, 0, 0,
				               psObj->time);
			}
			// Otherwise we just hit it plain and simple
			else
			{
				imd = psStats->pTargetHitGraphic;
			}

			addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, facing, true, imd, psStats->numExplosions,
			               psStats->lightWorld, psStats->effectSize, psObj->time);
		}

		// Check for electronic warfare damage where we know the subclass and source
		if (proj_Direct(psStats)
			&& psStats->weaponSubClass == WSC_ELECTRONIC
			&& psObj->source)
		{
			// If we did enough `damage' to capture the target
			if (electronicDamage(psObj->target,
			                     calcDamage(weaponDamage(psStats, psObj->player), psStats->weaponEffect, psObj->target),
			                     psObj->player))
			{
				switch (psObj->source->type)
				{
				case OBJ_DROID:
					((Droid*)psObj->source)->order.type = DORDER_NONE;
					actionDroid((Droid*)(psObj->source), DACTION_NONE);
					break;

				case OBJ_STRUCTURE:
					((Structure*)psObj->source)->psTarget[0] = nullptr;
					break;

				// This is only here to prevent the compiler from producing
				// warnings for unhandled enumeration values
				default:
					break;
				}
			}
		}
		// Else it is just a regular weapon (direct or indirect)
		else
		{
			// Calculate the damage the weapon does to its target
			unsigned int damage = calcDamage(weaponDamage(psStats, psObj->player), psStats->weaponEffect,
			                                 psObj->target);

			// If we are in a multi-player game and the attacker is our responsibility
			if (bMultiPlayer && psObj->source)
			{
				updateMultiStatsDamage(psObj->source->player, psObj->target->player, damage);
			}

			debug(LOG_NEVER, "Damage to object %d, player %d\n",
            psObj->target->id, psObj->target->player);

			struct Damage sDamage = {
				psObj,
				psObj->target,
				damage,
				psStats->weaponClass,
				psStats->weaponSubClass,
				psObj->time,
				false,
				(int)psStats->upgraded[psObj->player].minimumDamage
			};

			// Damage the object
			relativeDamage = objectDamage(&sDamage);

			if (relativeDamage >= 0) // So long as the target wasn't killed
			{
				psObj->damaged.push_back(psObj->target);
			}
		}
	}

	temp = psObj->target;
	setProjectileDestination(psObj, nullptr);
	// The damage has been done, no more damage expected from this projectile. (Ignore periodical damaging.)
	psObj->expectedDamageCaused = 0;
	setProjectileDestination(psObj, temp);

	// If the projectile does no splash damage and does not set fire to things
	if (psStats->upgraded[psObj->player].radius == 0 && psStats->upgraded[psObj->player].periodicalDamageTime == 0)
	{
		psObj->state = PROJ_INACTIVE;
		return;
	}

	if (psStats->upgraded[psObj->player].radius != 0)
	{
		/* An area effect bullet */
		psObj->state = PROJ_POSTIMPACT;

		/* Note when it exploded for the explosion effect */
		psObj->born = gameTime;

		// If projectile impacts a droid start the splash damage from the center of it, else use whatever location the projectile impacts at as the splash center.
		auto destDroid = castDroid(psObj->target);
		Vector3i targetPos = (destDroid != nullptr) ? destDroid->pos : psObj->pos;

		static GridList gridList; // static to avoid allocations.
		gridList = gridStartIterate(targetPos.x, targetPos.y, psStats->upgraded[psObj->player].radius);

		for (auto psCurr : gridList)
		{
			if (psCurr->died)
			{
				ASSERT(psCurr->type < OBJ_NUM_TYPES, "Bad pointer! type=%u", psCurr->type);
				continue; // Do not damage dead objects further.
			}

			if (psCurr == psObj->target)
			{
				continue; // Don't hit main target twice.
			}

			if (psObj->source && psObj->source->player == psCurr->player && psStats->flags.test(
				WEAPON_FLAG_NO_FRIENDLY_FIRE))
			{
				continue; // this weapon does not do friendly damage
			}

			bool bTargetInAir = false;
			bool useSphere = false;
			bool damageable = true;
			switch (psCurr->type)
			{
			case OBJ_DROID:
				bTargetInAir = asPropulsionTypes[asPropulsionStats[((Droid*)psCurr)->asBits[COMP_PROPULSION]].
					propulsionType].travel == AIR && ((Droid*)psCurr)->movement.status != MOVEINACTIVE;
				useSphere = true;
				break;
			case OBJ_STRUCTURE:
				break;
			case OBJ_FEATURE:
				damageable = ((Feature*)psCurr)->psStats->damageable;
				break;
			default: ASSERT(false, "Bad type.");
				continue;
			}

			if (!damageable)
			{
				continue; // Ignore features that are not damageable.
			}
			unsigned targetInFlag = bTargetInAir ? SHOOT_IN_AIR : SHOOT_ON_GROUND;
			if ((psStats->surfaceToAir & targetInFlag) == 0)
			{
				continue; // Target in air, and can't shoot at air, or target on ground, and can't shoot at ground.
			}
			if (useSphere && !Vector3i_InSphere(psCurr->pos, targetPos, psStats->upgraded[psObj->player].radius))
			{
				continue; // Target out of range.
			}
			// The psCurr will get damaged, at this point.
			unsigned damage = calcDamage(weaponRadDamage(psStats, psObj->player), psStats->weaponEffect, psCurr);
			debug(LOG_ATTACK, "Damage to object %d, player %d : %u", psCurr->id, psCurr->player, damage);
			if (bMultiPlayer && psObj->source != nullptr && psCurr->type != OBJ_FEATURE)
			{
				updateMultiStatsDamage(psObj->source->player, psCurr->player, damage);
			}

			struct Damage sDamage = {
				psObj,
				psCurr,
				damage,
				psStats->weaponClass,
				psStats->weaponSubClass,
				psObj->time,
				false,
				(int)psStats->upgraded[psObj->player].minimumDamage
			};

			objectDamage(&sDamage);
		}
	}

	if (psStats->upgraded[psObj->player].periodicalDamageTime != 0)
	{
		/* Periodical damage round */
		/* Periodical damage gets done in the bullet update routine */
		/* Just note when started damaging          */
		psObj->state = PROJ_POSTIMPACT;
		psObj->born = gameTime;
	}
	/* Something was blown up */
}

/***************************************************************************/

static void proj_PostImpactFunc(Projectile* psObj)
{
	ASSERT_OR_RETURN(, psObj != nullptr, "Invalid pointer");
	CHECK_PROJECTILE(psObj);

	WeaponStats* psStats = psObj->weaponStats;
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid weapon stats pointer");

	int age = gameTime - psObj->born;

	/* Time to finish postimpact effect? */
	if (age > psStats->radiusLife && age > psStats->upgraded[psObj->player].periodicalDamageTime)
	{
		psObj->state = PROJ_INACTIVE;
		return;
	}

	/* Periodical damage effect */
	if (psStats->upgraded[psObj->player].periodicalDamageTime > 0)
	{
		/* See if anything is in the fire and damage it periodically */
		proj_checkPeriodicalDamage(psObj);
	}
}

void Projectile::update()
{
	CHECK_PROJECTILE(this);
	syncDebugProjectile(this, '<');

	prevSpacetime = getSpacetime();

	// see if any of the stored objects have died
	// since the projectile was created
	if (source && source->died) {
		syncDebugObject(source, '-');
		setProjectileSource(this, nullptr);
	}
	if (target && target->died) {
		syncDebugObject(target, '-');
		setProjectileDestination(this, nullptr);
	}

	// remove dead objects from psDamaged.
	damaged.erase(std::remove_if(damaged.begin(), damaged.end(),
                               [](const SimpleObject* psObj)
                                 { return ::isDead(psObj); }), damaged.end());

	// This extra check fixes a crash in cam2, mission1
	if (worldOnMap(psObj->pos.x, psObj->pos.y) == false)
	{
		died = true;
		return;
	}

	switch (state)
	{
	case INFLIGHT:
		proj_InFlightFunc(this);
		if (state != IMPACT) {
			break;
		}
	// fallthrough
	case IMPACT:
		proj_ImpactFunc(this);
		if (state != POSTIMPACT) {
			break;
		}
	// fallthrough
	case POST_IMPACT:
		proj_PostImpactFunc(this);
		break;

	case INACTIVE:
		died = time;
		break;
	}
	syncDebugProjectile(this, '>');
}

// iterate through all projectiles and update their status
void proj_UpdateAll()
{
	// Update all projectiles. Penetrating projectiles may add to psProjectileList.
	std::for_each(psProjectileList.begin(), psProjectileList.end(), std::mem_fn(&Projectile::update));

	// Remove and free dead projectiles.
	psProjectileList.erase(
		std::remove_if(psProjectileList.begin(), psProjectileList.end(), std::mem_fn(&Projectile::deleteIfDead)),
		psProjectileList.end());
}

static void proj_checkPeriodicalDamage(Projectile* psProj)
{
	CHECK_PROJECTILE(psProj);

	// note the attacker if any
	g_pProjLastAttacker = psProj->source;

	WeaponStats* psStats = psProj->weaponStats;

	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(psProj->pos.x, psProj->pos.y, psStats->upgraded[psProj->player].periodicalDamageRadius);
	for (auto psCurr : gridList)
	{
			if (psCurr->died)
		{
			syncDebugObject(psCurr, '-');
			continue; // Do not damage dead objects further.
		}

		if (aiCheckAlliances(psProj->player, psCurr->player))
		{
			continue; // Don't damage your own droids, nor ally droids - unrealistic, but better.
		}

		if (psCurr->type == OBJ_DROID &&
        isVtolDroid((Droid*)psCurr) &&
        ((Droid*)psCurr)->movement.status != MOVEINACTIVE)
		{
			continue; // Can't set flying vtols on fire.
		}

		if (psCurr->type == OBJ_FEATURE && !((Feature*)psCurr)->psStats->damageable)
		{
			continue; // Can't destroy oil wells.
		}

		if (psCurr->periodicalDamageStart != gameTime)
		{
			psCurr->periodicalDamageStart = gameTime;
			psCurr->periodicalDamage = 0; // Reset periodical damage done this tick.
		}
		unsigned damageRate = calcDamage(weaponPeriodicalDamage(psStats, psProj->getPlayer()),
		                                 psStats->periodicalDamageWeaponEffect, psCurr);
		debug(LOG_NEVER, "Periodical damage of %d per second to object %d, player %d\n", damageRate, psCurr->id,
		      psCurr->player);

		struct Damage sDamage = {
			psProj,
			psCurr,
			damageRate,
			psStats->periodicalDamageWeaponClass,
			psStats->periodicalDamageWeaponSubClass,
			gameTime - deltaGameTime / 2 + 1,
			true,
			(int)psStats->upgraded[psProj->getPlayer()].minimumDamage
		};

		objectDamage(&sDamage);
	}
}

/***************************************************************************/

// return whether a weapon is direct or indirect
bool proj_Direct(const WeaponStats* psStats)
{
	ASSERT_OR_RETURN(false, psStats, "Called with NULL weapon");

	switch (psStats->movementModel)
	{
	case MM_DIRECT:
	case MM_HOMINGDIRECT:
		return true;
	case MM_INDIRECT:
	case MM_HOMINGINDIRECT:
		return false;
	}

	return false; // just to satisfy compiler
}

/***************************************************************************/

#define ASSERT_PLAYER_OR_RETURN(retVal, player) \
	ASSERT_OR_RETURN(retVal, player >= 0 && player < MAX_PLAYERS, "Invalid player: %" PRIu32 "", player);

// return the maximum range for a weapon
int proj_GetLongRange(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].maxRange;
}

// return the minimum range for a weapon
int proj_GetMinRange(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].minRange;
}

// return the short range for a weapon
int proj_GetShortRange(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].shortRange;
}

/***************************************************************************/
ObjectShape establishTargetShape(SimpleObject* psTarget)
{
	CHECK_OBJECT(psTarget);

	switch (psTarget->type)
	{
	case OBJ_DROID: // Circular.
		switch (castDroid(psTarget)->type)
		{
		case DROID_WEAPON:
		case DROID_SENSOR:
		case DROID_ECM:
		case DROID_CONSTRUCT:
		case DROID_COMMAND:
		case DROID_REPAIR:
		case DROID_PERSON:
		case DROID_CYBORG:
		case DROID_CYBORG_CONSTRUCT:
		case DROID_CYBORG_REPAIR:
		case DROID_CYBORG_SUPER:
			//Watermelon:'hitbox' size is now based on imd size
			return abs(psTarget->sDisplay.imd->radius) * 2;
		case DROID_DEFAULT:
		case DROID_TRANSPORTER:
		case DROID_SUPERTRANSPORTER:
		default:
			return TILE_UNITS / 4; // how will we arrive at this?
		}
		break;
	case OBJ_STRUCTURE: // Rectangular.
		return castStructure(psTarget)->size() * TILE_UNITS / 2;
	case OBJ_FEATURE: // Rectangular.
		return Vector2i(castFeature(psTarget)->psStats->base_width, castFeature(psTarget)->psStats->base_breadth) *
           TILE_UNITS / 2;
	case OBJ_PROJECTILE: // Circular, but can't happen since a PROJECTILE isn't a SimpleObject.
		//Watermelon 1/2 radius of a droid?
		return TILE_UNITS / 8;
	default:
		break;
	}

	return 0; // Huh?
}

/*the damage depends on the weapon effect and the target propulsion type or
structure strength*/
unsigned calcDamage(unsigned baseDamage, WEAPON_EFFECT weaponEffect, SimpleObject* psTarget)
{
	if (baseDamage == 0) {
		return 0;
	}

	auto damage = baseDamage * 100;

	if (dynamic_cast<Structure*>(psTarget))
	{
		damage += baseDamage * (asStructStrengthModifier[weaponEffect][((Structure*)psTarget)->pStructureType->strength]
			- 100);
	}
	else if (dynamic_cast<Droid*>(psTarget))
	{
		const int propulsion = (asPropulsionStats + ((Droid*)psTarget)->asBits[COMP_PROPULSION])->propulsionType;
		const int body = (asBodyStats + ((Droid*)psTarget)->asBits[COMP_BODY])->size;
		damage += baseDamage * (asWeaponModifier[weaponEffect][propulsion] - 100);
		damage += baseDamage * (asWeaponModifierBody[weaponEffect][body] - 100);
	}

	//Always do at least one damage.
	return MAX(damage / 100, 1);
}

/*
 * A quick explanation about how this function works:
 *  - It returns an integer between 0 and 100 (see note for exceptions);
 *  - this represents the amount of damage inflicted on the droid by the weapon
 *    in relation to its original health.
 *  - e.g. If 100 points of (*actual*) damage were done to a unit who started
 *    off (when first produced) with 400 points then .25 would be returned.
 *  - If the actual damage done to a unit is greater than its remaining points
 *    then the actual damage is clipped: so if we did 200 actual points of
 *    damage to a cyborg with 150 points left the actual damage would be taken
 *    as 150.
 *  - Should sufficient damage be done to destroy/kill a unit then the value is
 *    multiplied by -1, resulting in a negative number. Killed features do not
 *    result in negative numbers.
 */
static int objectDamageDispatch(Damage* psDamage)
{
	switch (psDamage->target->type)
	{
	case OBJ_DROID:
		return droidDamage((Droid*)psDamage->target, psDamage->damage, psDamage->weaponClass, psDamage->weaponSubClass,
                       psDamage->impactTime, psDamage->isDamagePerSecond, psDamage->minDamage);
		break;

	case OBJ_STRUCTURE:
		return structureDamage((Structure*)psDamage->target, psDamage->damage, psDamage->weaponClass,
                           psDamage->weaponSubClass, psDamage->impactTime, psDamage->isDamagePerSecond,
                           psDamage->minDamage);
		break;

	case OBJ_FEATURE:
		return featureDamage((Feature*)psDamage->target, psDamage->damage, psDamage->weaponClass,
                         psDamage->weaponSubClass, psDamage->impactTime, psDamage->isDamagePerSecond,
                         psDamage->minDamage);
		break;

	case OBJ_PROJECTILE:
		ASSERT(!"invalid object type: bullet", "invalid object type: OBJ_PROJECTILE (id=%d)", psDamage->target->id);
		break;

	default:
		ASSERT(!"unknown object type", "unknown object type %d, id=%d", psDamage->target->type, psDamage->target->id);
	}
	return 0;
}

//static bool isFriendlyFire(DAMAGE* psDamage)
//{
//	return psDamage->psProjectile->psDest && psDamage->psProjectile->psSource->player == psDamage->psProjectile->psDest
//		->player;
//}

//static bool shouldIncreaseExperience(DAMAGE* psDamage)
//{
//	return psDamage->psProjectile->psSource && !isFeature(psDamage->psProjectile->psDest) && !isFriendlyFire(psDamage);
//}

static void updateKills(Damage* psDamage)
{
	if (bMultiPlayer)
	{
		updateMultiStatsKills(psDamage->target, psDamage->projectile->source->player);
	}

	if (psDamage->projectile->source->type == OBJ_DROID)
	{
		Droid* psDroid = (Droid*)psDamage->projectile->source;

		psDroid->kills++;

		if (hasCommander(psDroid))
		{
			Droid* psCommander = psDroid->group->psCommander;
			psCommander->kills++;
		}
	}
	else if (psDamage->projectile->source->type == OBJ_STRUCTURE)
	{
		Droid* psCommander = getDesignatorAttackingObject(psDamage->projectile->source->player,
                                                      psDamage->projectile->target);

		if (psCommander != nullptr)
		{
			psCommander->kills++;
		}
	}
}

static int32_t objectDamage(Damage* psDamage)
{
	int32_t relativeDamage = objectDamageDispatch(psDamage);

	if (shouldIncreaseExperience(psDamage))
	{
		proj_UpdateExperience(psDamage->projectile,
                          abs(relativeDamage) * getExpGain(psDamage->projectile->source->player) / 100);

		bool isTargetDestroyed = relativeDamage < 0;

		if (isTargetDestroyed)
		{
			updateKills(psDamage);
		}
	}

	return relativeDamage;
}

/* Returns true if an object has just been hit by an electronic warfare weapon*/
static bool justBeenHitByEW(SimpleObject* psObj)
{
	Droid* psDroid;
	Feature* psFeature;
	Structure* psStructure;

	if (gamePaused())
	{
		return false;
	}

	switch (psObj->type)
	{
	case OBJ_DROID:
		psDroid = (Droid*)psObj;
		if ((gameTime - psDroid->timeLastHit) < ELEC_DAMAGE_DURATION
			&& psDroid->lastHitWeapon == WSC_ELECTRONIC)
		{
			return true;
		}
		break;

	case OBJ_FEATURE:
		psFeature = (Feature*)psObj;
		if ((gameTime - psFeature->timeLastHit) < ELEC_DAMAGE_DURATION)
		{
			return true;
		}
		break;

	case OBJ_STRUCTURE:
		psStructure = (Structure*)psObj;
		if ((gameTime - psStructure->timeLastHit) < ELEC_DAMAGE_DURATION
			&& psStructure->lastHitWeapon == WSC_ELECTRONIC)
		{
			return true;
		}
		break;

	default:
		ASSERT(false, "Unknown or invalid object for EW: %s", objInfo(psObj));
		return false;
	}

	return false;
}

glm::mat4 objectShimmy(SimpleObject* psObj)
{
	if (justBeenHitByEW(psObj))
	{
		const glm::mat4 rotations =
			glm::rotate(UNDEG(SKY_SHIMMY), glm::vec3(1.f, 0.f, 0.f)) *
			glm::rotate(UNDEG(SKY_SHIMMY), glm::vec3(0.f, 1.f, 0.f)) *
			glm::rotate(UNDEG(SKY_SHIMMY), glm::vec3(0.f, 0.f, 1.f));
		if (psObj->type != OBJ_DROID)
			return rotations;
		return rotations * glm::translate(glm::vec3(1 - rand() % 3, 0, 1 - rand() % 3));
	}
	return glm::mat4(1.f);
}


#define BULLET_FLIGHT_HEIGHT 16


int establishTargetHeight(SimpleObject const* psTarget)
{
	if (psTarget == nullptr)
	{
		return 0;
	}

	CHECK_OBJECT(psTarget);

	switch (psTarget->type)
	{
	case OBJ_DROID:
		{
			Droid const* psDroid = (Droid const*)psTarget;
			unsigned int height = asBodyStats[psDroid->asBits[COMP_BODY]].pIMD->max.y - asBodyStats[psDroid->asBits[
				COMP_BODY]].pIMD->min.y;
			unsigned int utilityHeight = 0, yMax = 0, yMin = 0;
			// Temporaries for addition of utility's height to total height

			// VTOL's don't have pIMD either it seems...
			if (isVtolDroid(psDroid))
			{
				return (height + VTOL_HITBOX_MODIFIER);
			}

			switch (psDroid->type)
			{
			case DROID_WEAPON:
				if (psDroid->numWeaps > 0)
				{
					// Don't do this for Barbarian Propulsions as they don't possess a turret (and thus have pIMD == NULL)
					if ((asWeaponStats[psDroid->asWeaps[0].nStat]).pIMD == nullptr)
					{
						return height;
					}

					yMax = (asWeaponStats[psDroid->asWeaps[0].nStat]).pIMD->max.y;
					yMin = (asWeaponStats[psDroid->asWeaps[0].nStat]).pIMD->min.y;
				}
				break;

			case DROID_SENSOR:
				yMax = (asSensorStats[psDroid->asBits[COMP_SENSOR]]).pIMD->max.y;
				yMin = (asSensorStats[psDroid->asBits[COMP_SENSOR]]).pIMD->min.y;
				break;

			case DROID_ECM:
				yMax = (asECMStats[psDroid->asBits[COMP_ECM]]).pIMD->max.y;
				yMin = (asECMStats[psDroid->asBits[COMP_ECM]]).pIMD->min.y;
				break;

			case DROID_CONSTRUCT:
				yMax = (asConstructStats[psDroid->asBits[COMP_CONSTRUCT]]).pIMD->max.y;
				yMin = (asConstructStats[psDroid->asBits[COMP_CONSTRUCT]]).pIMD->min.y;
				break;

			case DROID_REPAIR:
				yMax = (asRepairStats[psDroid->asBits[COMP_REPAIRUNIT]]).pIMD->max.y;
				yMin = (asRepairStats[psDroid->asBits[COMP_REPAIRUNIT]]).pIMD->min.y;
				break;

			case DROID_PERSON:
			//TODO:add person 'state'checks here(stand, knee, crouch, prone etc)
			case DROID_CYBORG:
			case DROID_CYBORG_CONSTRUCT:
			case DROID_CYBORG_REPAIR:
			case DROID_CYBORG_SUPER:
			case DROID_DEFAULT:
			case DROID_TRANSPORTER:
			case DROID_SUPERTRANSPORTER:
			// Commanders don't have pIMD either
			case DROID_COMMAND:
			case DROID_ANY:
				return height;
			}

			// TODO: check the /2 - does this really make sense? why + ?
			utilityHeight = (yMax + yMin) / 2;

			return height + utilityHeight;
		}
	case OBJ_STRUCTURE:
		{
			StructureStats* psStructureStats = ((Structure const*)psTarget)->pStructureType;
			int height = psStructureStats->IMDs[0]->max.y + psStructureStats->IMDs[0]->min.y;
			height -= gateCurrentOpenHeight((Structure const*)psTarget, gameTime, 2);
			// Treat gate as at least 2 units tall, even if open, so that it's possible to hit.
			return height;
		}
	case OBJ_FEATURE:
		// Just use imd ymax+ymin
		return psTarget->sDisplay.imd->max.y + psTarget->sDisplay.imd->min.y;
	case OBJ_PROJECTILE:
		return BULLET_FLIGHT_HEIGHT;
	default:
		return 0;
	}
}

void checkProjectile(const Projectile* psProjectile, const char* const location_description, const char* function,
                     const int recurse)
{
	if (recurse < 0)
	{
		return;
	}

	ASSERT_HELPER(psProjectile != nullptr, location_description, function, "CHECK_PROJECTILE: NULL pointer");
	ASSERT_HELPER(psProjectile->weaponStats != nullptr, location_description, function, "CHECK_PROJECTILE");
	ASSERT_HELPER(psProjectile->type == OBJ_PROJECTILE, location_description, function, "CHECK_PROJECTILE");
	ASSERT_HELPER(psProjectile->player < MAX_PLAYERS, location_description, function,
	              "CHECK_PROJECTILE: Out of bound owning player number (%u)", (unsigned int)psProjectile->player);
	ASSERT_HELPER(psProjectile->state == PROJ_INFLIGHT
	              || psProjectile->state == PROJ_IMPACT
	              || psProjectile->state == PROJ_POSTIMPACT
	              || psProjectile->state == PROJ_INACTIVE, location_description, function,
	              "CHECK_PROJECTILE: invalid projectile state: %u", (unsigned int)psProjectile->state);

	if (psProjectile->target)
	{
		checkObject(psProjectile->target, location_description, function, recurse - 1);
	}

	if (psProjectile->source)
	{
		checkObject(psProjectile->source, location_description, function, recurse - 1);
	}

	for (auto n : psProjectile->damaged)
	{
		checkObject(n, location_description, function, recurse - 1);
	}
}
