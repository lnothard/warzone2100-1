//
// Created by luna on 08/12/2021.
//

#include <algorithm>

#include "droid_group.h"

DroidGroup::DroidGroup(unsigned id)
  :id{id}
{
}

DroidGroup::DroidGroup(unsigned id, GROUP_TYPE type)
  : id{id}, type{type}
{
}

DroidGroup::DroidGroup(unsigned id, GROUP_TYPE type, Droid& commander)
  : id{id}, type{type}, commander{&commander}
{
}

void DroidGroup::add(Droid& droid)
{
  members.push_back(&droid);
}

bool DroidGroup::is_command_group() const noexcept
{
	return type == COMMAND;
}

bool DroidGroup::has_electronic_weapon() const
{
	return std::any_of(members.begin(), members.end(), [](const auto* droid)
	{
		return Impl::has_electronic_weapon(*dynamic_cast<const Impl::Unit*>(droid));
	});
}

unsigned DroidGroup::get_commander_level() const
{
	return commander->get_level();
}

void DroidGroup::commander_gain_experience(unsigned exp)
{
	commander->gain_experience(exp);
}

void DroidGroup::increment_commander_kills()
{
  commander->increment_kills();
}

const Droid& DroidGroup::get_commander() const
{
  return *commander;
}