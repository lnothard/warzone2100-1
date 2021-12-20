//
// Created by luna on 08/12/2021.
//

#include "basedef.h"

Spacetime::Spacetime(std::size_t time, Position position, Rotation rotation)
        :time {time}, position{position}, rotation{rotation}
{
}

Spacetime Impl::Simple_Object::spacetime() const noexcept
{
  return { time, position, rotation };
}

Impl::Simple_Object::Simple_Object(unsigned id, unsigned player)
: id {id}, player {player}
{
}

const Position& Impl::Simple_Object::get_position() const noexcept
{
  return position;
}

const Rotation& Impl::Simple_Object::get_rotation() const noexcept
{
  return rotation;
}

unsigned Impl::Simple_Object::get_player() const noexcept
{
  return player;
}

unsigned Impl::Simple_Object::get_id() const noexcept
{
  return id;
}

void Impl::Simple_Object::set_height(int height)
{
  position.z = height;
}

void Impl::Simple_Object::set_rotation(Rotation &new_rotation)
{
  rotation = new_rotation;
}