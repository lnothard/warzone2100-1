//
// Created by luna on 08/12/2021.
//

#include "basedef.h"

Spacetime::Spacetime(uint32_t time, Position position, Rotation rotation)
:time {time}, position{position}, rotation{rotation}
{
}

Spacetime Impl::Simple_Object::spacetime() const
{
  return { time, position, rotation };
}

Impl::Simple_Object::Simple_Object(uint32_t id, uint32_t player)
: id {id}, player {player}
{
}

Position Impl::Simple_Object::get_position() const
{
  return position;
}

Rotation Impl::Simple_Object::get_rotation() const
{
  return rotation;
}

uint8_t Impl::Simple_Object::get_player() const
{
  return player;
}

uint32_t Impl::Simple_Object::get_id() const
{
  return id;
}

void Impl::Simple_Object::set_height(int height)
{
  position.z = height;
}

static inline int object_position_square_diff(const Position& first, const Position& second)
{
  const Vector2i diff = (first - second).xy();
  return dot(diff, diff);
}

static inline int object_position_square_diff(const Simple_Object& first, const Simple_Object& second)
{
  return object_position_square_diff(first.get_position(), second.get_position());
}