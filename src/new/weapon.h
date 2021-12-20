//
// Created by luna on 09/12/2021.
//

#ifndef WARZONE2100_WEAPON_H
#define WARZONE2100_WEAPON_H

#include <cstdint>

#include "lib/framework/vector.h"
#include "lib/gamelib/gtime.h"
#include "basedef.h"
#include "stats.h"

static constexpr auto DEFAULT_RECOIL_TIME = GAME_TICKS_PER_SEC / 4;

enum class WEAPON_SIZE
{
  LIGHT,
  HEAVY
};

enum class WEAPON_CLASS
{
  KINETIC,
  HEAT
};

enum class WEAPON_SUBCLASS
{
  MACHINE_GUN,
  CANNON,
  MORTARS,
  MISSILE,
  ROCKET,
  ENERGY,
  GAUSS,
  FLAME,
  HOWITZER,
  ELECTRONIC,
  SLOW_MISSILE,
  SLOW_ROCKET,
  BOMB,
  EMP
};

enum class WEAPON_EFFECT
{
  ANTI_PERSONNEL,
  ANTI_TANK,
  BUNKER_BUSTER,
  FLAMER,
  ANTI_AIRCRAFT
};

enum class MOVEMENT_TYPE
{
  DIRECT,
  INDIRECT,
  HOMING_DIRECT,
  HOMING_INDIRECT
};

struct Weapon_Stats : public Component_Stats
{
  using enum WEAPON_CLASS;
  using enum WEAPON_SUBCLASS;
  using enum WEAPON_EFFECT;
  using enum WEAPON_SIZE;
  using enum MOVEMENT_TYPE;

  struct : Upgradeable
  {
    unsigned min_range;
    unsigned max_range;
    unsigned hit_chance;
    unsigned direct_damage;
    unsigned blast_radius;
    unsigned splash_damage;
    unsigned min_damage_percent;
    std::size_t reload_time;
    std::size_t pause_between_shots;
    unsigned ticking_damage;
    unsigned ticking_damage_radius;
    std::size_t ticking_damage_time;
    uint8_t  rounds_per_salvo;
  } base_stats, upgraded_stats[MAX_PLAYERS];

  WEAPON_CLASS    wclass;
  WEAPON_SUBCLASS subclass;
  WEAPON_EFFECT   effect;
  WEAPON_SIZE     size;
  MOVEMENT_TYPE   movement_type;
  unsigned        flight_speed;
  unsigned        recoil_value;
  unsigned        effect_magnitude;
  short           max_rotation;
  short           min_elevation;
  short           max_elevation;
  short           max_VTOL_attack_runs;
  bool            can_penetrate;
  bool            can_fire_while_moving;
  bool            effect_emits_light;
  std::unique_ptr<iIMDShape> weapon_graphic;
  std::unique_ptr<iIMDShape> mount_graphic;
  std::unique_ptr<iIMDShape> muzzle_graphic;
  std::unique_ptr<iIMDShape> in_flight_graphic;
  std::unique_ptr<iIMDShape> hit_graphic;
  std::unique_ptr<iIMDShape> miss_graphic;
  std::unique_ptr<iIMDShape> splash_graphic;
  std::unique_ptr<iIMDShape> trail_graphic;
};

enum class ATTACKER_TYPE
{
  UNKNOWN,
  PLAYER,
  VISUAL,
  ALLY,
  COMMANDER,
  SENSOR,
  CB_SENSOR,
  AIR_DEF_SENSOR,
  RADAR_DETECTOR
};

class Weapon : public virtual ::Simple_Object, public Impl::Simple_Object
{
public:
  bool has_ammo() const;
  bool has_full_ammo() const;
  bool is_artillery() const;
  bool is_VTOL_weapon() const;
  bool is_empty_VTOL_weapon(const unsigned player) const;
  const Weapon_Stats& get_stats() const;
  unsigned get_recoil() const;
  unsigned get_max_range(const unsigned player) const;
  unsigned get_min_range(const unsigned player) const;
  unsigned get_num_attack_runs(const unsigned player) const;
  unsigned get_shots_fired() const;
  const iIMDShape& get_IMD_shape() const;
  const iIMDShape& get_mount_graphic() const;
  WEAPON_SUBCLASS get_subclass() const;
private:
  using enum ATTACKER_TYPE;

  ATTACKER_TYPE attacker_type;
  Weapon_Stats  stats;
  Rotation      rotation;
  Rotation      previous_rotation;
  unsigned      ammo;
  unsigned      ammo_used;
  std::size_t      time_last_fired;
  unsigned      shots_fired;
};

#endif // WARZONE2100_WEAPON_H