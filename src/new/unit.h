//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_UNIT_H
#define WARZONE2100_UNIT_H

#include "basedef.h"

class Unit : public virtual ::Simple_Object
{
public:
  virtual bool is_alive() const = 0;
};

namespace Impl
{
  class Unit : public virtual ::Unit, public Impl::Simple_Object
  {
  public:
    bool is_alive() const override;
    bool hp_below_x(uint32_t x) const;
  private:
    uint32_t hit_points;
  };
}
#endif // WARZONE2100_UNIT_H