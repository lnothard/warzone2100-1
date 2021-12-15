//
// Created by luna on 08/12/2021.
//

#include <ranges>
using namespace std;

#include "unit.h"

namespace Impl
{
  Unit::Unit(uint32_t id, uint32_t player)
  : Impl::Simple_Object(id, player)
  {
  }

  uint32_t Unit::get_hp() const
  {
    return hit_points;
  }

  bool Unit::has_full_ammo() const
  {
    return ranges::all_of(weapons,
                       [](const auto& w){ return w.has_full_ammo(); });
  }

  bool Unit::has_artillery() const
  {
    return ranges::any_of(weapons, [] (const auto& weapon) {
      return weapon.is_artillery();
    });
  }

  bool Unit::has_electronic_weapon() const
  {
    if (weapons.size() == 0) return false;

    return (ranges::any_of(weapons, [] (const auto& weapon) {
      return weapon.get_subclass() == WEAPON_SUBCLASS::ELECTRONIC;
    }));
  }

  uint16_t Unit::num_weapons() const
  {
    return static_cast<uint16_t>(weapons.size());
  }

  const std::vector<Weapon>& Unit::get_weapons() const
  {
    return weapons;
  }
}