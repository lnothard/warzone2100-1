//
// Created by luna on 01/12/2021.
//

#include "unitdef.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"

#define ACTION_TURRET_ROTATION_RATE 45

//auto Unit::weaponList()
//{
//  return &weaponList;
//}

// Realign turret
void Unit::alignTurret(int weaponSlot)
{
  uint16_t        nearest = 0;
  uint16_t        tRot;
  uint16_t        tPitch;

  //get the maximum rotation this frame
  const int rotation = gameTimeAdjustedIncrement(DEG(ACTION_TURRET_ROTATION_RATE));


  tRot = weaponList[weaponSlot].rot.direction;
  tPitch = weaponList[weaponSlot].rot.pitch;

  if (type == OBJECT_TYPE::STRUCTURE)
  {
    // find the nearest 90 degree angle
    nearest = (uint16_t)((tRot + DEG(45)) / DEG(90) * DEG(90)); // Cast wrapping intended.
  }

  tRot += clip(angleDelta(nearest - tRot), -rotation, rotation);  // Addition wrapping intended.

  // align the turret pitch
  tPitch += clip(angleDelta(0 - tPitch), -rotation / 2, rotation / 2); // Addition wrapping intended.

  weaponList[weaponSlot].rot.direction = tRot;
  weaponList[weaponSlot].rot.pitch = tPitch;
}

int Unit::objPosDiffSq(Position otherPos)
{
  const Vector2i diff = (position - otherPos).xy();
  return dot(diff, diff);
}

int Unit::objPosDiffSq(const GameObject& otherObj)
{
  return objPosDiffSq(otherObj.getPosition());
}

/* returns true if on target */
bool Unit::turretOnTarget(GameObject *targetObj, Weapon *weapon)
{
  WEAPON_STATS *psWeapStats = asWeaponStats + weapon->nStat;
  uint16_t tRotation, tPitch;
  uint16_t targetRotation;
  int32_t  rotationTolerance = 0;
  int32_t  pitchLowerLimit, pitchUpperLimit;

  if (!targetObj) return false;

  // these are constants now and can be set up at the start of the function
  int rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 4;
  int pitchRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2;

  // extra heavy weapons on some structures need to rotate and pitch more slowly
  if (psWeapStats->weight > HEAVY_WEAPON_WEIGHT && !bRepair)
  {
    UDWORD excess = DEG(100) * (psWeapStats->weight - HEAVY_WEAPON_WEIGHT) / psWeapStats->weight;

    rotRate = DEG(ACTION_TURRET_ROTATION_RATE) * 2 - excess;
    pitchRate = rotRate / 2;
  }

  tRotation = weapon->rot.direction;
  tPitch = weapon->rot.pitch;

  //set the pitch limits based on the weapon stats of the attacker
  pitchLowerLimit = pitchUpperLimit = 0;
  Vector3i attackerMuzzlePos = position;  // Using for calculating the pitch, but not the direction, in case using the exact direction causes bugs somewhere.
  if (type == OBJ_STRUCTURE)
  {
    auto *psStructure = (Structure *)psAttacker;
    int weapon_slot =
        weapon - psStructure->weaponList;  // Should probably be passed weapon_slot instead of psWeapon.
    calcStructureMuzzleLocation(psStructure, &attackerMuzzlePos, weapon_slot);
    pitchLowerLimit = DEG(psWeapStats->minElevation);
    pitchUpperLimit = DEG(psWeapStats->maxElevation);
  }

  //get the maximum rotation this frame
  rotRate = gameTimeAdjustedIncrement(rotRate);
  rotRate = MAX(rotRate, DEG(1));
  pitchRate = gameTimeAdjustedIncrement(pitchRate);
  pitchRate = MAX(pitchRate, DEG(1));

  //and point the turret at target
  targetRotation =
      calcDirection(getPosition.x, getPosition.y, targetObj->getPosition.x,
                    targetObj->getPosition.y);

  //restrict rotationerror to =/- 180 degrees
  int rotationError = angleDelta(targetRotation - (tRotation + rotation.direction));

  tRotation += clip(rotationError, -rotRate, rotRate);  // Addition wrapping intentional.
  if (getType == OBJ_DROID && isVtolDroid((Droid *)psAttacker))
  {
    // limit the rotation for vtols
    int32_t limit = VTOL_TURRET_LIMIT;
    if (psWeapStats->weaponSubClass == WSC_BOMB || psWeapStats->weaponSubClass == WSC_EMP)
    {
      limit = 0;  // Don't turn bombs.
      rotationTolerance = VTOL_TURRET_LIMIT_BOMB;
    }
    tRotation = (uint16_t)clip(angleDelta(tRotation), -limit, limit);  // Cast wrapping intentional.
  }
  bool onTarget = abs(angleDelta(targetRotation - (tRotation + rotation.direction))) <= rotationTolerance;

  /* Set muzzle pitch if not repairing or outside minimum range */
  const int minRange = proj_GetMinRange(psWeapStats, owningPlayer);
  if (!bRepair && (unsigned)objPosDiffSq(psAttacker, targetObj) > minRange * minRange)
  {
    /* get target distance */
    Vector3i delta = targetObj->getPosition - attackerMuzzlePos;
    int32_t dxy = iHypot(delta.x, delta.y);

    uint16_t targetPitch = iAtan2(delta.z, dxy);
    targetPitch = (uint16_t)clip(angleDelta(targetPitch), pitchLowerLimit, pitchUpperLimit);  // Cast wrapping intended.
    int pitchError = angleDelta(targetPitch - tPitch);

    tPitch += clip(pitchError, -pitchRate, pitchRate);  // Addition wrapping intended.
    onTarget = onTarget && targetPitch == tPitch;
  }

  weapon->rot.direction = tRotation;
  weapon->rot.pitch = tPitch;

  return onTarget;
}

// calculate a position for units to pull back to if they
// need to increase the range between them and a target
void Unit::actionCalcPullBackPoint(GameObject const* targetObj, int *px, int *py) const
{
  // get the vector from the target to the object
  int xdiff = position.x - targetObj->getPosition().x;
  int ydiff = position.y - targetObj->getPosition().y;
  const int len = iHypot(xdiff, ydiff);

  if (len == 0)
  {
    xdiff = TILE_UNITS;
    ydiff = TILE_UNITS;
  }
  else
  {
    xdiff = (xdiff * TILE_UNITS) / len;
    ydiff = (ydiff * TILE_UNITS) / len;
  }

  // create the position
  *px = position.x + xdiff * PULL_BACK_DIST;
  *py = position.y + ydiff * PULL_BACK_DIST;

  // make sure coordinates stay inside of the map
  clip_world_offmap(px, py);
}