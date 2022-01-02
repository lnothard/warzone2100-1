//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_DROID_GROUP_H
#define WARZONE2100_DROID_GROUP_H

#include "droid.h"

/// Global list of `DroidGroups`
extern std::vector<DroidGroup> droid_groups;

/// The category of `DroidGroup`
enum class GROUP_TYPE
{
	NORMAL,
	COMMAND,
	TRANSPORTER
};

///
class DroidGroup
{
public:
  DroidGroup() = default;
  explicit DroidGroup(unsigned id);
  DroidGroup(unsigned id, GROUP_TYPE type);
  DroidGroup(unsigned id, GROUP_TYPE type, Droid& commander);

  ///
	void add(Droid& droid);

  ///
	void remove(Droid& droid);

  ///
	[[nodiscard]] bool is_command_group() const noexcept;

  ///
	[[nodiscard]] bool has_electronic_weapon() const;

  ///
	[[nodiscard]] unsigned get_commander_level() const;

  ///
	void commander_gain_experience(unsigned exp);

  ///
  void increment_commander_kills();

  ///
  [[nodiscard]] const Droid& get_commander() const;
private:
	using enum GROUP_TYPE;

  /// Unique ID belonging to this group
	unsigned id = 0;

  /// What kind of group is this?
	GROUP_TYPE type = NORMAL;

  /** Non-owning pointer to this group's commander.
   *  Equal to `nullptr` if this is not a command group
   */
	Droid* commander = nullptr;

  /// List of `Droids` included which are a part of this group
	std::vector<Droid*> members;
};


#endif // WARZONE2100_DROID_GROUP_H
