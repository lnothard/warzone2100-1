/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2020  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/**
 * @file group.h
 * Responsible for handling groups of droids
 */

#ifndef __INCLUDED_SRC_GROUP_H__
#define __INCLUDED_SRC_GROUP_H__

#include "order.h"

class Droid;


enum class GROUP_TYPE
{
	NORMAL,
	COMMAND,
	TRANSPORTER
};

class Group
{
public:
	Group() = default;
  explicit Group(unsigned id);
  Group(unsigned id, GROUP_TYPE type);
  Group(unsigned id, GROUP_TYPE type, Droid& commander);

  /**
   * Add a droid to the group. Remove it from its existing
   * group if it exists
   */
	void add(Droid* psDroid);

  [[nodiscard]] static std::unique_ptr<Group> create(unsigned id);

  /// Remove a droid from the group.
	void remove(Impl::Droid* psDroid);

  [[nodiscard]] bool isCommandGroup() const noexcept;

  [[nodiscard]] bool hasElectronicWeapon() const;

  [[nodiscard]] const Droid& getCommander() const;

	void orderGroup(ORDER_TYPE order); // give an order all the droids of the group
	void orderGroup(ORDER_TYPE order, unsigned x, unsigned y);

	/// Give an order all the droids of the group (using location)
	void orderGroup(ORDER_TYPE order, PersistentObject* psObj); // give an order all the droids of the group (using object)

	void setSecondary(SECONDARY_ORDER sec, SECONDARY_STATE state); // set the secondary state for a group of droids
private:
  friend class Droid;
  using enum GROUP_TYPE;
	GROUP_TYPE type = NORMAL;
  unsigned id = 0;

  /// List of droids in the group
	std::vector<Impl::Droid*> members;

  /**
   * Non-owning pointer to this group's commander.
   * Set to `nullptr` if this is not a command group
   */
	Droid* psCommander = nullptr;
};

/**
 * Create a new group, use -1 to generate a new ID. Never
 * use id != -1 unless loading from a save game.
 */
std::unique_ptr<Group> grpCreate(unsigned id = -1);

#endif // __INCLUDED_SRC_GROUP_H__
