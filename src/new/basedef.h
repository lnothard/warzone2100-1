//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_BASEDEF_H
#define WARZONE2100_BASEDEF_H

#include <cstdint>

#include "lib/framework/vector.h"

struct Spacetime
{
  Spacetime(uint32_t time, Position position, Rotation rotation);

  uint32_t time;
  Position position;
  Rotation rotation;
};

class Simple_Object
{
public:
  virtual ~Simple_Object();

  virtual Spacetime spacetime() const = 0;
  virtual Position get_position() const = 0;
};

namespace Impl
{
  class Simple_Object : public virtual ::Simple_Object
  {
  public:
    Simple_Object(uint32_t id, uint8_t player);

    Spacetime spacetime() const override;
    Position get_position() const override;
  private:
    uint8_t  player;
    uint32_t id;
    uint32_t time { 0 };
    Position position { Position(0, 0, 0) };
    Rotation rotation { Rotation(0, 0, 0) };
  };
}

static inline int object_position_square_diff(const Simple_Object& first, const Simple_Object& second);
static inline int object_position_square_diff(const Position& first, const Position& second);

#endif // WARZONE2100_BASEDEF_H