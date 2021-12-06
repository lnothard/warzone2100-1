//
// Created by luna on 01/12/2021.
//

#include "basedef.h"

Spacetime::Spacetime(Position position, Rotation rotation, uint32_t time)
    : m_position{position}, m_rotation{rotation}, m_time{time}
{
}

Position GameObject::getPosition() const
{
  return position;
}

OBJECT_TYPE GameObject::getType() const
{
  return type;
}

bool GameObject::alive() const
{
  // See objmem.c for comments on the NOT_CURRENT_LIST hack
  return deathTime <= NOT_CURRENT_LIST;
}

Spacetime GameObject::spacetime()
{
  return {position, m_rotation, m_time };
}

void GameObject::checkObject(const char *const location_description, const char *function, const int recurse) const
{
  if (recurse < 0)
  {
    return;
  }

  switch (type)
  {
  case OBJ_DROID:
    checkDroid((const Droid *)psObject, location_description, function, recurse - 1);
    break;

  case OBJ_STRUCTURE:
    checkStructure((const Structure *)psObject, location_description, function, recurse - 1);
    break;

  case OBJ_PROJECTILE:
    checkProjectile((const Projectile *)psObject, location_description, function, recurse - 1);
    break;

  case OBJ_FEATURE:
    break;

  default:
    ASSERT_HELPER(!"invalid object type", location_description, function, "CHECK_OBJECT: Invalid object type (type num %u)", (unsigned int)psObject->type);
    break;
  }
}