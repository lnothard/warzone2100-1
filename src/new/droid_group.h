//
// Created by luna on 08/12/2021.
//

#ifndef WARZONE2100_DROID_GROUP_H
#define WARZONE2100_DROID_GROUP_H

#include "droid.h"

std::vector<Droid_Group> droid_groups;

enum class GROUP_TYPE
{
	NORMAL,
	COMMAND,
	TRANSPORTER
};

class Droid_Group
{
public:
  Droid_Group(unsigned id);

	void add(Droid& droid);
	void remove(Droid& droid);
	[[nodiscard]] bool is_command_group() const noexcept;
	[[nodiscard]] bool has_electronic_weapon() const;
	[[nodiscard]] unsigned get_commander_level() const;
	void commander_gain_experience(unsigned exp);
private:
	using enum GROUP_TYPE;

	unsigned id;
	GROUP_TYPE type;
	Droid* commander;
	std::vector<Droid*> members;
};


#endif // WARZONE2100_DROID_GROUP_H
