//
// Created by luna on 08/12/2021.
//

#include "basedef.h"

Spacetime::Spacetime(uint32_t time, Position position, Rotation rotation)
{
  this->time = time;
  this->position = position;
  this->rotation = rotation;
}

Spacetime Impl::Simple_Object::spacetime()
{
  return { time, position, rotation };
}

Impl::Simple_Object::Simple_Object(uint32_t id, uint8_t player)
{
  this->id = id;
  this->player = player;
}