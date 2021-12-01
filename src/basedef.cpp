//
// Created by luna on 01/12/2021.
//

#include "basedef.h"

Spacetime::Spacetime(Position position, Rotation rotation, uint32_t time)
    : m_position{position}, m_rotation{rotation}, m_time{time}
{
}

Position Spacetime::position() const
{
  return m_position;
}

static inline int objPosDiffSq(Position pos1, Position pos2)
{
  const Vector2i diff = (pos1 - pos2).xy();
  return dot(diff, diff);
}

static inline int objPosDiffSq(GameObject const &pos1, GameObject const &pos2)
{
  return objPosDiffSq(pos1.spacetime()->position(), pos2.spacetime()->position());
}

std::shared_ptr<Spacetime> GameObject::spacetime() const
{
  return m_spacetime;
}

void GameObject::spacetime(std::shared_ptr<Spacetime> st)
{
  m_spacetime = std::move(st);
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