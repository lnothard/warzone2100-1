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
  ~Group() = default;
	Group();
  explicit Group(unsigned id);
  Group(unsigned id, GROUP_TYPE type);
  Group(unsigned id, GROUP_TYPE type, Droid& commander);

  Group(Group const& rhs);
  Group& operator=(Group const& rhs);

  Group(Group&& rhs) noexcept = default;
  Group& operator=(Group&& rhs) noexcept = default;

  /**
   * Add a droid to the group. Remove it from its existing
   * group if it exists
   */
	void add(Droid* psDroid);

  [[nodiscard]] static std::unique_ptr<Group> create(unsigned id);

  /// Remove a droid from the group.
	void remove(Droid* psDroid);
  [[nodiscard]] bool isCommandGroup() const noexcept;
  [[nodiscard]] bool hasElectronicWeapon() const;
  [[nodiscard]] Droid const* getCommander() const;
  [[nodiscard]] std::vector<Droid*> const& getMembers() const;
	void orderGroup(ORDER_TYPE order); // give an order all the droids of the group
	void orderGroup(ORDER_TYPE order, unsigned x, unsigned y);

	/// Give an order all the droids of the group (using location)
	void orderGroup(ORDER_TYPE order, BaseObject* psObj);

  // set the secondary state for a group of droids
	void setSecondary(SECONDARY_ORDER sec, SECONDARY_STATE state);
private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

#endif // __INCLUDED_SRC_GROUP_H__
