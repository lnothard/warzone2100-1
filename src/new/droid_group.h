
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

/**
 * A logical grouping of droids, possibly associated
 * with a particular transporter or commander
 */
class DroidGroup
{
public:
  /* Constructor overloads */
  DroidGroup() = default;
  explicit DroidGroup(unsigned id);
  DroidGroup(unsigned id, GROUP_TYPE type);
  DroidGroup(unsigned id, GROUP_TYPE type, Droid& commander);

  /* Accessors */
  [[nodiscard]] const Droid& get_commander() const;
  [[nodiscard]] unsigned get_commander_level() const;

  /// Add a droid to this group
	void add(Droid& droid);

  ///
	void remove(Droid& droid);

  /// @return `true` if this is a command group
	[[nodiscard]] bool is_command_group() const noexcept;

  /**
   * @return `true` if any of this group's droids have
   *    electronic weapons attached
   */
	[[nodiscard]] bool has_electronic_weapon() const;

  ///
	void commander_gain_experience(unsigned exp);

  ///
  void increment_commander_kills();

private:
	using enum GROUP_TYPE;

  /// The unique ID for this group
	unsigned id = 0;

  /// What kind of group is this?
	GROUP_TYPE type = NORMAL;

  /**
   * Non-owning pointer to this group's commander.
   * Set to `nullptr` if this is not a command group
   */
	Droid* commander = nullptr;

  /// The list of droids belonging to this group
	std::vector<Droid*> members;
};


#endif // WARZONE2100_DROID_GROUP_H
