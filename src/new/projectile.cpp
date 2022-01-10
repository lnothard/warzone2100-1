//
// Created by Luna Nothard on 23/12/2021.
//

#include "droid.h"
#include "feature.h"
#include "projectile.h"

bool Interval::is_empty() const noexcept
{
  return begin >= end;
}

bool is_friendly_fire(const Damage& damage)
{
  const auto& target = damage.projectile->target;
  const auto& source = damage.projectile->source;

  return source->getPlayer() == target->getPlayer();
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
    if (droid->hasCommander())
    {
      droid->increment_commander_kills();
    }
  }
  else if (auto structure = dynamic_cast<Structure*>(damage.projectile->source))
  {

  }
}

void set_projectile_target(Projectile& projectile, Unit& unit)
{
  const auto is_direct = !projectile.firing_weapon->is_artillery();
  projectile.target = &unit;
  projectile.target->updateExpectedDamage(projectile.base_damage, is_direct);
}

Interval resolve_xy_collision(Vector2i pos1, Vector2i pos2, int radius)
{
  /// Solve(1 - t)v1 + t v2 = r.
  const auto x_diff = pos2.x - pos1.x;
  const auto y_diff = pos2.y - pos1.y;
  // a = (v2 - v1)²
  const auto a = x_diff * x_diff + y_diff * y_diff;
  // b = v1(v2 - v1)
  const auto b = pos1.x * x_diff + pos1.y * y_diff;
  // c = v1² - r²
  const auto c = pos1.x * pos1.x + pos1.y * pos1.y - radius * radius;
  // Equation to solve is now a t^2 + 2 b t + c = 0.
  const auto d = b * b - a * c; // d = b² - a c

  const Interval empty = {-1, -1};
  const Interval full = {0, 1024};
  if (d < 0)
  {
    // Missed
    return empty;
  }
  if (a == 0)
  {
    // Not moving. See if inside the target
    return c < 0 ? full : empty;
  }

  const auto sd = i64Sqrt(d);
  return {MAX(0, 1024 * (-b - sd) / a),
          MIN(1024, 1024 * (-b + sd) / a)};
}

int calculate_height(const Projectile& projectile)
{
  return BULLET_FLIGHT_HEIGHT;
}
