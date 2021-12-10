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
  virtual ~Unit();

  virtual bool is_alive() const = 0;
  virtual bool has_electronic_weapon() const = 0;
  virtual uint32_t get_hp() const = 0;
};

namespace Impl
{
  class Unit : public virtual ::Unit, public Impl::Simple_Object
  {
  public:
    Unit(uint32_t id, uint32_t player);

    bool is_alive() const override;
    bool has_electronic_weapon() const override;
    bool has_full_ammo() const;
    bool has_artillery() const;
    uint32_t get_hp() const override;
    uint16_t num_weapons() const;
  private:
    uint32_t hit_points { 0 };
    std::vector<Weapon> weapons { 0 };
  };
}
#endif // WARZONE2100_UNIT_H