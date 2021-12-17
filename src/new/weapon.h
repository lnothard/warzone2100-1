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

constexpr uint32_t DEFAULT_RECOIL_TIME { GAME_TICKS_PER_SEC / 4 };

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
    uint32_t min_range;
    uint32_t max_range;
    uint32_t hit_chance;
    uint32_t direct_damage;
    uint32_t blast_radius;
    uint32_t splash_damage;
    uint32_t min_damage_percent;
    uint32_t reload_time;
    uint32_t pause_between_shots;
    uint32_t ticking_damage;
    uint32_t ticking_damage_radius;
    uint32_t ticking_damage_time;
    uint8_t  rounds_per_salvo;
  } base_stats, upgraded_stats[MAX_PLAYERS];

  WEAPON_CLASS    wclass;
  WEAPON_SUBCLASS subclass;
  WEAPON_EFFECT   effect;
  WEAPON_SIZE     size;
  MOVEMENT_TYPE   movement_type;
  uint32_t        flight_speed;
  uint32_t        recoil_value;
  uint16_t        effect_magnitude;
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

class Weapon : public Simple_Object
{
public:
  bool has_ammo() const;
  bool has_full_ammo() const;
  bool is_artillery() const;
  bool is_VTOL_weapon() const;
  bool is_empty_VTOL_weapon(uint32_t player) const;
  uint32_t get_recoil() const;
  uint32_t get_max_range(uint32_t player) const;
  uint32_t get_min_range(uint32_t player) const;
  uint32_t get_num_attack_runs(uint32_t player) const;
  uint32_t get_shots_fired() const;
  const iIMDShape& get_IMD_shape() const;
  const iIMDShape& get_mount_graphic() const;
  WEAPON_SUBCLASS get_subclass() const;
private:
  using enum ATTACKER_TYPE;

  ATTACKER_TYPE attacker_type;
  Weapon_Stats  stats;
  Rotation      rotation;
  Rotation      previous_rotation;
  uint32_t      ammo;
  uint32_t      ammo_used;
  uint32_t      time_last_fired;
  uint32_t      shots_fired;
};

#endif // WARZONE2100_WEAPON_H