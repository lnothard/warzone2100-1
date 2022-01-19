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

#include <glm/gtx/transform.hpp>
#include "lib/framework/math_ext.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "ai.h"
#include "display3d.h"
#include "displaydef.h"
#include "droid.h"
#include "effects.h"
#include "feature.h"
#include "mapgrid.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "scores.h"
#include "structure.h"
#include "weapon.h"

bool bMultiPlayer;
bool gamePaused();
const char* objInfo(const PersistentObject*);
Droid* cmdDroidGetDesignator(unsigned);
void tileSetFire(int, int, uint);
void updateMultiStatsDamage(unsigned, unsigned, unsigned);
void updateMultiStatsKills(PersistentObject*, unsigned);
int mapWidth, mapHeight;
unsigned map_LineIntersect(Vector3i, Vector3i, unsigned);
int map_Height(int, int);
int map_Height(Vector2i);
void shakeStart(unsigned);
static const unsigned max_check_object_recursion = 4;
void aiObjectAddExpectedDamage(PersistentObject*, int, bool);
int areaOfFire(const PersistentObject*, const PersistentObject*, int, bool);
void cmdDroidUpdateExperience(Droid*, unsigned);
void counterBatteryFire(PersistentObject*, PersistentObject*);
Spacetime interpolateObjectSpacetime(const PersistentObject*, unsigned);
unsigned objGuessFutureDamage(WeaponStats*, unsigned, PersistentObject*);


Projectile::Projectile(unsigned id, unsigned player)
  : PersistentObject(id, player)
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
static std::vector<std::unique_ptr<Projectile>> psProjectileList;

// the last unit that did damage - used by script functions
PersistentObject* g_pProjLastAttacker;

static void proj_ImpactFunc(Projectile* psObj);
static void proj_PostImpactFunc(Projectile* psObj);
static void proj_checkPeriodicalDamage(Projectile* psProj);

static int objectDamage(Damage* psDamage);

void Projectile::setTarget(::PersistentObject* psObj)
{
	bool bDirect = proj_Direct(weaponStats.get());

	aiObjectAddExpectedDamage(target,
                            -expectedDamageCaused,
                            bDirect);

	target = psObj;
	aiObjectAddExpectedDamage(target,
                            expectedDamageCaused,
                            bDirect);
	// let the new target know to say its prayers
}

bool Projectile::gfxVisible() const
{
	// already know it is visible
	if (isVisible) {
		return true;
	}

	// you fired it
	if (player == selectedPlayer) {
		return true;
	}

	// someone else's structure firing at something you can't see
	if (source != nullptr && source->isAlive() &&
      dynamic_cast<Structure*>(source) &&
      source->getPlayer() != selectedPlayer &&
      (target == nullptr || !target->isAlive() ||
       !target->visibleToSelectedPlayer())) {
		return false;
	}

	// something you cannot see firing at a structure that isn't yours
	if (target != nullptr
      && target->isAlive()
      && dynamic_cast<Structure*>(target)
      && target->getPlayer() != selectedPlayer
      && (source == nullptr
        || !source->visibleToSelectedPlayer())) {
		return false;
	}

	// you can see the source
	if (source != nullptr
      && source->isAlive()
      && source->visibleToSelectedPlayer()) {
		return true;
	}

	// you can see the destination
	if (target != nullptr
      && target->isAlive()
      && target->visibleToSelectedPlayer()) {
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

/**
 * Relates the quality of the attacker to the quality of the victim.
 * The value returned satisfies the following inequality:
 * \f(0.5 <= ret/65536 <= 2.0)\f
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

void setExpGain(unsigned player, int gain)
{
	experienceGain[player] = gain;
}

int getExpGain(unsigned player)
{
	return experienceGain[player];
}

Droid* getDesignatorAttackingObject(unsigned player, PersistentObject* target)
{
	const auto psCommander = cmdDroidGetDesignator(player);

	return psCommander != nullptr && 
         psCommander->getAction() == ACTION::ATTACK &&
         &psCommander->getTarget(0) == target
		       ? psCommander
		       : nullptr;
}

void Projectile::updateExperience(unsigned experienceInc)
{
	::PersistentObject* psSensor;

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
		ASSERT_OR_RETURN(, experienceInc < (int)(2.1 * 65536),
                     "Experience increase out of range");
		psDroid->gainExperience(experienceInc);
		cmdDroidUpdateExperience(psDroid, experienceInc);

		psSensor = orderStateObj(psDroid, ORDER_TYPE::FIRE_SUPPORT);
		if (psSensor && dynamic_cast<Droid*>(psSensor)) {
			dynamic_cast<Droid*>(psSensor)->gainExperience(experienceInc);
		}
	} else if (dynamic_cast<Structure*>(source))
	{
		ASSERT_OR_RETURN(, experienceInc < (int)(2.1 * 65536),
                     "Experience increase out of range");
		psDroid = getDesignatorAttackingObject(source->getPlayer(), target);

		if (psDroid) {
			psDroid->gainExperience(experienceInc);
		}
	}
}

//void Projectile::debug_(const char* function, char ch) const
//{
//	int list[] =
//	{
//		ch,
//    static_cast<int>(player),
//		getPosition().x,
//    getPosition().y,
//    getPosition().z,
//		getRotation().direction,
//    getRotation().pitch,
//    getRotation().roll,
//    static_cast<int>(state),
//		expectedDamageCaused,
//		static_cast<int>(damaged.size()),
//	};
//	_syncDebugIntList(
//		function, "%c projectile = p%d;pos(%d,%d,%d),rot(%d,%d,%d),state%d,expectedDamageCaused%d,numberDamaged%u",
//		list, ARRAY_SIZE(list));
//}

static int randomVariation(int val)
{
	// Up to ±5% random variation
	return (int64_t)val * (95000 + gameRand(10001)) / 100000;
}

int projCalcIndirectVelocities(int dx, int dz, int v,
                               int* vx, int* vz, int min_angle)
{
	// Find values of vx and vz, which solve the equations:
	// dz = -1/2 g t² + vz t
	// dx = vx t
	// v² = vx² + vz²
	// Increases v, if needed for there to be a solution. Decreases v, if needed for vz > 0.
	// Randomly changes v by up to 2.5%, so the shots don't all follow the same path.

	const auto g = ACC_GRAVITY; // In units/s².
	auto a = randomVariation(v * v) - dz * g; // In units²/s².
	auto b = g * g * ((uint64_t)dx * dx + (uint64_t)dz * dz);
	// In units⁴/s⁴. Casting to uint64_t does sign extend the int.
	auto c = (uint64_t)a * a - b; // In units⁴/s⁴.
	if (c < 0) {
		// Must increase velocity, target too high. Find the smallest possible a (which corresponds to the smallest possible velocity).

		a = i64Sqrt(b) + 1; // Still in units²/s². Adding +1, since i64Sqrt rounds down.
		c = (uint64_t)a * a - b; // Still in units⁴/s⁴. Should be 0, plus possible rounding errors.
	}

	auto t = MAX(1, iSqrt(2 * (a - i64Sqrt(c))) * (GAME_TICKS_PER_SEC / g));
	// In ticks. Note that a - √c ≥ 0, since c ≤ a². Try changing the - to +, and watch the mini-rockets.
	*vx = dx * GAME_TICKS_PER_SEC / t; // In units/sec.
	*vz = dz * GAME_TICKS_PER_SEC / t + g * t / (2 * GAME_TICKS_PER_SEC); // In units/sec.

	STATIC_ASSERT(GAME_TICKS_PER_SEC / ACC_GRAVITY * ACC_GRAVITY == GAME_TICKS_PER_SEC);
	// On line that calculates t, must cast iSqrt to uint64_t, and remove brackets around TICKS_PER_SEC/g, if changing ACC_GRAVITY.

	if (*vz < 0) {
		// Don't want to shoot downwards, reduce velocity and let gravity take over.
		t = MAX(1, i64Sqrt(-2 * dz * (uint64_t)GAME_TICKS_PER_SEC * GAME_TICKS_PER_SEC / g)); // Still in ticks.
		*vx = dx * GAME_TICKS_PER_SEC / t; // Still in units/sec.
		*vz = 0; // Still in units/sec. (Wouldn't really matter if it was pigeons/inch, since it's 0 anyway.)
	}

	/* CorvusCorax: Check against min_angle */
	if (iAtan2(*vz, *vx) < min_angle) {
		/* set pitch to pass terrain */
		// tan(min_angle)=mytan/65536
		auto mytan = ((int64_t)iSin(min_angle) * 65536) / iCos(min_angle);
		t = MAX(
			1, i64Sqrt(2 * ((int64_t)dx * mytan - dz * 65536) * (int64_t)GAME_TICKS_PER_SEC * GAME_TICKS_PER_SEC / (
				int64_t)(g * 65536))); // Still in ticks.
		*vx = dx * GAME_TICKS_PER_SEC / t;
		// mytan=65536*vz/vx
		*vz = (mytan * (*vx)) / 65536;
	}

	return t;
}

bool proj_SendProjectile(Weapon* psWeap, PersistentObject* psAttacker, unsigned player,
                         Vector3i target, PersistentObject* psTarget, bool bVisible,
                         int weapon_slot)
{
	return proj_SendProjectileAngled(psWeap, psAttacker, player, target, psTarget,
                                   bVisible, weapon_slot, 0, gameTime - 1);
}

bool Projectile::proj_SendProjectileAngled(Weapon* psWeap, ::PersistentObject* psAttacker, unsigned plr,
                                           Vector3i dest, ::PersistentObject* psTarget, bool bVisible,
                                           int weapon_slot, int min_angle, unsigned fireTime) const
{
	auto& psStats = psWeap->getStats();

	ASSERT_OR_RETURN(false, psTarget == nullptr ||
                          !psTarget->died, "Aiming at dead target!");

	auto psProj = std::make_unique<Projectile>(
          ProjectileTrackerID + ++projectileTrackerIDIncrement, plr);

	/* get muzzle offset */
	if (psAttacker == nullptr) {
		// if there isn't an attacker just start at the target position
		// NB this is for the script function to fire the las sats
		psProj->origin = dest;
	}
	else if (dynamic_cast<Droid*>(psAttacker) && 
           weapon_slot >= 0) {
		::calcDroidMuzzleLocation(dynamic_cast<Droid*>(psAttacker),
                              &psProj->origin, weapon_slot);
		// update attack runs for VTOL droid's each time a shot is fired
		::updateVtolAttackRun(*dynamic_cast<Droid*>(psAttacker), weapon_slot);
	}
	else if (dynamic_cast<Structure*>(psAttacker) && 
           weapon_slot >= 0) {
		::calcStructureMuzzleLocation(dynamic_cast<Structure*>(psAttacker),
                                  &psProj->origin, weapon_slot);
	}
	else // incase anything wants a projectile 
  {
		psProj->origin = psAttacker->getPosition();
	}

	/* Initialise the structure */
	psProj->weaponStats = std::make_shared<WeaponStats>(psStats);

	psProj->setPosition(psProj->origin);
	psProj->destination = dest;

	psProj->isVisible = false;

	// Must set ->psDest and ->expectedDamageCaused before first call to setProjectileDestination().
	psProj->target = nullptr;
	psProj->expectedDamageCaused = ::objGuessFutureDamage(psStats, plr, psTarget);
	psProj->setTarget(psTarget);
	// Updates expected damage of psProj->psDest, using psProj->expectedDamageCaused.

	/*
	When we have been created by penetration (spawned from another projectile),
	we shall live no longer than the original projectile may have lived
	*/
	if (psAttacker && dynamic_cast<Projectile*>(psAttacker)) {
		auto psOldProjectile = dynamic_cast<Projectile*>(psAttacker);
		psProj->bornTime = psOldProjectile->bornTime;
		psProj->origin = psOldProjectile->origin;

    // have partially ticked already
		psProj->previousLocation.time = psOldProjectile->getTime();
		psProj->setTime(gameTime);

    // times should not be equal, for interpolation
    psProj->previousLocation.time -= psProj->
              previousLocation.time == psProj->getTime();

		psProj->setSource(psOldProjectile->source);
		psProj->damaged = psOldProjectile->damaged;

		// TODO Should finish the tick, when penetrating.
	}
	else {
		psProj->bornTime = fireTime; // Born at the start of the tick.

		psProj->previousLocation.time = fireTime;
		psProj->setTime(psProj->previousLocation.time);

		psProj->setSource(psAttacker);
	}

	if (psTarget) {
		auto maxHeight = establishTargetHeight(psTarget);
		auto minHeight = std::min(
			std::max(maxHeight + 2 * LINE_OF_FIRE_MINIMUM - areaOfFire(
              psAttacker, psTarget, weapon_slot, true), 0), maxHeight);
		scoreUpdateVar(WD_SHOTS_ON_TARGET);

		psProj->destination.z = psTarget->getPosition().z + 
            minHeight + gameRand(std::max(maxHeight - minHeight, 1));
		/* store visible part (LOCK ON this part for homing :) */
		psProj->partVisible = maxHeight - minHeight;
	}
	else {
		psProj->destination.z = dest.z + LINE_OF_FIRE_MINIMUM;
		scoreUpdateVar(WD_SHOTS_OFF_TARGET);
	}

	Vector3i deltaPos = psProj->destination - psProj->origin;

	/* roll never set */
	auto roll = 0;
	auto direction = iAtan2(deltaPos.xy());


	// get target distance, horizontal distance only
	auto dist = iHypot(deltaPos.xy());

  int pitch;
	if (proj_Direct(&psStats)) {
		pitch = iAtan2(deltaPos.z, dist);
	}
	else {
		/* indirect */
		projCalcIndirectVelocities(dist, deltaPos.z, psStats.flightSpeed,
                               &psProj->vXY, &psProj->vZ, min_angle);
		pitch = iAtan2(psProj->vZ, psProj->vXY);
	}
  
  psProj->setRotation({direction, pitch, roll});
  
	psProj->state = PROJECTILE_STATE::INFLIGHT;

	// if droid or structure, set muzzle pitch
	if (psAttacker != nullptr && weapon_slot >= 0) {
		if (auto droid = dynamic_cast<Droid*>(psAttacker)) {
			droid->getWeapons()[weapon_slot].rotation.pitch = psProj->getRotation().pitch;
		}
		else if (auto structure = dynamic_cast<Structure*>(psAttacker)) {
			structure->getWeapons()[weapon_slot].rotation.pitch = psProj->getRotation().pitch;
		}
	}

	/* put the projectile object in the global list */
	psProjectileList.push_back(psProj);

	// play firing audio
	// -- only play if either object is visible, I know it's a bit of a hack,
  // but it avoids the problem of having to calculate real visibility
  // values for each projectile
	if (bVisible || psProj->gfxVisible()) {
		// note that the projectile is visible
		psProj->isVisible = true;

		if (psStats.iAudioFireID != NO_SOUND) {
			if (psProj->source) {
				/* firing sound emitted from source */
				audio_PlayObjDynamicTrack(psProj->source,
                                  psStats.iAudioFireID, nullptr);
				/* GJ HACK: move howitzer sound with shell */
				if (psStats.weaponSubClass == WEAPON_SUBCLASS::HOWITZERS)
				{
					audio_PlayObjDynamicTrack(psProj.get(),
                                    ID_SOUND_HOWITZ_FLIGHT, nullptr);
				}
			}
			// don't play the sound for a LasSat in multiplayer
			else if (!(bMultiPlayer && psStats.weaponSubClass == WEAPON_SUBCLASS::LAS_SAT)) {
				audio_PlayObjStaticTrack(psProj.get(), psStats.iAudioFireID);
			}
		}
	}

	if (psAttacker != nullptr && !proj_Direct(&psStats)) {
		// check for counter-battery sensor in range of target
		counterBatteryFire(psAttacker, psTarget);
	}

	return true;
}


static Interval intervalIntersection(Interval i1, Interval i2)
{
	Interval ret = {MAX(i1.begin, i2.begin), MIN(i1.end, i2.end)};
	return ret;
}

bool Interval::isEmpty() const
{
	return begin >= end;
}

static Interval collisionZ(int z1, int z2, int height)
{
	Interval ret = {-1, -1};
	if (z1 > z2) {
		z1 *= -1;
		z2 *= -1;
	}
	if (z1 > height || z2 < -height) {
		return ret; // no collision between time 1 and time 2
	}
	if (z1 == z2) {
		if (z1 >= -height && z1 <= height) {
			ret.begin = 0;
			ret.end = 1024;
		}
		return ret;
	}
	ret.begin = 1024 * (-height - z1) / (z2 - z1);
	ret.end = 1024 * (height - z1) / (z2 - z1);
	return ret;
}

static Interval collisionXY(int x1, int y1, int x2, int y2, int radius)
{
	// Solve (1 - t)v1 + t v2 = r.
	auto dx = x2 - x1, dy = y2 - y1;
	auto a = (int64_t)dx * dx + (int64_t)dy * dy; // a = (v2 - v1)²
	auto b = (int64_t)x1 * dx + (int64_t)y1 * dy; // b = v1(v2 - v1)
	auto c = (int64_t)x1 * x1 + (int64_t)y1 * y1 - (int64_t)radius * radius; // c = v1² - r²
	// Equation to solve is now a t^2 + 2 b t + c = 0.
	auto d = b * b - a * c; // d = b² - a c
	// Solution is (-b ± √d)/a.
	Interval empty = {-1, -1};
	Interval full = {0, 1024};

	if (d < 0) {
    // missed
		return empty; 
	}
	if (a == 0) {
    // not moving. see if inside the target
		return c < 0 ? full : empty; 
	}

	auto sd = i64Sqrt(d);
  Interval ret;
	ret.begin = MAX(0, 1024 * (-b - sd) / a);
	ret.end = MIN(1024, 1024 * (-b + sd) / a);
	return ret;
}

static int collisionXYZ(Vector3i v1, Vector3i v2, ObjectShape shape, int height)
{
	auto i = collisionZ(v1.z, v2.z, height);
  // don't bother checking x and y unless z passes
  if (!i.isEmpty()) {
		if (shape.isRectangular) {
			i = intervalIntersection(i, collisionZ(
              v1.x, v2.x, shape.size.x));
      // don't bother checking y unless x and z pass
      if (!i.isEmpty()) {
				i = intervalIntersection(i, collisionZ(
                v1.y, v2.y, shape.size.y));
			}
		}
    // else is circular
		else {
			i = intervalIntersection(i, collisionXY(
              v1.x, v1.y, v2.x, v2.y, shape.radius()));
		}

		if (!i.isEmpty()) {
			return MAX(0, i.begin);
		}
	}
	return -1;
}

void Projectile::proj_InFlightFunc()
{
	/* we want a delay between Las-Sats firing and actually hitting in multiPlayer
	magic number but that's how long the audio countdown message lasts! */
	const auto LAS_SAT_DELAY = 4;
	::PersistentObject* closestCollisionObject = nullptr;
	Spacetime closestCollisionSpacetime;

	auto timeSoFar = gameTime - bornTime;

	setTime(gameTime);
	auto deltaProjectileTime = getTime() - previousLocation.time;

	auto& psStats = weaponStats;
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid weapon stats pointer");

	/* we want a delay between Las-Sats firing and actually hitting in multiPlayer
	magic number but that's how long the audio countdown message lasts! */
	if (bMultiPlayer && 
      psStats->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT &&
		  timeSoFar < LAS_SAT_DELAY * GAME_TICKS_PER_SEC) {
		return;
	}

	/* Calculate movement vector: */
  
	auto currentDistance = 0;
	switch (psStats->movementModel) {
    using enum MOVEMENT_MODEL;
	  case DIRECT: // Go in a straight line.
	  	{
	  		Vector3i delta = destination - origin;
	  		if (psStats->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT) {
	  			// LAS_SAT doesn't have a z
	  			delta.z = 0;
	  		}
	  		auto targetDistance = std::max(iHypot(delta.xy()), 1);
	  		currentDistance = timeSoFar * psStats->flightSpeed / GAME_TICKS_PER_SEC;
	  		setPosition(origin + delta * currentDistance / targetDistance);
	  		break;
	  	}
	  case INDIRECT: // ballistic trajectory
	  	{
	  		auto delta = destination - origin;
	  		delta.z = (vZ - (timeSoFar * ACC_GRAVITY / (GAME_TICKS_PER_SEC * 2))) * timeSoFar /
	  			GAME_TICKS_PER_SEC; // '2' because we reach our highest point in the mid of flight, when "vZ is 0".
	  		auto targetDistance = std::max(iHypot(delta.xy()), 1);
	  		currentDistance = timeSoFar * vXY / GAME_TICKS_PER_SEC;
	  		setPosition(origin + delta * currentDistance / targetDistance);
	  		setPosition({getPosition().x, getPosition().y,
                     origin.z + delta.z}); // use raw z value

	  		auto pitch = iAtan2(vZ - (timeSoFar * ACC_GRAVITY / GAME_TICKS_PER_SEC), vXY);
        setRotation({getRotation().direction, pitch, getRotation().roll});
	  		break;
	  	}
	  case HOMING_DIRECT: // Fly towards target, even if target moves.
	  case HOMING_INDIRECT: // Fly towards target, even if target moves. Avoid terrain.
	  	{
	  		if (target) {
	  			if (psStats->movementModel == HOMING_DIRECT) {
	  				// If it's homing and has a target (not a miss)...
	  				// Home at the centre of the part that was visible when firing.
	  				destination = target->getPosition() + Vector3i(
	  					0, 0, establishTargetHeight(target) - partVisible / 2);
	  			}
	  			else {
	  				destination = target->getPosition() + Vector3i{
                    0, 0, establishTargetHeight(target) / 2};
	  			}
	  			auto targetDroid = dynamic_cast<Droid*>(target);
	  			if (targetDroid != nullptr) {
	  				// Do target prediction.
	  				auto delta = destination - getPosition();
	  				auto flightTime = iHypot(delta.xy()) * GAME_TICKS_PER_SEC / psStats->flightSpeed;
	  				destination += Vector3i(iSinCosR(targetDroid->getMovementData().moveDir,
	  				                                 std::min<int>(targetDroid->getMovementData().speed,
	  				                                               psStats->flightSpeed * 3 / 4) * flightTime /
	  				                                 GAME_TICKS_PER_SEC), 0);
	  			}
	  			destination.x = clip(
                  destination.x, 0, world_coord(mapWidth) - 1);
	  			destination.y = clip(
                  destination.y, 0, world_coord(mapHeight) - 1);
	  		}
	  		if (psStats->movementModel == HOMING_INDIRECT) {
	  			if (target == nullptr) {
	  				destination.z = map_Height(getPosition().xy()) - 1;
	  				// target missing, so just home in on the ground under where the target was
	  			}
	  			int horizontalTargetDistance = iHypot((destination - getPosition()).xy());
	  			int terrainHeight = std::max(map_Height(getPosition().xy()),
	  			                             map_Height(getPosition().xy() + iSinCosR(
	  				                             iAtan2((destination - getPosition()).xy()),
	  				                             psStats->flightSpeed * 2 * deltaProjectileTime / GAME_TICKS_PER_SEC)));
	  			int desiredMinHeight = terrainHeight +
	  				std::min(horizontalTargetDistance / 4, HOMINGINDIRECT_HEIGHT_MIN);
	  			int desiredMaxHeight = std::max(destination.z, terrainHeight + HOMINGINDIRECT_HEIGHT_MAX);
	  			int heightError = getPosition().z - clip(getPosition().z, desiredMinHeight, desiredMaxHeight);
	  			destination.z -= horizontalTargetDistance * heightError * 2 / HOMINGINDIRECT_HEIGHT_MIN;
	  		}
	  		Vector3i delta = destination - getPosition();
	  		int targetDistance = std::max(iHypot(delta), 1);
	  		if (target == nullptr && targetDistance < 10000 && 
            psStats->movementModel == HOMING_DIRECT) {
	  			destination = getPosition() + delta * 10; // Target missing, so just keep going in a straight line.
	  		}
	  		currentDistance = timeSoFar * psStats->flightSpeed / GAME_TICKS_PER_SEC;
	  		Vector3i step = quantiseFraction(delta * int(psStats->flightSpeed), GAME_TICKS_PER_SEC * targetDistance,
                                         getTime(), previousLocation.time);
	  		if (psStats->movementModel == HOMING_INDIRECT && target != nullptr) {
	  			for (int tries = 0; tries < 10 && map_LineIntersect(previousLocation.position, getPosition() + step,
                                                              iHypot(step)) < targetDistance - 1u; ++tries)
	  			{
	  				destination.z += iHypot((destination - getPosition()).xy());
	  				// Would collide with terrain this tick, change trajectory.
	  				// Recalculate delta, targetDistance and step.
	  				delta = destination - getPosition();
	  				targetDistance = std::max(iHypot(delta), 1);
	  				step = quantiseFraction(delta * int(psStats->flightSpeed), GAME_TICKS_PER_SEC * targetDistance,
                                    getTime(), previousLocation.time);
	  			}
	  		}
	  		setPosition(getPosition() + step);

	  		auto direction = iAtan2(delta.xy());
	  		auto pitch = iAtan2(delta.z, targetDistance);
        setRotation({direction, pitch, getRotation().roll});

	  		break;
	  	}
	  }

	closestCollisionSpacetime.time = 0xFFFFFFFF;

	/* Check nearby objects for possible collisions */
	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(getPosition().x, getPosition().y, PROJ_NEIGHBOUR_RANGE);
	for (::PersistentObject* psTempObj : gridList)
	{
		if (std::find(damaged.begin(), damaged.end(),
                  psTempObj) != damaged.end()) {
			// don't damage the same target twice
			continue;
		}
		else if (psTempObj->died) {
			// do not damage dead objects further
			continue;
		}
		else if (dynamic_cast<Feature*>(psTempObj) &&
             !dynamic_cast<Feature*>(psTempObj)->getStats()->damageable) {
			// ignore oil resources, artifacts and other pickups
			continue;
		}
		else if (aiCheckAlliances(
            psTempObj->getPlayer(), getPlayer()) &&
            psTempObj != target) {
			// no friendly fire unless intentional
			continue;
		}
		else if (!(psStats->surfaceToAir & SHOOT_ON_GROUND) &&
             (dynamic_cast<Structure*>(psTempObj) ||
              dynamic_cast<Feature*>(psTempObj) ||
             (dynamic_cast<Droid*>(psTempObj) &&
              !isFlying(dynamic_cast<Droid*>(psTempObj))))) {
			// AA weapons should not hit buildings and non-vtol droids
			continue;
		}

		Vector3i psTempObjPrevPos = dynamic_cast<Droid*>(psTempObj)
            ? dynamic_cast<Droid*>(psTempObj)->previousLocation.pos
            : psTempObj->getPosition();

		const Vector3i diff = getPosition() - psTempObj->getPosition();
		const Vector3i prevDiff = previousLocation.position - psTempObjPrevPos;
		const auto targetHeight = establishTargetHeight(psTempObj);
		const auto targetShape = establishTargetShape(psTempObj);
		const auto collision = collisionXYZ(prevDiff, diff, targetShape, targetHeight);
		const auto collisionTime = previousLocation.time + (getTime() - previousLocation.time) *
                                                       collision / 1024;

		if (collision >= 0 &&
        collisionTime < closestCollisionSpacetime.time) {
			// We hit!
			closestCollisionSpacetime = interpolateObjectSpacetime(this, collisionTime);
			closestCollisionObject = psTempObj;

			// Keep testing for more collisions, in case there was a closer target.
		}
	}

	unsigned terrainIntersectTime = map_LineIntersect(previousLocation.position, getPosition(),
                                                    getTime() - previousLocation.time);
	if (terrainIntersectTime != UINT32_MAX) {
		const uint collisionTime = previousLocation.time + terrainIntersectTime;
		if (collisionTime < closestCollisionSpacetime.time) {
			// We hit the terrain!
			closestCollisionSpacetime = interpolateObjectSpacetime(this, collisionTime);
			closestCollisionObject = nullptr;
		}
	}

	if (closestCollisionSpacetime.time != 0xFFFFFFFF) {
		// We hit!
		setSpacetime(this, closestCollisionSpacetime);
		setTime(std::max(getTime(), gameTime - deltaGameTime + 1));
		// Make sure .died gets set in the interval [gameTime - deltaGameTime + 1; gameTime].
		if (getTime() == previousLocation.time) {
			--previousLocation.time;
		}
		setTarget(closestCollisionObject); // We hit something.

		// Buildings and terrain cannot be penetrated and we need a penetrating weapon, and projectile should not have already travelled further than 1.25 * maximum range.
		if (closestCollisionObject && 
        dynamic_cast<Droid*>(closestCollisionObject) &&
        psStats->penetrate &&
			  currentDistance < static_cast<int>(
                1.25 * proj_GetLongRange(psStats.get(),
                                         getPlayer()))) {
			Weapon asWeap;
			asWeap.stats = psStats;

			// Assume we damaged the chosen target
			damaged.push_back(closestCollisionObject);

			proj_SendProjectile(&asWeap, this, getPlayer(),
                          destination, nullptr, true, -1);
		}

		state = PROJECTILE_STATE::IMPACT;

		return;
	}

	if (currentDistance * 100 >= 
        proj_GetLongRange(psStats.get(),
                          getPlayer()) * psStats->distanceExtensionFactor) {
		// We've travelled our maximum range.
		state = PROJECTILE_STATE::IMPACT;
		setTarget(nullptr); /* miss registered if NULL target */
		return;
	}

	/* Paint effects if visible */
	if (gfxVisible()) {
		uint effectTime;
		for (effectTime = ((previousLocation.time + 31) & ~31);
         effectTime < getTime(); effectTime += 32)
		{
			Spacetime st = interpolateObjectSpacetime(this, effectTime);
			Vector3i posFlip = st.position.xzy();
			switch (psStats->weaponSubClass) {
			  case WEAPON_SUBCLASS::FLAME:
			  	posFlip.z -= 8; // Why?
			  	effectGiveAuxVar(PERCENT(currentDistance, proj_GetLongRange(
                  psStats.get(), getPlayer())));
          
			  	addEffect(&posFlip, EFFECT_GROUP::EXPLOSION,
                    EFFECT_TYPE::EXPLOSION_TYPE_FLAMETHROWER, 
                    false, nullptr, 0, effectTime);
			  	break;
			  case WEAPON_SUBCLASS::COMMAND:
			  case WEAPON_SUBCLASS::ELECTRONIC:
			  case WEAPON_SUBCLASS::EMP:
			  	posFlip.z -= 8; // Why?
			  	effectGiveAuxVar(PERCENT(currentDistance, proj_GetLongRange(
                  psStats.get(), getPlayer())) / 2);
          
			  	addEffect(&posFlip, EFFECT_GROUP::EXPLOSION, 
                    EFFECT_TYPE::EXPLOSION_TYPE_LASER, 
                    false, nullptr, 0, effectTime);
			  	break;
			  case WEAPON_SUBCLASS::ROCKET:
			  case WEAPON_SUBCLASS::MISSILE:
			  case WEAPON_SUBCLASS::SLOW_ROCKET:
			  case WEAPON_SUBCLASS::SLOW_MISSILE:
			  	posFlip.z += 8; // Why?
			  	addEffect(&posFlip, EFFECT_GROUP::SMOKE, 
                    EFFECT_TYPE::SMOKE_TYPE_TRAIL, 
                    false, nullptr, 0, effectTime);
			  	break;
			  default:
			  	// Add smoke trail to indirect weapons, even if firing directly.
			  	if (!proj_Direct(psStats.get())) {
			  		posFlip.z += 4; // Why?
			  		addEffect(&posFlip, EFFECT_GROUP::SMOKE,
                      EFFECT_TYPE::SMOKE_TYPE_TRAIL, 
                      false, nullptr, 0, effectTime);
			  	}
			  // Otherwise no effect.
			  	break;
			}
		}
	}
}

void Projectile::proj_ImpactFunc()
{
  Vector3i position;
	std::unique_ptr<iIMDShape> imd;
	PersistentObject* temp;

	auto psStats = weaponStats.get();
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid weapon stats pointer");

	// note the attacker if any
	g_pProjLastAttacker = source;

	/* play impact audio */
	if (gfxVisible()) {
		if (psStats->iAudioImpactID == NO_SOUND) {
			/* play richochet if MG */
			if (target != nullptr &&
          psStats->weaponSubClass == WEAPON_SUBCLASS::MACHINE_GUN
          && ONEINTHREE) {
				auto iAudioImpactID = ID_SOUND_RICOCHET_1 + (rand() % 3);
        
				audio_PlayStaticTrack(target->getPosition().x,
                              target->getPosition().y, 
                              iAudioImpactID);
			}
		}
		else {
			audio_PlayStaticTrack(getPosition().x,
                            getPosition().y,
                            psStats->iAudioImpactID);
		}

		/** Shouldn't need to do this check but the stats aren't all
		 * at a value yet...
		 */ // FIXME
		if (psStats->upgraded[player].periodicalDamageRadius != 0 &&
        psStats->upgraded[player].periodicalDamageTime != 0) {
			position.x = getPosition().x;
			position.z = getPosition().y; // z = y [sic] intentional
			position.y = map_Height(position.x, position.z);
			effectGiveAuxVar(psStats->upgraded[player].periodicalDamageRadius);
			effectGiveAuxVarSec(psStats->upgraded[player].periodicalDamageTime);
      
			addEffect(&position, EFFECT_GROUP::FIRE, 
                EFFECT_TYPE::FIRE_TYPE_LOCALISED,
                false, nullptr, 0, time);
		}

		// may want to add both a fire effect and the las sat effect
		if (psStats->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT) { 
			position.x = getPosition().x;
			position.z = getPosition().y; // z = y [sic] intentional
			position.y = map_Height(position.x, position.z);
			addEffect(&position, EFFECT_GROUP::SAT_LASER,
                EFFECT_TYPE::SAT_LASER_STANDARD,
                false, nullptr, 0, time);

			if (clipXY(getPosition().x, getPosition().y)) {
        // takes out lots of stuff so shake length is greater
				shakeStart(1800);
			}
		}
	}

	if (psStats->upgraded[player].periodicalDamageRadius &&
      psStats->upgraded[player].periodicalDamageTime) {
		tileSetFire(getPosition().x, getPosition().y, 
                psStats->upgraded[player].periodicalDamageTime);
	}

	// set the effects position and radius
	position.x = getPosition().x;
	position.z = getPosition().y; // z = y [sic] intentional
	position.y = getPosition().z; // y = z [sic] intentional
	auto x = psStats->upgraded[player].radius;
	auto y = 0;
	auto z = psStats->upgraded[player].radius;
  auto scatter = Vector3i{x, y, z};

	// If the projectile missed its target (or the target died)
	if (target == nullptr) {
		if (gfxVisible()) {
			// Get if we are facing or not
			EFFECT_TYPE facing = (psStats->facePlayer
              ? EFFECT_TYPE::EXPLOSION_TYPE_SPECIFIED
              : EFFECT_TYPE::EXPLOSION_TYPE_NOT_FACING);

			// the graphic to show depends on if we hit water or not
			if (terrainType(mapTile(
              map_coord(getPosition().x),
              map_coord(getPosition().y))) == TER_WATER) {
				imd = psStats->pWaterHitGraphic;
			}
			// we did not hit water, the regular miss graphic will do the trick
			else {
				imd = std::make_unique<iIMDShape>(*psStats->pTargetMissGraphic);
			}

			addMultiEffect(&position, &scatter,
                     EFFECT_GROUP::EXPLOSION,
                     facing, true, imd.get(),
                     psStats->numExplosions, psStats->lightWorld,
                     psStats->effectSize, time);

			// If the target was a VTOL hit in the air add smoke
			if ((psStats->surfaceToAir & SHOOT_IN_AIR) && 
          !(psStats->surfaceToAir & SHOOT_ON_GROUND)) {
				addMultiEffect(&position, &scatter, EFFECT_GROUP::SMOKE,
                       EFFECT_TYPE::SMOKE_TYPE_DRIFTING, false,
                       nullptr, 3, 0, 0, time);
			}
		}
	}
	// The projectile hit its intended target
	else {
		if (dynamic_cast<Feature*>(target) &&
        dynamic_cast<Feature*>(target)->getStats()->damageable == 0) {
			debug(LOG_NEVER, "proj_ImpactFunc: trying to damage "
                       "non-damageable target, projectile removed");
			state = PROJECTILE_STATE::INACTIVE;
			return;
		}

		if (gfxVisible()) {
			// Get if we are facing or not
			EFFECT_TYPE facing = (psStats->facePlayer
              ? EFFECT_TYPE::EXPLOSION_TYPE_SPECIFIED 
              : EFFECT_TYPE::EXPLOSION_TYPE_NOT_FACING);

			// if we hit a VTOL with an AA gun use the miss graphic and add some smoke
			if ((psStats->surfaceToAir & SHOOT_IN_AIR) && 
          !(psStats->surfaceToAir & SHOOT_ON_GROUND) && 
          psStats->weaponSubClass == WEAPON_SUBCLASS::AA_GUN) {
				imd = std::make_unique<iIMDShape>(*psStats->pTargetMissGraphic);
        
				addMultiEffect(&position, &scatter,
                       EFFECT_GROUP::SMOKE,
                       EFFECT_TYPE::SMOKE_TYPE_DRIFTING,
                       false, nullptr, 3,
                       0, 0, time);
			}
			// otherwise, we just hit it plain and simple
			else {
				imd = std::make_unique<iIMDShape>(*psStats->pTargetHitGraphic);
			}

			addMultiEffect(&position, &scatter, EFFECT_GROUP::EXPLOSION,
                     facing, true, imd.get(), psStats->numExplosions,
			               psStats->lightWorld, psStats->effectSize, time);
		}

		// check for electronic warfare damage where we know the subclass and source
		if (proj_Direct(psStats) &&
        psStats->weaponSubClass == WEAPON_SUBCLASS::ELECTRONIC && source) {
			// if we did enough `damage' to capture the target
			if (electronicDamage(target,
			                     calcDamage(
                                   weaponDamage(psStats, player),
                                   psStats->weaponEffect, target),
                           player)) {
				if (auto psDroid = dynamic_cast<Droid*>(source)) {
          psDroid->order.type = ORDER_TYPE::NONE;
          actionDroid(psDroid, ACTION::NONE);
        }
        else if (auto psStruct = dynamic_cast<Structure*>(source)) {
          psStruct->psTarget[0] = nullptr;
        }
			}
		}
		// else it is just a regular weapon (direct or indirect)
		else {
			// Calculate the damage the weapon does to its target
			auto damage = calcDamage(
              weaponDamage(psStats,
                                      player),
                                      psStats->weaponEffect,
                                      target);

			// if we are in a multiplayer game and the attacker is our responsibility
			if (bMultiPlayer && source) {
				updateMultiStatsDamage(source->getPlayer(), 
                               target->getPlayer(), 
                               damage);
			}

			debug(LOG_NEVER, "Damage to object %d, player %d\n",
            target->getId(), target->getPlayer());

			struct Damage sDamage = {
				psObj,
				target,
				damage,
				psStats->weaponClass,
				psStats->weaponSubClass,
				time,
				false,
				(int)psStats->upgraded[player].minimumDamage
			};

			// Damage the object
			auto relativeDamage = objectDamage(&sDamage);

      // so long as the target wasn't killed
			if (relativeDamage >= 0) {
				damaged.push_back(target);
			}
		}
	}

	temp = target;
	setTarget(nullptr);
	// The damage has been done, no more damage expected from 
  // this projectile. (Ignore periodical damaging.)
	expectedDamageCaused = 0;
	setTarget(temp);

	// If the projectile does no splash damage and does not set fire to things
	if (psStats->upgraded[player].radius == 0 &&
      psStats->upgraded[player].periodicalDamageTime == 0) {
		state = PROJECTILE_STATE::INACTIVE;
		return;
	}

	if (psStats->upgraded[player].radius != 0) {
		/* An area effect bullet */
		state = PROJECTILE_STATE::POST_IMPACT;

		/* Note when it exploded for the explosion effect */
		bornTime = gameTime;

		// If projectile impacts a droid start the splash damage from the center of it, else use whatever location the projectile impacts at as the splash center.
		auto destDroid = dynamic_cast<Droid*>(target);
		Vector3i targetPos = (destDroid != nullptr) ? destDroid->getPosition() : getPosition();

		static GridList gridList; // static to avoid allocations.
		gridList = gridStartIterate(targetPos.x, targetPos.y, psStats->upgraded[player].radius);

		for (auto psCurr : gridList)
		{
			if (psCurr->died) {
				continue; // Do not damage dead objects further.
			}

			if (psCurr == target) {
				continue; // Don't hit main target twice.
			}

			if (source && source->getPlayer() == psCurr->getPlayer() && 
          psStats->flags.test(
		     static_cast<size_t>(WEAPON_FLAGS::NO_FRIENDLY_FIRE))) {
				continue; // this weapon does not do friendly damage
			}

			bool bTargetInAir = false;
			bool useSphere = false;
			bool damageable = true;
			switch (psCurr->type) {
			case OBJ_DROID:
				bTargetInAir = asPropulsionTypes[asPropulsionStats[((Droid*)psCurr)->asBits[COMP_PROPULSION]].
					propulsionType].travel == TRAVEL_MEDIUM::AIR && ((Droid*)psCurr)->movement.status != MOVE_STATUS::INACTIVE;
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
			if (useSphere && !Vector3i_InSphere(psCurr->getPosition(), targetPos, psStats->upgraded[player].radius))
			{
				continue; // Target out of range.
			}
			// The psCurr will get damaged, at this point.
			unsigned damage = calcDamage(weaponRadDamage(psStats, player), psStats->weaponEffect, psCurr);
			debug(LOG_ATTACK, "Damage to object %d, player %d : %u", psCurr->getId(), psCurr->getPlayer(), damage);
			if (bMultiPlayer && source != nullptr && psCurr->type != OBJ_FEATURE)
			{
				updateMultiStatsDamage(source->getPlayer(), psCurr->getPlayer(), damage);
			}

			struct Damage sDamage = {
				psObj,
				psCurr,
				damage,
				psStats->weaponClass,
				psStats->weaponSubClass,
				time,
				false,
				(int)psStats->upgraded[player].minimumDamage
			};

			objectDamage(&sDamage);
		}
	}

	if (psStats->upgraded[player].periodicalDamageTime != 0) {
		/* Periodical damage round */
		/* Periodical damage gets done in the bullet update routine */
		/* Just note when started damaging          */
		state = PROJECTILE_STATE::POST_IMPACT;
		bornTime = gameTime;
	}
	/* Something was blown up */
}


void Projectile::proj_PostImpactFunc()
{
	WeaponStats* psStats = weaponStats.get();
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid weapon stats pointer");

	auto age = gameTime - bornTime;

	/* Time to finish postimpact effect? */
	if (age > psStats->radiusLife &&
      age > psStats->upgraded[player].periodicalDamageTime) {
		state = PROJECTILE_STATE::INACTIVE;
		return;
	}

	/* Periodical damage effect */
	if (psStats->upgraded[player].periodicalDamageTime > 0) {
		/* See if anything is in the fire and damage it periodically */
		proj_checkPeriodicalDamage(psObj);
	}
}

void Projectile::update()
{
  previousLocation = Spacetime();

	// see if any of the stored objects have died
	// since the projectile was created
	if (source && source->died) {
		setProjectileSource(this, nullptr);
	}
	if (target && target->died) {
		setTarget(nullptr);
	}

	// remove dead objects from psDamaged.
	damaged.erase(std::remove_if(damaged.begin(), damaged.end(),
                               [](const PersistentObject* psObj)
                                 { return ::isDead(psObj); }), damaged.end());

	// This extra check fixes a crash in cam2, mission1
	if (!worldOnMap(getPosition().x, getPosition().y)) {
		died = true;
		return;
	}

	switch (state) {
  	case INFLIGHT:
  		proj_InFlightFunc();
  		if (state != IMPACT) {
  			break;
  		}
  	// fallthrough
  	case IMPACT:
  		proj_ImpactFunc();
  		if (state != POST_IMPACT) {
  			break;
  		}
  	// fallthrough
  	case POST_IMPACT:
  		proj_PostImpactFunc();
  		break;
  
  	case INACTIVE:
  		died = getTime();
  		break;
	}
}

// iterate through all projectiles and update their status
void proj_UpdateAll()
{
	// Update all projectiles. Penetrating projectiles may add to psProjectileList.
	std::for_each(psProjectileList.begin(), psProjectileList.end(), 
                std::mem_fn(&Projectile::update));

	// Remove and free dead projectiles.
	psProjectileList.erase(
		std::remove_if(psProjectileList.begin(), psProjectileList.end(), 
                   std::mem_fn(&Projectile::deleteIfDead)),
		psProjectileList.end());
}

void Projectile::proj_checkPeriodicalDamage()
{
	// note the attacker if any
	g_pProjLastAttacker = source;

	auto& psStats = weaponStats;

	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(getPosition().x, 
                              getPosition().y,
                              psStats->upgraded[player].periodicalDamageRadius);
  
	for (auto psCurr : gridList)
	{
    if (psCurr->died) {
			continue; // Do not damage dead objects further.
		}

		if (aiCheckAlliances(player, psCurr->getPlayer())) {
			continue; // Don't damage your own droids, nor ally droids - unrealistic, but better.
		}

		if (dynamic_cast<Droid*>(psCurr) &&
        dynamic_cast<Droid*>(psCurr)->isVtol() &&
        dynamic_cast<Droid*>(psCurr)->getMovementData()
          .status != MOVE_STATUS::INACTIVE) {
			continue; // Can't set flying vtols on fire.
		}

		if (psCurr->type == OBJ_FEATURE && 
        !((Feature*)psCurr)->psStats->damageable) {
			continue; // Can't destroy oil wells.
		}

		if (psCurr->periodicalDamageStart != gameTime) {
			psCurr->periodicalDamageStart = gameTime;
			psCurr->periodicalDamage = 0; // Reset periodical damage done this tick.
		}
		unsigned damageRate = calcDamage(weaponPeriodicalDamage(psStats, player),
		                                 psStats->periodicalDamageWeaponEffect, psCurr);
		debug(LOG_NEVER, "Periodical damage of %d per second to object %d, player %d\n", damageRate, psCurr->getId(),
		      psCurr->getPlayer());

		struct Damage sDamage = {
			psProj,
			psCurr,
			damageRate,
			psStats->periodicalDamageWeaponClass,
			psStats->periodicalDamageWeaponSubClass,
			gameTime - deltaGameTime / 2 + 1,
			true,
			(int)psStats->upgraded[player].minimumDamage
		};

		objectDamage(&sDamage);
	}
}

// return whether a weapon is direct or indirect
bool proj_Direct(const WeaponStats* psStats)
{
	ASSERT_OR_RETURN(false, psStats, "Called with NULL weapon");

	switch (psStats->movementModel) {
  	case MOVEMENT_MODEL::DIRECT:
  	case MOVEMENT_MODEL::HOMING_DIRECT:
  		return true;
  	case MOVEMENT_MODEL::INDIRECT:
  	case MOVEMENT_MODEL::HOMING_INDIRECT:
  		return false;
	}
}

#define ASSERT_PLAYER_OR_RETURN(retVal, player) \
	ASSERT_OR_RETURN(retVal, player >= 0 && player < MAX_PLAYERS, \
                   "Invalid player: %" PRIu32 "", player);

// return the maximum range for a weapon
unsigned proj_GetLongRange(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].maxRange;
}

// return the minimum range for a weapon
unsigned proj_GetMinRange(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].minRange;
}

// return the short range for a weapon
unsigned proj_GetShortRange(const WeaponStats* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].shortRange;
}

ObjectShape establishTargetShape(PersistentObject* psTarget)
{
	switch (psTarget->type) {
	case OBJ_DROID: // Circular.
		switch (dynamic_cast<Droid*>(psTarget)->getType()) {
      using enum DROID_TYPE;
		  case WEAPON:
		  case SENSOR:
		  case ECM:
		  case CONSTRUCT:
		  case COMMAND:
		  case REPAIRER:
		  case PERSON:
		  case CYBORG:
		  case CYBORG_CONSTRUCT:
		  case CYBORG_REPAIR:
		  case CYBORG_SUPER:
		  	//Watermelon:'hitbox' size is now based on imd size
		  	return abs(psTarget->getDisplayData().imd_shape->radius) * 2;
		  case DEFAULT:
		  case TRANSPORTER:
		  case SUPER_TRANSPORTER:
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
unsigned calcDamage(unsigned baseDamage, WEAPON_EFFECT weaponEffect, PersistentObject* psTarget)
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
		ASSERT(!"invalid object type: bullet", "invalid object type: OBJ_PROJECTILE (id=%d)", psDamage->target->getId());
		break;

	default:
		ASSERT(!"unknown object type", "unknown object type %d, id=%d", psDamage->target->type, psDamage->target->getId());
	}
	return 0;
}

bool Damage::isFriendlyFire() const
{
	return projectile->target &&
         projectile->source->getPlayer() == projectile->target->getPlayer();
}

bool Damage::shouldIncreaseExperience() const
{
	return projectile->source && 
         !dynamic_cast<Feature*>(projectile->target) && 
         !isFriendlyFire();
}

void Damage::updateKills()
{
	if (bMultiPlayer) {
		updateMultiStatsKills(
            target, projectile->source->getPlayer());
	}

	if (auto psDroid = dynamic_cast<Droid*>(projectile->source)) {
		psDroid->kills++;

		if (psDroid->hasCommander()) {
			auto& psCommander = psDroid->getGroup().getCommander();
			psCommander.kills++;
		}
	}
	else if (dynamic_cast<Structure*>(projectile->source)) {
		auto psCommander = getDesignatorAttackingObject(
            projectile->source->getPlayer(),
            projectile->target);

		if (psCommander != nullptr) {
			psCommander->kills++;
		}
	}
}

int Damage::objectDamage()
{
	auto relativeDamage = objectDamageDispatch(this);
	if (shouldIncreaseExperience()) {
		projectile->updateExperience(
            abs(relativeDamage) * getExpGain(
                    projectile->source->getPlayer()) / 100);

		if (relativeDamage < 0) {
			updateKills();
		}
	}
	return relativeDamage;
}

/// @return true if `psObj` has just been hit by an electronic warfare weapon
static bool justBeenHitByEW(PersistentObject* psObj)
{
	if (gamePaused()) {
		return false;
	}

	if (auto psDroid = dynamic_cast<Droid*>(psObj)) {
    if ((gameTime - psDroid->timeLastHit) < ELEC_DAMAGE_DURATION
        && psDroid->lastHitWeapon == WEAPON_SUBCLASS::ELECTRONIC) {
      return true;
    }
  } 
  else if (auto psFeature = dynamic_cast<Feature*>(psObj)) {
    if ((gameTime - psFeature->timeLastHit) < ELEC_DAMAGE_DURATION) {
      return true;
    }
  }
  else if (auto psStructure = dynamic_cast<Structure*>(psObj)) {
    if ((gameTime - psStructure->timeLastHit) < ELEC_DAMAGE_DURATION
        && psStructure->lastHitWeapon == WEAPON_SUBCLASS::ELECTRONIC) {
      return true;
    }
  }
  else {
    ASSERT(false, "Unknown or invalid object for EW: %s", objInfo(psObj));
    return false;
	}
	return false;
}

glm::mat4 objectShimmy(PersistentObject* psObj)
{
	if (justBeenHitByEW(psObj)) {
		const glm::mat4 rotations =
			glm::rotate(UNDEG(SKY_SHIMMY),
                  glm::vec3(1.f, 0.f, 0.f)) *
			glm::rotate(UNDEG(SKY_SHIMMY),
                  glm::vec3(0.f, 1.f, 0.f)) *
			glm::rotate(UNDEG(SKY_SHIMMY),
                  glm::vec3(0.f, 0.f, 1.f));
		if (!dynamic_cast<Droid*>(psObj))
			return rotations;
		return rotations * glm::translate(
            glm::vec3(1 - rand() % 3, 0, 1 - rand() % 3));
	}
	return glm::mat4(1.f);
}

static constexpr auto BULLET_FLIGHT_HEIGHT = 16;

int establishTargetHeight(PersistentObject const* psTarget)
{
	if (!psTarget) {
		return 0;
	}

  if (auto psDroid = dynamic_cast<const Droid*>(psTarget)) {
    auto height = asBodyStats[psDroid->asBits[COMP_BODY]].pIMD->max.y - asBodyStats[psDroid->asBits[
      COMP_BODY]].pIMD->min.y;
    auto utilityHeight = 0, yMax = 0, yMin = 0;
    // Temporaries for addition of utility's height to total height

    // VTOL's don't have pIMD either it seems...
    if (psDroid->isVtol()) {
      return (height + VTOL_HITBOX_MODIFIER);
    }
      
    switch (psDroid->getType()) {
      using enum DROID_TYPE;
      case WEAPON:
        if (numWeapons(*psDroid) > 0) {
          // Don't do this for Barbarian Propulsions as they don't possess a turret (and thus have pIMD == NULL)
          if (!psDroid->getWeapons()[0].getStats().pIMD) {
            return height;
          }
          yMax = psDroid->getWeapons()[0].getStats().pIMD->max.y;
          yMin = psDroid->getWeapons()[0].getStats().pIMD->min.y;
        }
        break;
      case SENSOR:
        yMax = (asSensorStats[psDroid->asBits[COMP_SENSOR]]).pIMD->max.y;
        yMin = (asSensorStats[psDroid->asBits[COMP_SENSOR]]).pIMD->min.y;
        break;
      case ECM:
        yMax = (asECMStats[psDroid->asBits[COMP_ECM]]).pIMD->max.y;
        yMin = (asECMStats[psDroid->asBits[COMP_ECM]]).pIMD->min.y;
        break;
      case CONSTRUCT:
        yMax = (asConstructStats[psDroid->asBits[COMP_CONSTRUCT]]).pIMD->max.y;
        yMin = (asConstructStats[psDroid->asBits[COMP_CONSTRUCT]]).pIMD->min.y;
        break;
      case REPAIRER:
        yMax = (asRepairStats[psDroid->asBits[COMP_REPAIRUNIT]]).pIMD->max.y;
        yMin = (asRepairStats[psDroid->asBits[COMP_REPAIRUNIT]]).pIMD->min.y;
        break;
      case PERSON:
      //TODO:add person 'state' checks here(stand, knee, crouch, prone etc)
      case CYBORG:
      case CYBORG_CONSTRUCT:
      case CYBORG_REPAIR:
      case CYBORG_SUPER:
      case DEFAULT:
      case TRANSPORTER:
      case SUPER_TRANSPORTER:
      // Commanders don't have pIMD either
      case COMMAND:
      case ANY:
        return height;
    }
    // TODO: check the /2 - does this really make sense? why + ?
    utilityHeight = (yMax + yMin) / 2;
    return height + utilityHeight;
  }
  else if (auto psStruct = dynamic_cast<const Structure*>(psTarget)) {
      auto& psStructureStats = psStruct->getStats();
      auto height = psStructureStats.IMDs[0]->max.y + psStructureStats.IMDs[0]->min.y;
      height -= gateCurrentOpenHeight(psStruct, gameTime, 2);
      // treat gate as at least 2 units tall, even if open, so that it's possible to hit
      return height;
  }
  else if (dynamic_cast<const Feature*>(psTarget)) {
  // Just use imd ymax+ymin
  return psTarget->getDisplayData().imd_shape->max.y +
    psTarget->getDisplayData().imd_shape->min.y;
  }
  else if (dynamic_cast<const Projectile*>(psTarget)) {
  return BULLET_FLIGHT_HEIGHT;
  }
  else {
    return 0;
  }
}

//void Projectile::checkProjectile(const char* location_description,
//                                 const char* function, int recurse) const
//{
//	if (recurse < 0) {
//		return;
//	}
//
//	ASSERT_HELPER(weaponStats != nullptr, location_description,
//                function, "CHECK_PROJECTILE");
//
//	ASSERT_HELPER(player < MAX_PLAYERS, location_description, function,
//	              "CHECK_PROJECTILE: Out of bound owning player number (%u)",
//                (unsigned int)player);
//
//  ASSERT_HELPER(state == PROJECTILE_STATE::INFLIGHT ||
//                state == PROJECTILE_STATE::IMPACT ||
//                state == PROJECTILE_STATE::POST_IMPACT ||
//                state == PROJECTILE_STATE::INACTIVE,
//                location_description, function,
//	              "CHECK_PROJECTILE: invalid projectile state: %u",
//                static_cast<unsigned>(state));
//}

void Projectile::setSource(::PersistentObject *psObj)
{
	// use the source of the source of psProj if psAttacker is a projectile
	source = nullptr;
	if (psObj == nullptr) {
    return;
  }
	else if (auto psPrevProj = dynamic_cast<Projectile*>(psObj)) {
		if (psPrevProj->source && !psPrevProj->source->died) {
			source = psPrevProj->source;
		}
	}
	else {
		source = psObj;
	}
}
