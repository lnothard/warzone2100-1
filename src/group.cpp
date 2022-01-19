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
 * @file group.cpp
 * Link droids together into a group for AI etc.
 */

#include "lib/netplay/netplay.h"

#include "droid.h"
#include "group.h"
#include "multiplay.h"


bool Group::isCommandGroup() const noexcept
{
  return type == COMMAND;
}

bool Group::hasElectronicWeapon() const
{
  return std::any_of(members.begin(), members.end(),
                     [](const auto droid)
  {
    return droid->hasElectronicWeapon();
  });
}

const Droid& Group::getCommander() const
{
  return *psCommander;
}

std::unique_ptr<Group> Group::create(unsigned id)
{
	return std::make_unique<Group>(id);
}

Group::Group(unsigned id)
  :id{id}
{
}

Group::Group(unsigned id, GROUP_TYPE type)
  : id{id}, type{type}
{
}

Group::Group(unsigned id, GROUP_TYPE type, Droid& commander)
  : id{id}, type{type}, psCommander{&commander}
{
}

void Group::add(Droid* psDroid)
{
	if (psDroid == nullptr) {
    return;
  }

  for (auto member : members)
  {
    if (psDroid->getPlayer() != member->getPlayer()) {
      ASSERT(false, "grpJoin: Cannot have more than one players droids in a group");
      return;
    }
  }

  if (psDroid->group != nullptr) {
    psDroid->group->remove(psDroid);
  }
  psDroid->group = this;

  if (isTransporter(*psDroid)) {
    ASSERT_OR_RETURN(, (type == NORMAL), "grpJoin: Cannot have two transporters in a group");

    type = TRANSPORTER;
    members.push_back(psDroid);
  }
  else if (psDroid->getType() == DROID_TYPE::COMMAND &&
           type != TRANSPORTER) {
    ASSERT_OR_RETURN(, (type == NORMAL) && (psCommander == nullptr),
                     "grpJoin: Cannot have two command droids in a group");
    type = COMMAND;
    psCommander = psDroid;
  }
  else {
    members.push_back(psDroid);
  }

  if (type == COMMAND) {
    syncDebug("Droid %d joining command group %d", psDroid->getId(), psCommander != nullptr ? psCommander->getId() : 0);
  }
}

// remove a droid from a group
void Group::remove(Impl::Droid* psDroid)
{
	if (psDroid != nullptr && psDroid->group.get() != this) {
		ASSERT(false, "grpLeave: droid group does not match");
		return;
	}

	// SyncDebug
	if (psDroid != nullptr && type == COMMAND) {
		syncDebug("Droid %d leaving command group %d",
              psDroid->getId(), psCommander != nullptr
              ? psCommander->getId() : 0);
	}

  psDroid->group = nullptr;

  // update group's type
  if (psDroid->getType() == DROID_TYPE::COMMAND &&
      type == COMMAND) {
    type = NORMAL;
    psCommander = nullptr;
  } else if (isTransporter(*psDroid) &&
             type == TRANSPORTER) {
    type = NORMAL;
  }
}

// Give a group of droids an order
void Group::orderGroup(ORDER_TYPE order)
{
	for (auto droid : members)
	{
		::orderDroid(droid, order, ModeQueue);
	}
}

// Give a group of droids an order (using a Location)
void Group::orderGroup(ORDER_TYPE order, unsigned x, unsigned y)
{
	ASSERT_OR_RETURN(, validOrderForLoc(order), "orderGroup: Bad order");

	for (auto droid : members)
	{
		::orderDroidLoc(droid, order, x, y, bMultiMessages
    ? ModeQueue
    : ModeImmediate);
	}
}

void Group::orderGroup(ORDER_TYPE order, PersistentObject* psObj)
{
	ASSERT_OR_RETURN(, validOrderForObj(order), "orderGroup: Bad order");

	for (auto droid : members)
	{
		::orderDroidObj(droid, order, psObj, bMultiMessages
    ? ModeQueue
    : ModeImmediate);
	}
}

void Group::setSecondary(SECONDARY_ORDER sec, SECONDARY_STATE state)
{
	for (auto droid : members)
	{
		droid->secondarySetState(sec, state);
	}
}
