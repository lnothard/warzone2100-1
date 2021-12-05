//
// Created by luna on 01/12/2021.
//

#include "unitdef.h"
#include "lib/framework/fixedpoint.h"
#include "lib/framework/math_ext.h"
#include "lib/gamelib/gtime.h"

#define ACTION_TURRET_ROTATION_RATE 45

auto Unit::weaponList()
{
  return &m_weaponList;
}

// Realign turret
void Unit::alignTurret(int weaponSlot)
{
  uint16_t        nearest = 0;
  uint16_t        tRot;
  uint16_t        tPitch;

  //get the maximum rotation this frame
  const int rotation = gameTimeAdjustedIncrement(DEG(ACTION_TURRET_ROTATION_RATE));


  tRot = m_weaponList[weaponSlot].rot.direction;
  tPitch = m_weaponList[weaponSlot].rot.pitch;

  if (m_type == OBJECT_TYPE::STRUCTURE)
  {
    // find the nearest 90 degree angle
    nearest = (uint16_t)((tRot + DEG(45)) / DEG(90) * DEG(90)); // Cast wrapping intended.
  }

  tRot += clip(angleDelta(nearest - tRot), -rotation, rotation);  // Addition wrapping intended.

  // align the turret pitch
  tPitch += clip(angleDelta(0 - tPitch), -rotation / 2, rotation / 2); // Addition wrapping intended.

  m_weaponList[weaponSlot].rot.direction = tRot;
  m_weaponList[weaponSlot].rot.pitch = tPitch;
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

// If we have ECM, use this for range instead. Otherwise, the sensor's range will be used for
// jamming range, which we do not want. Rather limit ECM unit sensor range to jammer range.
int Unit::objSensorRange()
{
  if (m_type == OBJECT_TYPE::DROID)
  {
    const int ecmrange = asECMStats[((const Droid *)psObj)->asBits[COMP_ECM]].upgrade[owningPlayer].range;
    if (ecmrange > 0)
    {
      return ecmrange;
    }
    return asSensorStats[((const Droid *)psObj)->asBits[COMP_SENSOR]].upgrade[owningPlayer].range;
  }
  else if (m_type == OBJECT_TYPE::STRUCTURE)
  {
    const int ecmrange = ((const Structure *)psObj)->stats->pECM->upgrade[owningPlayer].range;
    if (ecmrange)
    {
      return ecmrange;
    }
    return ((const Structure *)psObj)->stats->pSensor->upgrade[owningPlayer].range;
  }
  return 0;
}