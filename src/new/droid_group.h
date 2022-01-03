
/**
 * @file droid_group.h
 *
 */

#ifndef WARZONE2100_DROID_GROUP_H
#define WARZONE2100_DROID_GROUP_H

#include "droid.h"

/// The global list of active droid groups
extern std::vector<DroidGroup> droid_groups;

/// The category of group
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
  /* Constructor overloads */
  DroidGroup() = default;
  explicit DroidGroup(unsigned id);
  DroidGroup(unsigned id, GROUP_TYPE type);
  DroidGroup(unsigned id, GROUP_TYPE type, Droid& commander);

  /// Add a new member
	void add(Droid& droid);

  ///
	void remove(Droid& droid);

  /// @return `true` if this is a command group
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

  /// Unique ID for this group
	unsigned id = 0;

  /// What kind of group is this?
	GROUP_TYPE type = NORMAL;

  /**
   * Non-owning pointer to this group's commander.
   * Set to `nullptr` if this is not a command group
   */
	Droid* commander = nullptr;

  /// List of droids which are a part of this group
	std::vector<Droid*> members;
};


#endif // WARZONE2100_DROID_GROUP_H
