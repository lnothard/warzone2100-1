//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_DROID_H
#define WARZONE2100_DROID_H

#include <memory>
#include <vector>
#include <optional>

#include "unit.h"
#include "droid_group.h"
#include "structure.h"
#include "movement.h"
#include "order.h"

constexpr auto MAX_COMPONENTS { COMPONENT_TYPE::COUNT - 1 };

enum class ACTION
{
  NONE,					///< 0 not doing anything
  MOVE,					///< 1 moving to a location
  BUILD,					///< 2 building a structure
  DEMOLISH,				///< 4 demolishing a structure
  REPAIR,					///< 5 repairing a structure
  ATTACK,					///< 6 attacking something
  OBSERVE,				///< 7 observing something
  FIRE_SUPPORT,				///< 8 attacking something visible by a sensor droid
  SULK,					///< 9 refuse to do anything aggressive for a fixed time
  TRANSPORT_OUT,				///< 11 move transporter offworld
  TRANSPORT_WAIT_TO_FLY_IN,			///< 12 wait for timer to move reinforcements in
  TRANSPORT_IN,				///< 13 move transporter onworld
  DROID_REPAIR,				///< 14 repairing a droid
  RESTORE,				///< 15 restore resistance points of a structure

  // The states below are used by the action system
  // but should not be given as an action
  MOVE_FIRE,				///< 17
  MOVE_TO_BUILD,				///< 18 moving to a new building location
  MOVE_TO_DEMOLISH,				///< 19 moving to a new demolition location
  MOVE_TO_REPAIR,				///< 20 moving to a new repair location
  BUILD_WANDER,				///< 21 moving around while building
  MOVE_TO_ATTACK,				///< 23 moving to a target to attack
  ROTATE_TO_ATTACK,				///< 24 rotating to a target to attack
  MOVE_TO_OBSERVE,				///< 25 moving to be able to see a target
  WAIT_FOR_REPAIR,				///< 26 waiting to be repaired by a facility
  MOVE_TO_REPAIR_POINT,			///< 27 move to repair facility repair point
  WAIT_DURING_REPAIR,			///< 28 waiting to be repaired by a facility
  MOVE_TO_DROID_REPAIR,			///< 29 moving to a new location next to droid to be repaired
  MOVE_TO_RESTORE,				///< 30 moving to a low resistance structure
  MOVE_TO_REARM,				///< 32 moving to a rearming pad - VTOLS
  WAIT_FOR_REARM,				///< 33 waiting for rearm - VTOLS
  MOVE_TO_REARM_POINT,			///< 34 move to rearm point - VTOLS - this actually moves them onto the pad
  WAIT_DURING_REARM,			///< 35 waiting during rearm process- VTOLS
  VTOL_ATTACK,				///< 36 a VTOL droid doing attack runs
  CLEAR_REARM_PAD,				///< 37 a VTOL droid being told to get off a rearm pad
  RETURN_TO_POS,				///< 38 used by scout/patrol order when returning to route
  FIRE_SUPPORT_RETREAT,			///< 39 used by firesupport order when sensor retreats
  CIRCLE,
};

enum class DROID_TYPE
{
  WEAPON,           ///< Weapon droid
  SENSOR,           ///< Sensor droid
  ECM,              ///< ECM droid
  CONSTRUCT,        ///< Constructor droid
  PERSON,           ///< person
  CYBORG,           ///< cyborg-type thang
  TRANSPORTER,      ///< guess what this is!
  COMMAND,          ///< Command droid
  REPAIRER,         ///< Repair droid
  DEFAULT,          ///< Default droid
  CYBORG_CONSTRUCT, ///< cyborg constructor droid - new for update 28/5/99
  CYBORG_REPAIR,    ///< cyborg repair droid - new for update 28/5/99
  CYBORG_SUPER,     ///< cyborg repair droid - new for update 7/6/99
  SUPER_TRANSPORTER, ///< SuperTransport (MP)
  ANY,              ///< Any droid. Used as a parameter for various stuff.
};

class Droid : public virtual ::Unit, public Impl::Unit
{
public:
  Droid(uint32_t id, uint32_t player);

  uint8_t get_player() const final;
  bool has_electronic_weapon() const final;

  ACTION get_current_action() const;
  const Order& get_current_order() const;
  bool is_probably_doomed(bool is_direct_damage) const;
  bool is_commander() const;
  bool is_VTOL() const;
  bool is_flying() const;
  bool is_transporter() const;
  bool is_builder() const;
  bool is_cyborg() const;
  bool is_repairer() const;
  bool is_IDF() const;
  bool is_being_repaired() const;
  bool is_stationary() const;
  bool is_rearming() const;
  bool is_damaged() const;
  bool is_attacking() const;
  bool is_VTOL_rearmed_and_repaired() const;
  bool is_VTOL_empty() const;
  bool is_VTOL_full() const;
  bool are_all_VTOLs_rearmed() const;
  bool has_commander() const;
  bool has_standard_sensor() const;
  bool has_CB_sensor() const;
  bool target_within_range(const Unit& target, uint8_t weapon_slot) const;
  void gain_experience(uint32_t exp);
  void commander_gain_experience(uint32_t exp);
  void move_to_rearming_pad();
  void cancel_build();
  void reset_action();
  void update_expected_damage(int32_t damage, bool is_direct);
  uint32_t get_level() const;
  uint32_t get_commander_level() const;
  uint32_t get_effective_level() const;
  int calculate_sensor_range() const final;
  int calculate_max_range() const;
private:
  using enum ACTION;
  using enum DROID_TYPE;

  std::string name;
  ACTION action { NONE };
  DROID_TYPE type { ANY };
  Order order;
  Droid_Group* group { nullptr };
  Structure* associated_structure { nullptr };
  Movement movement;
  std::optional<Propulsion_Stats> propulsion;
  std::optional<Commander_Stats> brain;
  std::optional<Sensor_Stats> sensor;
  std::optional<ECM_Stats> ecm;
  uint32_t weight { 0 };
  uint32_t base_speed { 0 };
  uint32_t original_hp { 0 };
  uint32_t expected_damage_direct { 0 };
  uint32_t expected_damage_indirect { 0 };
  uint32_t kills { 0 };
  uint32_t experience { 0 };
  uint32_t time_action_started { 0 };
  uint32_t action_points_done { 0 };
  int16_t resistance_to_electric { 0 };
};

struct Droid_Template
{
  using enum DROID_TYPE;

  DROID_TYPE type;
  uint8_t    weapon_count;
  bool       is_prefab;
  bool       is_stored;
  bool       is_enabled;
};

static inline bool VTOL_may_land_here(int32_t x, int32_t y);

template <typename T>
static uint32_t calculate_required_build_points(const T& object);

template <typename T>
static uint32_t calculate_required_power(const T& object);

#endif // WARZONE2100_DROID_H