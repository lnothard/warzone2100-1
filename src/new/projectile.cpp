//
// Created by Luna Nothard on 23/12/2021.
//

#include "droid.h"
#include "feature.h"
#include "projectile.h"

bool is_friendly_fire(const Damage& damage)
{
  const auto& target = damage.projectile->target;
  const auto& source = damage.projectile->source;

  return source->get_player() == target->get_player();
}

bool should_increase_experience(const Damage& damage)
{
  return !is_friendly_fire(damage) &&
         !dynamic_cast<const Feature*>(damage.projectile->target);
}

void update_kills(const Damage& damage)
{
  if (auto droid = dynamic_cast<Droid*>(damage.projectile->source))
  {
    droid->increment_kills();
    if (droid->has_commander())
    {
      droid->increment_commander_kills();
    }
  }
  else if (auto structure = dynamic_cast<Structure*>(damage.projectile->source))
  {

  }
}