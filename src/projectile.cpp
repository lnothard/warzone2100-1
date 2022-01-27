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

#include "lib/framework/math_ext.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "action.h"
#include "ai.h"
#include "display3d.h"
#include "displaydef.h"
#include "droid.h"
#include "effects.h"
#include "feature.h"
#include "map.h"
#include "mapgrid.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "scores.h"
#include "structure.h"
#include "weapon.h"

bool bMultiPlayer;
bool gamePaused();
Droid* cmdDroidGetDesignator(unsigned);
void updateMultiStatsDamage(unsigned, unsigned, unsigned);
void updateMultiStatsKills(BaseObject*, unsigned);
int mapWidth, mapHeight;
static const unsigned max_check_object_recursion = 4;
int areaOfFire(const BaseObject *, const BaseObject*, int, bool);
void cmdDroidUpdateExperience(Droid*, unsigned);
void counterBatteryFire(BaseObject*, BaseObject*);
Spacetime interpolateObjectSpacetime(const BaseObject *, unsigned);
unsigned objGuessFutureDamage(WeaponStats const*, unsigned, BaseObject const*);


// used to create a specific ID for projectile objects to facilitate tracking them.
static const auto ProjectileTrackerID = 0xdead0000;
static auto projectileTrackerIDIncrement = 0;

/* The list of projectiles in play */
static std::vector<std::unique_ptr<Projectile>> psProjectileList;

// the last unit that did damage - used by script functions
BaseObject* g_pProjLastAttacker;

static void proj_ImpactFunc(Projectile* psObj);
static void proj_PostImpactFunc(Projectile* psObj);
static void proj_checkPeriodicalDamage(Projectile* psProj);

static int objectDamage(Damage* psDamage);


struct Projectile::Impl
{
  ~Impl() = default;
  Impl() = default;

  Impl(Impl const& rhs);
  Impl& operator=(Impl const& rhs);

  Impl(Impl&& rhs) noexcept = default;
  Impl& operator=(Impl&& rhs) noexcept = default;


  PROJECTILE_STATE state = PROJECTILE_STATE::INACTIVE;
  std::unique_ptr<Damage> damage;
  /// Whether the selected player should see the projectile
  bool isVisible = false;
  /// Firing weapon stats
  std::shared_ptr<WeaponStats> weaponStats;
  /// What fired the projectile
  BaseObject* source = nullptr;
  /// Target of this projectile (not a Unit because it can
  /// be a feature I guess)
  BaseObject* target = nullptr;
  /// Targets that have already been dealt damage to (don't
  /// damage the same target twice)
  std::vector<BaseObject*> damaged;
  /// Where projectile started
  Vector3i origin{};
  /// The target coordinates
  Vector3i destination{};
  /// Axis velocities
  int vZ = 0;
  int vXY = 0;
  /// Expected damage that this projectile will cause to the target
  int expectedDamageCaused = 0;
  /// How much of target was visible on shooting (important for homing)
  int partVisible = 0;
};

struct ObjectShape::Impl
{
  Impl() = default;
  Impl(int width, int length);
  explicit Impl(int radius);
  explicit Impl(Vector2i size);

  /// Equal to \c true if rectangular, \c false if circular
  bool isRectangular = false;
  /// \c x == y if circular
  Vector2i size{};
};

Projectile::Projectile(unsigned id, unsigned player)
  : BaseObject(id, std::make_unique<PlayerManager>(player),
               std::make_unique<DamageManager>())
  , pimpl{std::make_unique<Impl>()}
{
}

Projectile::Projectile(Projectile const& rhs)
  : BaseObject(rhs)
  , pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

Projectile& Projectile::operator=(Projectile const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  *damageManager = *rhs.damageManager;
  *playerManager = *rhs.playerManager;
  return *this;
}

Projectile::Impl::Impl(Impl const& rhs)
  : damage{std::make_unique<Damage>(*rhs.damage)}
  , state{rhs.state}
  , isVisible{rhs.isVisible}
  , weaponStats{rhs.weaponStats}
  , source{rhs.source}
  , target{rhs.target}
  , damaged{rhs.damaged}
  , origin{rhs.origin}
  , destination{rhs.destination}
  , vZ{rhs.vZ}
  , vXY{rhs.vXY}
  , expectedDamageCaused{rhs.expectedDamageCaused}
  , partVisible{rhs.partVisible}
{
}

Projectile::Impl& Projectile::Impl::operator=(Impl const& rhs)
{
  if (this == &rhs) return *this;

  *damage = *rhs.damage;
  state = rhs.state;
  isVisible = rhs.isVisible;
  weaponStats = rhs.weaponStats;
  source = rhs.source;
  target = rhs.target;
  damaged = rhs.damaged;
  origin = rhs.origin;
  destination = rhs.destination;
  vZ = rhs.vZ;
  vXY = rhs.vXY;
  expectedDamageCaused = rhs.expectedDamageCaused;
  partVisible = rhs.partVisible;
  return *this;
}

ObjectShape::ObjectShape()
  : pimpl{std::make_unique<Impl>()}
{
}

ObjectShape::ObjectShape(int length, int width)
  : pimpl{std::make_unique<Impl>(length, width)}
{
}

ObjectShape::ObjectShape(int radius)
  : pimpl{std::make_unique<Impl>(radius)}
{
}

ObjectShape::ObjectShape(Vector2i size)
  : pimpl{std::make_unique<Impl>(size)}
{
}

ObjectShape::ObjectShape(ObjectShape const& rhs)
  : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

ObjectShape& ObjectShape::operator=(ObjectShape const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

ObjectShape::Impl::Impl(int width, int length)
  : size{width, length}
  , isRectangular{true}
{
}

ObjectShape::Impl::Impl(int radius)
  : size{radius, radius}
  , isRectangular{false}
{
}

ObjectShape::Impl::Impl(Vector2i size)
  : size{size}
{
}

bool ObjectShape::isRectangular() const
{
  return pimpl && pimpl->isRectangular;
}

Vector2i ObjectShape::getSize() const
{
  return pimpl ? pimpl->size : Vector2i();
}

int ObjectShape::radius() const
{
  return pimpl ? pimpl->size.x : -1;
}

PROJECTILE_STATE Projectile::getState() const noexcept
{
  return pimpl ? pimpl->state : PROJECTILE_STATE::INACTIVE;
}

void Projectile::setTarget(BaseObject* psObj)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Projectile object is undefined");
	bool bDirect = proj_Direct(pimpl->weaponStats.get());
	aiObjectAddExpectedDamage(pimpl->target,
                            -pimpl->expectedDamageCaused,
                            bDirect);

	pimpl->target = psObj;
	aiObjectAddExpectedDamage(pimpl->target,
                            pimpl->expectedDamageCaused,
                            bDirect);
}

bool Projectile::isVisible() const
{
  ASSERT_OR_RETURN(false, pimpl != nullptr, "Projectile object is undefined");

	// already know it is visible
	if (pimpl->isVisible) return true;

	// you fired it
	if (playerManager->getPlayer() == selectedPlayer) return true;

	// someone else's structure firing at something you can't see
	if (pimpl->source != nullptr &&
      !pimpl->source->damageManager->isDead() &&
      dynamic_cast<Structure*>(pimpl->source) &&
      !pimpl->source->playerManager->isSelectedPlayer() &&
      (pimpl->target == nullptr || pimpl->target->damageManager->isDead() ||
       !pimpl->target->isVisibleToSelectedPlayer())) {
		return false;
	}

	// something you cannot see firing at a structure that isn't yours
	if (pimpl->target != nullptr &&
      !pimpl->target->damageManager->isDead() &&
      dynamic_cast<Structure*>(pimpl->target) &&
      !pimpl->target->playerManager->isSelectedPlayer() &&
      (pimpl->source == nullptr || !pimpl->source->isVisibleToSelectedPlayer())) {
		return false;
	}

	// you can see the source
	if (pimpl->source != nullptr && !pimpl->source->damageManager->isDead() &&
      pimpl->source->isVisibleToSelectedPlayer()) {
		return true;
	}

	// you can see the destination
	if (pimpl->target != nullptr && !pimpl->target->damageManager->isDead() &&
      pimpl->target->isVisibleToSelectedPlayer()) {
		return true;
	}

	return false;
}

void Projectile::updateExperience(unsigned experienceInc)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Projectile object is undefined");

  /* update droid kills */
  if (auto psDroid = dynamic_cast<Droid*>(pimpl->source)) {
    // if it is 'droid-on-droid' then modify the experience by the
    // quality factor. only do this in MP so to not unbalance the campaign
    if (pimpl->target && dynamic_cast<Droid*>(pimpl->target) && bMultiPlayer) {
      // modify the experience gained by the 'quality factor' of the units
      experienceInc = experienceInc * qualityFactor(
              psDroid, dynamic_cast<Droid*>(pimpl->target)) / 65536;
    }
    ASSERT_OR_RETURN(, experienceInc < (int)(2.1 * 65536), "Experience increase out of range");
    psDroid->gainExperience(experienceInc);
    cmdDroidUpdateExperience(psDroid, experienceInc);

    auto psSensor = orderStateObj(psDroid, ORDER_TYPE::FIRE_SUPPORT);
    if (psSensor && dynamic_cast<Droid*>(psSensor)) {
      dynamic_cast<Droid*>(psSensor)->gainExperience(experienceInc);
    }
  }
  else if (dynamic_cast<Structure*>(pimpl->source)) {
    ASSERT_OR_RETURN(, experienceInc < (int)(2.1 * 65536),
                       "Experience increase out of range");
    psDroid = getDesignatorAttackingObject(pimpl->source->playerManager->getPlayer(), pimpl->target);

    if (psDroid) {
      psDroid->gainExperience(experienceInc);
    }
  }
}

bool Projectile::proj_SendProjectileAngled(Weapon* psWeap, BaseObject* psAttacker, unsigned plr,
                                           Vector3i dest, BaseObject* psTarget, bool bVisible,
                                           int weapon_slot, int min_angle, unsigned fireTime) const
{
  ASSERT_OR_RETURN(false, pimpl != nullptr, "Projectile object is undefined");
  auto psStats = psWeap->getStats();

  ASSERT_OR_RETURN(false, psTarget == nullptr || !psTarget->damageManager->isDead(), "Aiming at dead target!");

  auto psProj = std::make_unique<Projectile>(
          ProjectileTrackerID + ++projectileTrackerIDIncrement, plr);

  /* get muzzle offset */
  if (psAttacker == nullptr) {
    // if there isn't an attacker just start at the target position
    // NB this is for the script function to fire the las sats
    psProj->pimpl->origin = dest;
  }
  else if (dynamic_cast<Droid*>(psAttacker) && weapon_slot >= 0) {
    calcDroidMuzzleLocation(dynamic_cast<Droid*>(psAttacker),
                              &psProj->pimpl->origin, weapon_slot);

    // update attack runs for VTOL droid's each time a shot is fired
    updateVtolAttackRun(*dynamic_cast<Droid*>(psAttacker), weapon_slot);
  }
  else if (dynamic_cast<Structure*>(psAttacker) && weapon_slot >= 0) {
    calcStructureMuzzleLocation(dynamic_cast<Structure*>(psAttacker),
                                  &psProj->pimpl->origin, weapon_slot);
  }
  else { // incase anything wants a projectile
    psProj->pimpl->origin = psAttacker->getPosition();
  }

  /* Initialise the structure */
  psProj->pimpl->weaponStats = std::make_shared<WeaponStats>(*psStats);

  psProj->setPosition(psProj->pimpl->origin);
  psProj->pimpl->destination = dest;

  psProj->pimpl->isVisible = false;

  // Must set ->psDest and ->expectedDamageCaused before first call to setProjectileDestination().
  psProj->pimpl->target = nullptr;
  psProj->pimpl->expectedDamageCaused = objGuessFutureDamage(psStats, plr, psTarget);
  psProj->setTarget(psTarget);
  // Updates expected damage of psProj->psDest, using psProj->expectedDamageCaused.

  /*
	When we have been created by penetration (spawned from another projectile),
	we shall live no longer than the original projectile may have lived
	*/
  if (psAttacker && dynamic_cast<Projectile*>(psAttacker)) {
    auto psOldProjectile = dynamic_cast<Projectile*>(psAttacker);
    psProj->pimpl->bornTime = psOldProjectile->pimpl->bornTime;
    psProj->pimpl->origin = psOldProjectile->pimpl->origin;

    // have partially ticked already
    psProj->pimpl->previousLocation.time = psOldProjectile->getTime();
    psProj->setTime(gameTime);

    // times should not be equal, for interpolation
    psProj->pimpl->previousLocation.time -= psProj->
           pimpl->previousLocation.time == psProj->getTime();

    psProj->setSource(psOldProjectile->pimpl->source);
    psProj->pimpl->damaged = psOldProjectile->pimpl->damaged;

    // TODO Should finish the tick, when penetrating.
  }
  else {
    psProj->pimpl->bornTime = fireTime; // Born at the start of the tick.

    psProj->setSource(psAttacker);
    psProj->setTime(fireTime);
    psProj->setPreviousLocation(
            {fireTime, getPreviousLocation().position,
             getPreviousLocation().rotation});
  }

  if (psTarget) {
    auto maxHeight = establishTargetHeight(psTarget);
    auto minHeight = std::min(
            std::max(maxHeight + 2 * LINE_OF_FIRE_MINIMUM - areaOfFire(
                    psAttacker, psTarget, weapon_slot, true), 0), maxHeight);
    scoreUpdateVar(WD_SHOTS_ON_TARGET);

    psProj->pimpl->destination.z = psTarget->getPosition().z +
                            minHeight + gameRand(std::max(maxHeight - minHeight, 1));
    /* store visible part (LOCK ON this part for homing :) */
    psProj->pimpl->partVisible = maxHeight - minHeight;
  }
  else {
    psProj->pimpl->destination.z = dest.z + LINE_OF_FIRE_MINIMUM;
    scoreUpdateVar(WD_SHOTS_OFF_TARGET);
  }

  auto deltaPos = psProj->pimpl->destination - psProj->pimpl->origin;

  /* roll never set */
  auto roll = 0;
  auto direction = iAtan2(deltaPos.xy());

  // get target distance, horizontal distance only
  auto dist = iHypot(deltaPos.xy());

  int pitch;
  if (proj_Direct(psStats)) {
    pitch = iAtan2(deltaPos.z, dist);
  }
  else {
    /* indirect */
    projCalcIndirectVelocities(dist, deltaPos.z, psStats->flightSpeed,
                               &psProj->pimpl->vXY, &psProj->pimpl->vZ, min_angle);
    pitch = iAtan2(psProj->pimpl->vZ, psProj->pimpl->vXY);
  }

  psProj->setRotation({direction, pitch, roll});

  psProj->pimpl->state = PROJECTILE_STATE::INFLIGHT;

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
  if (bVisible || psProj->isVisible()) {
    // note that the projectile is visible
    psProj->pimpl->isVisible = true;

    if (psStats->iAudioFireID != NO_SOUND) {
      if (psProj->pimpl->source) {
        /* firing sound emitted from source */
        audio_PlayObjDynamicTrack(psProj->pimpl->source,
                                  psStats->iAudioFireID, nullptr);
        /* GJ HACK: move howitzer sound with shell */
        if (psStats->weaponSubClass == WEAPON_SUBCLASS::HOWITZERS)
        {
          audio_PlayObjDynamicTrack(psProj.get(),
                                    ID_SOUND_HOWITZ_FLIGHT, nullptr);
        }
      }
        // don't play the sound for a LasSat in multiplayer
      else if (!(bMultiPlayer && psStats->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT)) {
        audio_PlayObjStaticTrack(psProj.get(), psStats.iAudioFireID);
      }
    }
  }

  if (psAttacker != nullptr && !proj_Direct(psStats)) {
    // check for counter-battery sensor in range of target
    counterBatteryFire(psAttacker, psTarget);
  }

  return true;
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

Droid* getDesignatorAttackingObject(unsigned player, BaseObject* target)
{
	const auto psCommander = cmdDroidGetDesignator(player);

	return psCommander != nullptr && 
         psCommander->getAction() == ACTION::ATTACK &&
         psCommander->getActionTarget(0) == target
		       ? psCommander
		       : nullptr;
}


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

bool proj_SendProjectile(Weapon* psWeap, BaseObject* psAttacker, unsigned player,
                         Vector3i target, BaseObject* psTarget, bool bVisible,
                         int weapon_slot)
{
	return proj_SendProjectileAngled(psWeap, psAttacker, player, target, psTarget,
                                   bVisible, weapon_slot, 0, gameTime - 1);
}


static Interval intervalIntersection(Interval i1, Interval i2)
{
	return {MAX(i1.begin, i2.begin), MIN(i1.end, i2.end)};;
}

bool Interval::isEmpty() const
{
	return begin >= end;
}

static Interval collisionZ(int z1, int z2, int height)
{
	auto ret = Interval{-1, -1};
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
	auto empty = Interval {-1, -1};
	auto full = Interval {0, 1024};

	if (d < 0) {
    // missed
		return empty; 
	}
	if (a == 0) {
    // not moving. see if inside the target
		return c < 0 ? full : empty; 
	}

	auto sd = i64Sqrt(d);
  return {MAX(0, 1024 * (-b - sd) / a), MIN(1024, 1024 * (-b + sd) / a)};
}

static int collisionXYZ(Vector3i v1, Vector3i v2, ObjectShape const& shape, int height)
{
	auto i = collisionZ(v1.z, v2.z, height);
  // don't bother checking x and y unless z passes
  if (i.isEmpty()) return -1;
  if (shape.isRectangular()) {
    i = intervalIntersection(i, collisionZ(
            v1.x, v2.x, shape.getSize().x));
    // don't bother checking y unless x and z pass
    if (!i.isEmpty()) {
      i = intervalIntersection(i, collisionZ(
              v1.y, v2.y, shape.getSize().y));
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
  return -1;
}

void Projectile::proj_InFlightFunc()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Projectile object is undefined");

	/* we want a delay between Las-Sats firing and actually hitting in multiPlayer
	magic number but that's how long the audio countdown message lasts! */
	const auto LAS_SAT_DELAY = 4;
	BaseObject* closestCollisionObject = nullptr;
	Spacetime closestCollisionSpacetime;

	auto timeSoFar = gameTime - bornTime;

	setTime(gameTime);
	auto deltaProjectileTime = getTime() - getPreviousLocation().time;

	auto psStats = pimpl->weaponStats;
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid weapon stats pointer");

	/* we want a delay between Las-Sats firing and actually hitting in multiPlayer
	magic number but that's how long the audio countdown message lasts! */
	if (bMultiPlayer && psStats->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT &&
		  timeSoFar < LAS_SAT_DELAY * GAME_TICKS_PER_SEC) {
		return;
	}

	/* Calculate movement vector: */
  
	auto currentDistance = 0;
	switch (psStats->movementModel) {
    using enum MOVEMENT_MODEL;
	  case DIRECT: // Go in a straight line.
	  	{
	  		Vector3i delta = pimpl->destination - pimpl->origin;
	  		if (psStats->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT) {
	  			// LAS_SAT doesn't have a z
	  			delta.z = 0;
	  		}
	  		auto targetDistance = std::max(iHypot(delta.xy()), 1);
	  		currentDistance = timeSoFar * psStats->flightSpeed / GAME_TICKS_PER_SEC;
	  		setPosition(pimpl->origin + delta * currentDistance / targetDistance);
	  		break;
	  	}

	  case INDIRECT: // ballistic trajectory
	  	{
	  		auto delta = pimpl->destination - pimpl->origin;
	  		delta.z = (pimpl->vZ - (timeSoFar * ACC_GRAVITY / (GAME_TICKS_PER_SEC * 2))) * timeSoFar /
	  			GAME_TICKS_PER_SEC; // '2' because we reach our highest point in the mid of flight, when "vZ is 0".
	  		auto targetDistance = std::max(iHypot(delta.xy()), 1);
	  		currentDistance = timeSoFar * pimpl->vXY / GAME_TICKS_PER_SEC;
	  		setPosition(pimpl->origin + delta * currentDistance / targetDistance);
	  		setPosition({getPosition().x, getPosition().y,
                     pimpl->origin.z + delta.z}); // use raw z value

	  		auto pitch = iAtan2(pimpl->vZ - (timeSoFar * ACC_GRAVITY / GAME_TICKS_PER_SEC), pimpl->vXY);
        setRotation({getRotation().direction, pitch, getRotation().roll});
	  		break;
	  	}

	  case HOMING_DIRECT: // Fly towards target, even if target moves.
	  case HOMING_INDIRECT: // Fly towards target, even if target moves. Avoid terrain.
	  	{
	  		if (pimpl->target) {
	  			if (psStats->movementModel == HOMING_DIRECT) {
	  				// If it's homing and has a target (not a miss)...
	  				// Home at the centre of the part that was visible when firing.
	  				pimpl->destination = pimpl->target->getPosition() + Vector3i(
	  					0, 0, establishTargetHeight(pimpl->target) - pimpl->partVisible / 2);
	  			}
	  			else {
	  				pimpl->destination = pimpl->target->getPosition() + Vector3i{
                    0, 0, establishTargetHeight(pimpl->target) / 2};
	  			}
	  			auto targetDroid = dynamic_cast<Droid*>(pimpl->target);
	  			if (targetDroid != nullptr) {
	  				// Do target prediction.
	  				auto delta = pimpl->destination - getPosition();
	  				auto flightTime = iHypot(delta.xy()) * GAME_TICKS_PER_SEC / psStats->flightSpeed;
	  			pimpl->destination += Vector3i(
                  iSinCosR(targetDroid->getMovementData()->moveDir,
                           std::min<int>(targetDroid->getMovementData()->speed,
                                         psStats->flightSpeed * 3 / 4) * flightTime / GAME_TICKS_PER_SEC), 0);
	  			}
	  			pimpl->destination.x = clip(
                  pimpl->destination.x, 0, world_coord(mapWidth) - 1);
	  			pimpl->destination.y = clip(
                  pimpl->destination.y, 0, world_coord(mapHeight) - 1);
	  		}
	  		if (psStats->movementModel == HOMING_INDIRECT) {
	  			if (pimpl->target == nullptr) {
	  				pimpl->destination.z = map_Height(getPosition().xy()) - 1;
	  				// target missing, so just home in on the ground under where the target was
	  			}
	  			int horizontalTargetDistance = iHypot((pimpl->destination - getPosition()).xy());
	  			int terrainHeight = std::max(map_Height(getPosition().xy()),
	  			                             map_Height(getPosition().xy() + iSinCosR(
	  				                             iAtan2((pimpl->destination - getPosition()).xy()),
	  				                             psStats->flightSpeed * 2 * deltaProjectileTime / GAME_TICKS_PER_SEC)));
	  			int desiredMinHeight = terrainHeight +
	  				std::min(horizontalTargetDistance / 4, HOMINGINDIRECT_HEIGHT_MIN);
	  			int desiredMaxHeight = std::max(pimpl->destination.z, terrainHeight + HOMINGINDIRECT_HEIGHT_MAX);
	  			int heightError = getPosition().z - clip(getPosition().z, desiredMinHeight, desiredMaxHeight);
	  			pimpl->destination.z -= horizontalTargetDistance * heightError * 2 / HOMINGINDIRECT_HEIGHT_MIN;
	  		}
	  		Vector3i delta = pimpl->destination - getPosition();
	  		int targetDistance = std::max(iHypot(delta), 1);
	  		if (pimpl->target == nullptr && targetDistance < 10000 &&
            psStats->movementModel == HOMING_DIRECT) {
	  			pimpl->destination = getPosition() + delta * 10; // Target missing, so just keep going in a straight line.
	  		}
	  		currentDistance = timeSoFar * psStats->flightSpeed / GAME_TICKS_PER_SEC;
	  		Vector3i step = quantiseFraction(
                delta * int(psStats->flightSpeed),
                GAME_TICKS_PER_SEC * targetDistance,
                getTime(), getPreviousLocation().time);

	  		if (psStats->movementModel == HOMING_INDIRECT && pimpl->target != nullptr) {
	  			for (int tries = 0; tries < 10 && map_LineIntersect(
                  getPreviousLocation().position, getPosition() + step,
                  iHypot(step)) < targetDistance - 1u; ++tries)
	  			{
	  				pimpl->destination.z += iHypot((pimpl->destination - getPosition()).xy());
	  				// Would collide with terrain this tick, change trajectory.
	  				// Recalculate delta, targetDistance and step.
	  				delta = pimpl->destination - getPosition();
	  				targetDistance = std::max(iHypot(delta), 1);
	  				step = quantiseFraction(delta * int(psStats->flightSpeed), GAME_TICKS_PER_SEC * targetDistance,
                                    getTime(), getPreviousLocation().time);
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
	for (auto psTempObj : gridList)
	{
		if (std::find(pimpl->damaged.begin(), pimpl->damaged.end(),
                  psTempObj) != pimpl->damaged.end()) {
			// don't damage the same target twice
			continue;
		}
		else if (psTempObj->damageManager->isDead()) {
			// do not damage dead objects further
			continue;
		}
		else if (dynamic_cast<Feature*>(psTempObj) &&
             !dynamic_cast<Feature*>(psTempObj)->getStats()->damageable) {
			// ignore oil resources, artifacts and other pickups
			continue;
		}
		else if (aiCheckAlliances(psTempObj->playerManager->getPlayer(),
                              playerManager->getPlayer()) &&
            psTempObj != pimpl->target) {
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
            ? dynamic_cast<Droid*>(psTempObj)->getPreviousLocation().position
            : psTempObj->getPosition();

		const Vector3i diff = getPosition() - psTempObj->getPosition();
		const Vector3i prevDiff = getPreviousLocation().position - psTempObjPrevPos;
		const auto targetHeight = establishTargetHeight(psTempObj);
		const auto targetShape = establishTargetShape(psTempObj);
		const auto collision = collisionXYZ(
            prevDiff, diff, targetShape, targetHeight);

		const auto collisionTime = getPreviousLocation().time
                               + (getTime() - getPreviousLocation().time) * collision / 1024;

		if (collision >= 0 &&
        collisionTime < closestCollisionSpacetime.time) {
			// We hit!
			closestCollisionSpacetime = interpolateObjectSpacetime(this, collisionTime);
			closestCollisionObject = psTempObj;

			// Keep testing for more collisions, in case there was a closer target.
		}
	}

	auto terrainIntersectTime = map_LineIntersect(getPreviousLocation().position, getPosition(),
                                                    getTime() - getPreviousLocation().time);
	if (terrainIntersectTime != UINT32_MAX) {
		const uint collisionTime = getPreviousLocation().time + terrainIntersectTime;
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
		if (getTime() == getPreviousLocation().time) {
			--previousLocation.time;
		}
		setTarget(closestCollisionObject); // We hit something.

		// Buildings and terrain cannot be penetrated and we need a penetrating weapon, and projectile should not have already travelled further than 1.25 * maximum range.
		if (closestCollisionObject && 
        dynamic_cast<Droid*>(closestCollisionObject) &&
        psStats->penetrate &&
			  currentDistance < static_cast<int>(
                1.25 * proj_GetLongRange(psStats.get(),
                                         playerManager->getPlayer()))) {
			Weapon asWeap;
			asWeap.stats = psStats;

			// Assume we damaged the chosen target
			pimpl->damaged.push_back(closestCollisionObject);

			proj_SendProjectile(&asWeap, this, playerManager->getPlayer(),
                          pimpl->destination, nullptr, true, -1);
		}

		pimpl->state = PROJECTILE_STATE::IMPACT;

		return;
	}

	if (currentDistance * 100 >= 
        proj_GetLongRange(psStats.get(),
                          playerManager->getPlayer()) * psStats->distanceExtensionFactor) {
		// We've travelled our maximum range.
		pimpl->state = PROJECTILE_STATE::IMPACT;
		setTarget(nullptr); /* miss registered if NULL target */
		return;
	}

  if (!isVisible()) return;

  /* Paint effects if visible */
  for (auto effectTime = getPreviousLocation().time + 31 & ~31;
       effectTime < getTime(); effectTime += 32)
  {
    auto st = interpolateObjectSpacetime(this, effectTime);
    auto posFlip = st.position.xzy();
    switch (psStats->weaponSubClass) {
      case WEAPON_SUBCLASS::FLAME:
        posFlip.z -= 8; // Why?
        effectGiveAuxVar(PERCENT(currentDistance, proj_GetLongRange(
                psStats.get(), playerManager->getPlayer())));

        addEffect(&posFlip, EFFECT_GROUP::EXPLOSION,
                  EFFECT_TYPE::EXPLOSION_TYPE_FLAMETHROWER,
                  false, nullptr, 0, effectTime);
        break;
      case WEAPON_SUBCLASS::COMMAND:
      case WEAPON_SUBCLASS::ELECTRONIC:
      case WEAPON_SUBCLASS::EMP:
        posFlip.z -= 8; // Why?
        effectGiveAuxVar(PERCENT(currentDistance, proj_GetLongRange(
                psStats.get(), playerManager->getPlayer())) / 2);

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

void Projectile::proj_ImpactFunc()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Projectile object is undefined");
	auto psStats = pimpl->weaponStats;
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid weapon stats pointer");

	// note the attacker if any
	g_pProjLastAttacker = pimpl->source;
  Vector3i position;

  if (!isVisible()) {}
  /* play impact audio */
  else {
    if (psStats->iAudioImpactID == NO_SOUND) {
      /* play richochet if MG */
      if (pimpl->target != nullptr &&
          psStats->weaponSubClass == WEAPON_SUBCLASS::MACHINE_GUN && ONEINTHREE) {
        auto iAudioImpactID = ID_SOUND_RICOCHET_1 + (rand() % 3);
        audio_PlayStaticTrack(pimpl->target->getPosition().x,
                              pimpl->target->getPosition().y,
                              iAudioImpactID);
      }
    }
    else {
      audio_PlayStaticTrack(getPosition().x,
                            getPosition().y,
                            psStats->iAudioImpactID);
    }

    // Shouldn't need to do this check but the stats aren't all at a value yet... FIXME
    if (psStats->upgraded[playerManager->getPlayer()].periodicalDamageRadius != 0 &&
        psStats->upgraded[playerManager->getPlayer()].periodicalDamageTime != 0) {
      position.x = getPosition().x;
      position.z = getPosition().y;// z = y [sic] intentional
      position.y = map_Height(position.x, position.z);
      effectGiveAuxVar(psStats->upgraded[playerManager->getPlayer()].periodicalDamageRadius);
      effectGiveAuxVarSec(psStats->upgraded[playerManager->getPlayer()].periodicalDamageTime);

      addEffect(&position, EFFECT_GROUP::FIRE,
                EFFECT_TYPE::FIRE_TYPE_LOCALISED,
                false, nullptr, 0, getTime());
    }

    // may want to add both a fire effect and the las sat effect
    if (psStats->weaponSubClass == WEAPON_SUBCLASS::LAS_SAT) {
      position.x = getPosition().x;
      position.z = getPosition().y;// z = y [sic] intentional
      position.y = map_Height(position.x, position.z);
      addEffect(&position, EFFECT_GROUP::SAT_LASER,
                EFFECT_TYPE::SAT_LASER_STANDARD,
                false, nullptr, 0, getTime());

      if (clipXY(getPosition().x, getPosition().y)) {
        // takes out lots of stuff so shake length is greater
        shakeStart(1800);
      }
    }
  }

  if (psStats->upgraded[playerManager->getPlayer()].periodicalDamageRadius &&
      psStats->upgraded[playerManager->getPlayer()].periodicalDamageTime) {
    tileSetFire(getPosition().x, getPosition().y,
                psStats->upgraded[playerManager->getPlayer()].periodicalDamageTime);
  }

  // set the effects position and radius
  position.x = getPosition().x;
  position.z = getPosition().y; // z = y [sic] intentional
  position.y = getPosition().z; // y = z [sic] intentional
  auto x = psStats->upgraded[playerManager->getPlayer()].radius;
  auto y = 0;
  auto z = psStats->upgraded[playerManager->getPlayer()].radius;
  auto scatter = Vector3i{x, y, z};

  iIMDShape* imd = nullptr;
  // If the projectile missed its target (or the target died)
  if (pimpl->target == nullptr) {
    if (isVisible()) {
      // Get if we are facing or not
      EFFECT_TYPE facing = (psStats->facePlayer
                            ? EFFECT_TYPE::EXPLOSION_TYPE_SPECIFIED
                            : EFFECT_TYPE::EXPLOSION_TYPE_NOT_FACING);

      // the graphic to show depends on if we hit water or not
      if (terrainType(mapTile(
              map_coord(getPosition().x),
              map_coord(getPosition().y))) == TER_WATER) {
        imd = psStats->pWaterHitGraphic.get();
      }
        // we did not hit water, the regular miss graphic will do the trick
      else {
        imd = psStats->pTargetMissGraphic.get();
      }

      addMultiEffect(&position, &scatter,
                     EFFECT_GROUP::EXPLOSION,
                     facing, true, imd,
                     psStats->numExplosions, psStats->lightWorld,
                     psStats->effectSize, getTime());

      // If the target was a VTOL hit in the air add smoke
      if ((psStats->surfaceToAir & SHOOT_IN_AIR) &&
          !(psStats->surfaceToAir & SHOOT_ON_GROUND)) {
        addMultiEffect(&position, &scatter, EFFECT_GROUP::SMOKE,
                       EFFECT_TYPE::SMOKE_TYPE_DRIFTING, false,
                       nullptr, 3, 0, 0, getTime());
      }
    }
  }
    // The projectile hit its intended target
  else {
    if (dynamic_cast<Feature*>(pimpl->target) &&
        dynamic_cast<Feature*>(pimpl->target)->getStats()->damageable == 0) {
      debug(LOG_NEVER, "proj_ImpactFunc: trying to damage "
                       "non-damageable target, projectile removed");
      pimpl->state = PROJECTILE_STATE::INACTIVE;
      return;
    }

    if (isVisible()) {
      // Get if we are facing or not
      EFFECT_TYPE facing = (psStats->facePlayer
                            ? EFFECT_TYPE::EXPLOSION_TYPE_SPECIFIED
                            : EFFECT_TYPE::EXPLOSION_TYPE_NOT_FACING);

      // if we hit a VTOL with an AA gun use the miss graphic and add some smoke
      if ((psStats->surfaceToAir & SHOOT_IN_AIR) &&
          !(psStats->surfaceToAir & SHOOT_ON_GROUND) &&
          psStats->weaponSubClass == WEAPON_SUBCLASS::AA_GUN) {
        imd = psStats->pTargetMissGraphic.get();

        addMultiEffect(&position, &scatter,
                       EFFECT_GROUP::SMOKE,
                       EFFECT_TYPE::SMOKE_TYPE_DRIFTING,
                       false, nullptr, 3,
                       0, 0, getTime());
      }
        // otherwise, we just hit it plain and simple
      else {
        imd = psStats->pTargetHitGraphic.get();
      }

      addMultiEffect(&position, &scatter, EFFECT_GROUP::EXPLOSION,
                     facing, true, imd, psStats->numExplosions,
                     psStats->lightWorld, psStats->effectSize, getTime());
    }

    // check for electronic warfare damage where we know the subclass and source
    if (proj_Direct(psStats.get()) &&
        psStats->weaponSubClass == WEAPON_SUBCLASS::ELECTRONIC && pimpl->source) {
      // if we did enough `damage' to capture the target
      if (electronicDamage(pimpl->target,
                           calcDamage(
                                   weaponDamage(psStats.get(), playerManager->getPlayer()),
                                   psStats->weaponEffect, pimpl->target), playerManager->getPlayer())) {

        if (auto psDroid = dynamic_cast<Droid*>(pimpl->source)) {
          psDroid->order.type = ORDER_TYPE::NONE;
          actionDroid(psDroid, ACTION::NONE);
        }
        else if (auto psStruct = dynamic_cast<Structure*>(pimpl->source)) {
          psStruct->psTarget[0] = nullptr;
        }
      }
    }
      // else it is just a regular weapon (direct or indirect)
    else {
      // Calculate the damage the weapon does to its target
      auto damage = calcDamage(weaponDamage(
                                        psStats.get(), playerManager->getPlayer()),
                               psStats->weaponEffect, pimpl->target);

      // if we are in a multiplayer game and the attacker is our responsibility
      if (bMultiPlayer && pimpl->source) {
        updateMultiStatsDamage(dynamic_cast<PlayerManager *>(pimpl->source)->getPlayer(),
                               dynamic_cast<PlayerManager *>(pimpl->target)->getPlayer(),
                               damage);
      }

      debug(LOG_NEVER, "Damage to object %d, player %d\n",
            pimpl->target->getId(), dynamic_cast<PlayerManager *>(pimpl->target)->getPlayer());

      struct Damage sDamage = {
              pimpl->target,
              damage,
              getTime(),
              false,
              (int)psStats->upgraded[playerManager->getPlayer()].minimumDamage
      };
      pimpl->damage = std::make_unique<Damage>(sDamage);

      // Damage the object
      auto relativeDamage = objectDamage();

      // so long as the target wasn't killed
      if (relativeDamage >= 0) {
        pimpl->damaged.push_back(pimpl->target);
      }
    }
  }

  auto temp = pimpl->target;
  setTarget(nullptr);
  // The damage has been done, no more damage expected from
  // this projectile. (Ignore periodical damaging.)
  pimpl->expectedDamageCaused = 0;
  setTarget(temp);

  // If the projectile does no splash damage and does not set fire to things
  if (psStats->upgraded[playerManager->getPlayer()].radius == 0 &&
      psStats->upgraded[playerManager->getPlayer()].periodicalDamageTime == 0) {
    pimpl->state = PROJECTILE_STATE::INACTIVE;
    return;
  }

  if (psStats->upgraded[playerManager->getPlayer()].radius != 0) {
    /* An area effect bullet */
    pimpl->state = PROJECTILE_STATE::POST_IMPACT;

    /* Note when it exploded for the explosion effect */
    bornTime = gameTime;

    // If projectile impacts a droid start the splash damage from the center of it, else use whatever location the projectile impacts at as the splash center.
    auto destDroid = dynamic_cast<Droid*>(pimpl->target);
    Vector3i targetPos = (destDroid != nullptr) ? destDroid->getPosition() : getPosition();

    static GridList gridList; // static to avoid allocations.
    gridList = gridStartIterate(targetPos.x, targetPos.y, psStats->upgraded[playerManager->getPlayer()].radius);

    for (auto psCurr : gridList)
    {
      if (psCurr->damageManager->isDead()) {
        continue; // Do not damage dead objects further.
      }

      if (psCurr == pimpl->target) {
        continue; // Don't hit main target twice.
      }

      if (pimpl->source && pimpl->source->playerManager->getPlayer() ==
                                   dynamic_cast<PlayerManager *>(psCurr)->getPlayer() &&
          psStats->flags.test(static_cast<size_t>(WEAPON_FLAGS::NO_FRIENDLY_FIRE))) {
        continue; // this weapon does not do friendly damage
      }

      bool bTargetInAir = false;
      bool useSphere = false;
      bool damageable = true;

      if (auto psDroid = dynamic_cast<Droid*>(psCurr)) {
        auto propulsion = dynamic_cast<PropulsionStats const *>(
                psDroid->getComponent("propulsion"));

        if (!propulsion) continue;

        bTargetInAir = asPropulsionTypes[static_cast<int>(
                propulsion->propulsionType)] .travel == TRAVEL_MEDIUM::AIR &&
                psDroid->getMovementData()->status != MOVE_STATUS::INACTIVE;
        useSphere = true;
      }
      else if (auto psFeature = dynamic_cast<Feature*>(psCurr)) {
        damageable = ((Feature *) psCurr)->getStats()->damageable;
      }
      else {
        continue;
      }

      if (!damageable) {
        continue; // Ignore features that are not damageable.
      }
      auto targetInFlag = bTargetInAir ? SHOOT_IN_AIR : SHOOT_ON_GROUND;
      if ((psStats->surfaceToAir & targetInFlag) == 0) {
        continue; // Target in air, and can't shoot at air, or target on ground, and can't shoot at ground.
      }
      if (useSphere && !Vector3i_InSphere(
              psCurr->getPosition(), targetPos,
              psStats->upgraded[playerManager->getPlayer()].radius)) {
        continue; // Target out of range.
      }
      // The psCurr will get damaged, at this point.
      auto damage = calcDamage(weaponRadDamage(
                                       psStats.get(), playerManager->getPlayer()),
                               psStats->weaponEffect, psCurr);

      debug(LOG_ATTACK, "Damage to object %d, player %d : %u", psCurr->getId(), psCurr->playerManager->getPlayer(), damage);
      if (bMultiPlayer && pimpl->source != nullptr && psCurr->type != OBJ_FEATURE) {
        updateMultiStatsDamage(pimpl->source->playerManager->getPlayer(), psCurr->playerManager->getPlayer(), damage);
      }

      struct Damage sDamage = {
              psCurr,
              damage,
              getTime(),
              false,
              (int)psStats->upgraded[playerManager->getPlayer()].minimumDamage
      };

      pimpl->damage = std::make_unique<Damage>(sDamage);
      objectDamage();
    }
  }

  if (psStats->upgraded[playerManager->getPlayer()].periodicalDamageTime != 0) {
    /* Periodical damage round */
    /* Periodical damage gets done in the bullet update routine */
    /* Just note when started damaging          */
    pimpl->state = PROJECTILE_STATE::POST_IMPACT;
    bornTime = gameTime;
  }
  /* Something was blown up */
}

void Projectile::proj_PostImpactFunc()
{
  if (!pimpl) return;
	auto psStats = pimpl->weaponStats;
	ASSERT_OR_RETURN(, psStats != nullptr, "Invalid weapon stats pointer");

	auto age = gameTime - bornTime;

	/* Time to finish postimpact effect? */
	if (age > psStats->radiusLife &&
      age > psStats->upgraded[playerManager->getPlayer()].periodicalDamageTime) {
		pimpl->state = PROJECTILE_STATE::INACTIVE;
		return;
	}

	/* Periodical damage effect */
	if (psStats->upgraded[playerManager->getPlayer()].periodicalDamageTime > 0) {
		/* See if anything is in the fire and damage it periodically */
		proj_checkPeriodicalDamage();
	}
}

void Projectile::update()
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Projectile object is undefined");
  setPreviousLocation(Spacetime());

	// see if any of the stored objects have died
	// since the projectile was created
	if (pimpl->source && dynamic_cast<DamageManager *>(pimpl->source)->isDead()) {
		setProjectileSource(this, nullptr);
	}
	if (pimpl->target && dynamic_cast<DamageManager *>(pimpl->target)->isDead()) {
		setTarget(nullptr);
	}

	// remove dead objects from psDamaged.
	pimpl->damaged.erase(std::remove_if(pimpl->damaged.begin(), pimpl->damaged.end(),
                                      [](BaseObject const* psObj) {
      return dynamic_cast<DamageManager const*>(psObj)->isDead();
    }), pimpl->damaged.end());

	// This extra check fixes a crash in cam2, mission1
	if (!worldOnMap(getPosition().x, getPosition().y)) {
    damageManager->setTimeOfDeath(1);
		return;
	}

	switch (pimpl->state) {
    using enum PROJECTILE_STATE;
  	case INFLIGHT:
  		proj_InFlightFunc();
  		if (pimpl->state != IMPACT) {
  			break;
  		}
  	case IMPACT:
  		proj_ImpactFunc();
  		if (pimpl->state != POST_IMPACT) {
  			break;
  		}
  	case POST_IMPACT:
  		proj_PostImpactFunc();
  		break;
  	case INACTIVE:
      damageManager->setTimeOfDeath(getTime());
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
  ASSERT_OR_RETURN(, pimpl != nullptr, "Projectile object is undefined");
	// note the attacker if any
	g_pProjLastAttacker = pimpl->source;

	auto psStats = pimpl->weaponStats;

	static GridList gridList; // static to avoid allocations.
	gridList = gridStartIterate(getPosition().x, 
                              getPosition().y,
                              psStats->upgraded[playerManager->getPlayer()].periodicalDamageRadius);
  
	for (auto psCurr : gridList)
	{
    if (psCurr->damageManager->isDead()) {
			continue; // Do not damage dead objects further.
		}

		if (aiCheckAlliances(playerManager->getPlayer(), psCurr->playerManager->getPlayer())) {
			continue; // Don't damage your own droids, nor ally droids - unrealistic, but better.
		}

		if (dynamic_cast<Droid*>(psCurr) &&
        dynamic_cast<Droid*>(psCurr)->isVtol() &&
        dynamic_cast<Droid*>(psCurr)->getMovementData()->status != MOVE_STATUS::INACTIVE) {
			continue; // Can't set flying vtols on fire.
		}

		if (dynamic_cast<Feature*>(psCurr) &&
        !dynamic_cast<Feature*>(psCurr)->getStats()->damageable) {
			continue; // Can't destroy oil wells.
		}

    auto asDamageableObj = dynamic_cast<DamageManager *>(psCurr);
		if (asDamageableObj) {
			asDamageableObj->setPeriodicalDamageStartTime(gameTime);
			asDamageableObj->setPeriodicalDamage(0); // Reset periodical damage done this tick.
		}
		unsigned damageRate = calcDamage(
            weaponPeriodicalDamage(psStats.get(), playerManager->getPlayer()),
            psStats->periodicalDamageWeaponEffect, psCurr);

		debug(LOG_NEVER, "Periodical damage of %d per second to object %d, player %d\n",
          damageRate, psCurr->getId(), psCurr->playerManager->getPlayer());

		auto sDamage = std::make_unique<Damage>();
		pimpl->damage->target = psCurr;
		pimpl->damage->damage = damageRate,
		pimpl->damage->impactTime = gameTime - deltaGameTime / 2 + 1,
		pimpl->damage->isDamagePerSecond = true;
		pimpl->damage->minDamage = psStats->upgraded[playerManager->getPlayer()].minimumDamage;

		objectDamage();
	}
}

// return whether a weapon is direct or indirect
bool proj_Direct(WeaponStats const* psStats)
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
	ASSERT_OR_RETURN(retVal, (player) >= 0 && (player) < MAX_PLAYERS, \
                   "Invalid player: %" PRIu32 "", player);

// return the maximum range for a weapon
unsigned proj_GetLongRange(WeaponStats const* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].maxRange;
}

// return the minimum range for a weapon
unsigned proj_GetMinRange(WeaponStats const* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].minRange;
}

// return the short range for a weapon
unsigned proj_GetShortRange(WeaponStats const* psStats, unsigned player)
{
	ASSERT_PLAYER_OR_RETURN(0, player);
	return psStats->upgraded[player].shortRange;
}

ObjectShape establishTargetShape(BaseObject* psTarget)
{
	if (auto psDroid = dynamic_cast<Droid*>(psTarget)) { // circular
    switch (dynamic_cast<Droid *>(psTarget)->getType()) {
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
        return ObjectShape(abs(psTarget->getDisplayData()->imd_shape->radius) * 2);
      case DEFAULT:
      case TRANSPORTER:
      case SUPER_TRANSPORTER:
      default:
        return ObjectShape(TILE_UNITS / 4);// how will we arrive at this?
    }
  }
	if (auto structure = dynamic_cast<Structure*>(psTarget)) { // Rectangular.
    return ObjectShape(structure->getSize() * TILE_UNITS / 2);
  }
	if (auto feature = dynamic_cast<Feature*>(psTarget)) { // Rectangular.
    return ObjectShape(Vector2i(feature->getStats()->baseWidth,
                                feature->getStats()->baseBreadth) *
                       TILE_UNITS / 2);
  }
	if (dynamic_cast<Projectile*>(psTarget)) { // Circular, but can't happen since a PROJECTILE isn't a SimpleObject.
		//Watermelon 1/2 radius of a droid?
		return ObjectShape(TILE_UNITS / 8);
	}

	return ObjectShape(0); // Huh?
}

/*the damage depends on the weapon effect and the target propulsion type or
structure strength*/
unsigned calcDamage(unsigned baseDamage, WEAPON_EFFECT weaponEffect, BaseObject const* psTarget)
{
	if (baseDamage == 0) return 0;

	auto damage = baseDamage * 100;
	if (auto structure = dynamic_cast<Structure const*>(psTarget)) {
		damage += baseDamage * (asStructStrengthModifier[weaponEffect][structure->getStats()->strength]
			- 100);
	}
	else if (auto droid = dynamic_cast<Droid const*>(psTarget)) {
		const auto propulsion = dynamic_cast<PropulsionStats const*>(
            droid->getComponent("propulsion"))->propulsionType;
		const auto body = dynamic_cast<BodyStats const*>(droid->getComponent("body"))->size;
		damage += baseDamage * (asWeaponModifier[static_cast<int>(weaponEffect)][static_cast<int>(propulsion)] - 100);
		damage += baseDamage * (asWeaponModifierBody[static_cast<int>(weaponEffect)][static_cast<int>(body)] - 100);
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
int Projectile::objectDamageDispatch()
{
  ASSERT_OR_RETURN(-1, pimpl != nullptr, "Projectile object is undefined");
	if (auto droid = dynamic_cast<Droid*>(pimpl->damage->target)) {
    return droid->droidDamage(pimpl->damage->damage, pimpl->weaponStats->weaponClass,
                              pimpl->weaponStats->weaponSubClass, pimpl->damage->impactTime,
                              pimpl->damage->isDamagePerSecond, pimpl->damage->minDamage);
  }
  if (auto structure = dynamic_cast<Structure*>(pimpl->damage->target)) {
    return structureDamage(structure, pimpl->damage->damage, pimpl->weaponStats->weaponClass,
                           pimpl->weaponStats->weaponSubClass, pimpl->damage->impactTime,
                           pimpl->damage->isDamagePerSecond, pimpl->damage->minDamage);
  }
  if (auto feature = dynamic_cast<Feature*>(pimpl->damage->target)) {
    return featureDamage(feature, pimpl->damage->damage, pimpl->weaponStats->weaponClass,
                         pimpl->weaponStats->weaponSubClass, pimpl->damage->impactTime,
                         pimpl->damage->isDamagePerSecond, pimpl->damage->minDamage);
  }
	ASSERT(!"unknown object type", "unknown object, id=%d", pimpl->damage->target->getId());
	return 0;
}

bool Projectile::isFriendlyFire() const
{
	return pimpl && pimpl->target &&
         dynamic_cast<PlayerManager *>(pimpl->source)->getPlayer()
         == dynamic_cast<PlayerManager *>(pimpl->target)->getPlayer();
}

bool Projectile::shouldIncreaseExperience() const
{
	return pimpl && pimpl->source &&
         !dynamic_cast<Feature*>(pimpl->target) &&
         !isFriendlyFire();
}

void Projectile::updateKills()
{
	if (bMultiPlayer) {
		updateMultiStatsKills(pimpl->damage->target,
                          dynamic_cast<PlayerManager *>(pimpl->source)->getPlayer());
	}

	if (auto psDroid = dynamic_cast<Droid*>(pimpl->source)) {
		psDroid->incrementKills();

		if (psDroid->hasCommander()) {
			auto psCommander = psDroid->getGroup()->getCommander();
			psCommander->kills++;
		}
	}
	else if (dynamic_cast<Structure*>(pimpl->source)) {
		auto psCommander = getDesignatorAttackingObject(
            dynamic_cast<PlayerManager *>(pimpl->source)->getPlayer(),
            pimpl->target);

		if (psCommander != nullptr) {
			psCommander->incrementKills();
		}
	}
}

int Projectile::objectDamage()
{
  ASSERT_OR_RETURN(-1, pimpl != nullptr, "Projectile object is undefined");
	auto relativeDamage = objectDamageDispatch();
  if (!shouldIncreaseExperience()) return relativeDamage;

  updateExperience(abs(relativeDamage) * getExpGain
                   (pimpl->source->playerManager->getPlayer()) / 100);

  if (relativeDamage < 0) updateKills();
  return relativeDamage;
}

/// @return true if `psObj` has just been hit by an electronic warfare weapon
static bool justBeenHitByEW(BaseObject const* psObj)
{
	if (gamePaused()) return false;

	if (auto psDroid = dynamic_cast<Droid const*>(psObj)) {
    if ((gameTime - psDroid->timeLastHit) < ELEC_DAMAGE_DURATION &&
        psDroid->damageManager->getLastHitWeapon() == WEAPON_SUBCLASS::ELECTRONIC) {
      return true;
    }
  } 
  if (auto psFeature = dynamic_cast<Feature const*>(psObj)) {
    if ((gameTime - psFeature->timeLastHit) < ELEC_DAMAGE_DURATION) {
      return true;
    }
  }
  if (auto psStructure = dynamic_cast<Structure const*>(psObj)) {
    if ((gameTime - psStructure->timeLastHit) < ELEC_DAMAGE_DURATION &&
        psStructure->damageManager->getLastHitWeapon() == WEAPON_SUBCLASS::ELECTRONIC) {
      return true;
    }
  }
  else {
    ASSERT(false, "Unknown or invalid object for EW: %s", objInfo(psObj));
    return false;
	}
	return false;
}

glm::mat4 objectShimmy(BaseObject const* psObj)
{
	if (justBeenHitByEW(psObj)) {
		const glm::mat4 rotations =
			glm::rotate(UNDEG(SKY_SHIMMY),
                  glm::vec3(1.f, 0.f, 0.f)) *
			glm::rotate(UNDEG(SKY_SHIMMY),
                  glm::vec3(0.f, 1.f, 0.f)) *
			glm::rotate(UNDEG(SKY_SHIMMY),
                  glm::vec3(0.f, 0.f, 1.f));
		if (!dynamic_cast<Droid const*>(psObj))
			return rotations;
		return rotations * glm::translate(
            glm::vec3(1 - rand() % 3, 0, 1 - rand() % 3));
	}
	return glm::mat4(1.f);
}

static constexpr auto BULLET_FLIGHT_HEIGHT = 16;

int establishTargetHeight(BaseObject const* psTarget)
{
	if (!psTarget) return 0;

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
      auto psStructureStats = psStruct->getStats();
      auto height = psStructureStats->IMDs[0]->max.y + psStructureStats.IMDs[0]->min.y;
      height -= gateCurrentOpenHeight(psStruct, gameTime, 2);
      // treat gate as at least 2 units tall, even if open, so that it's possible to hit
      return height;
  }
  else if (dynamic_cast<const Feature*>(psTarget)) {
  // Just use imd ymax+ymin
  return psTarget->getDisplayData()->imd_shape->max.y +
    psTarget->getDisplayData()->imd_shape->min.y;
  }
  else if (dynamic_cast<const Projectile*>(psTarget)) {
  return BULLET_FLIGHT_HEIGHT;
  }
  else {
    return 0;
  }
}

void Projectile::setSource(BaseObject* psObj)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Projectile object is undefined");
	// use the source of the source of psProj if psAttacker is a projectile
	pimpl->source = nullptr;
	if (psObj == nullptr) return;
	else if (auto psPrevProj = dynamic_cast<Projectile*>(psObj)) {
		if (psPrevProj->pimpl->source &&
        !dynamic_cast<DamageManager *>(psPrevProj->pimpl->source)->isDead()) {
			pimpl->source = psPrevProj->pimpl->source;
		}
	}
	else {
		pimpl->source = psObj;
	}
}
