//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_UNIT_H
#define WARZONE2100_UNIT_H

#include <vector>

#include "basedef.h"
#include "weapon.h"

class Unit : public virtual ::Simple_Object
{
public:
  virtual bool is_alive() const = 0;
  virtual void align_turret() = 0;
};

namespace Impl
{
  class Unit : public virtual ::Unit, public Impl::Simple_Object
  {
  public:
    bool is_alive() const override;
    bool has_full_ammo() const;
    uint32_t get_hp() const;
  private:
    uint32_t            hit_points;
    std::vector<Weapon> weapons;
  };
}
#endif // WARZONE2100_UNIT_H