//
// Created by luna on 01/12/2021.
//

#include "basedef.h"

Spacetime::Spacetime(Position position, Rotation rotation, uint32_t time)
    : m_position{position}, m_rotation{rotation}, m_time{time}
{
}

Position GameObject::position() const
{
  return m_position;
}

OBJECT_TYPE GameObject::type() const
{
  return m_type;
}

bool GameObject::alive() const
{
  // See objmem.c for comments on the NOT_CURRENT_LIST hack
  return deathTime <= NOT_CURRENT_LIST;
}

Spacetime GameObject::spacetime()
{
  return { m_position, m_rotation, m_time };
}