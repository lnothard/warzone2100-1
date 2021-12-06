//
// Created by luna on 01/12/2021.
//

#ifndef WARZONE2100_UNITDEF_H
#define WARZONE2100_UNITDEF_H

#include "basedef.h"
#include <array>
#include <lib/framework/types.h>

/**
 * Represents either a structure or a droid. Created to reduce redundancy
 * in GameObject blob.
 */
class Unit : public GameObject
{
protected:
  unsigned numWeapons;
  std::array<Weapon, MAX_WEAPONS> weaponList;
  UBYTE selected;
  UBYTE animationEvent;              ///< If animation start time > 0, this points to which animation to run
  UDWORD timeAnimationStarted;       ///< Animation start time, zero for do not animate
public:
  //auto weaponList();
  void alignTurret(int weaponSlot);

  int objPosDiffSq(Position otherPos) override;
  int objPosDiffSq(const GameObject& otherObj) override;

  virtual bool turretOnTarget(GameObject *targetObj, Weapon *weapon);

  virtual int sensorRange() = 0;

  virtual bool aiUnitHasRange(const GameObject& targetObj, int weapon_slot) = 0;

  void actionCalcPullBackPoint(GameObject const* targetObj, int *px, int *py) const;
};

#endif // WARZONE2100_UNITDEF_H