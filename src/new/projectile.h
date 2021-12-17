//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_PROJECTILE_H
#define WARZONE2100_PROJECTILE_H

#include "basedef.h"
#include "unit.h"

constexpr auto PROJECTILE_MAX_PITCH { 45 };
constexpr auto BULLET_FLIGHT_HEIGHT { 16 };
constexpr auto VTOL_HITBOX_MODIFIER { 100 };

enum class PROJECTILE_STATE
{
  IN_FLIGHT,
  IMPACT,
  POST_IMPACT,
  INACTIVE
};

class Projectile : public virtual Simple_Object, public Impl::Simple_Object
{
public:
  int calculate_height() const final;
private:
  using enum PROJECTILE_STATE;

  PROJECTILE_STATE state;
  Unit*            source;
  Unit*            target;
  Vector3i         destination;
  Vector3i         origin;
  uint32_t         base_damage;
};

#endif // WARZONE2100_PROJECTILE_H