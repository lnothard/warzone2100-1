//
// Created by luna on 01/12/2021.
//

#include "projectiledef.h"

Projectile::Projectile(uint32_t id, unsigned player)
    : GameObject(OBJ_PROJECTILE, id, player)
{
}

bool Projectile::deleteIfDead()
{
  if (deathTime == 0 || deathTime >= gameTime - deltaGameTime)
  {
    return false;
  }
  delete this;

  return true;
}

/// True iff object is a projectile.
static inline bool isProjectile(GameObject const *obj)
{
  return obj != nullptr && obj->type() == OBJ_PROJECTILE;
}

/// Returns PROJECTILE * if projectile or NULL if not.
static inline Projectile *castProjectile(GameObject *psObject)
{
  return isProjectile(psObject) ? (Projectile *)psObject : (Projectile *)nullptr;
}

/// Returns PROJECTILE const * if projectile or NULL if not.
static inline Projectile const *castProjectile(GameObject const *psObject)
{
  return isProjectile(psObject) ? (Projectile const *)psObject : (Projectile const *)nullptr;
}