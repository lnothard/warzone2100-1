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

struct Group::Impl
{
  Impl() = default;
  Impl(unsigned id, GROUP_TYPE type);
  Impl(unsigned id, GROUP_TYPE type, Droid& commander);
  explicit Impl(unsigned id);

  unsigned id = 0;
  GROUP_TYPE type = GROUP_TYPE::NORMAL;

  /// List of droids in the group
  std::vector<Droid*> members;

  /**
   * Non-owning pointer to this group's commander.
   * Set to \c nullptr if this is not a command group
   */
  Droid* psCommander = nullptr;
};

Group::Group()
  : pimpl{std::make_unique<Impl>()}
{
}

Group::Group(unsigned id)
  : pimpl{std::make_unique<Impl>(id)}
{
}

Group::Group(unsigned id, GROUP_TYPE type)
  : pimpl{std::make_unique<Impl>(id, type)}
{
}

Group::Group(unsigned id, GROUP_TYPE type, Droid& commander)
  : pimpl{std::make_unique<Impl>(id, type, commander)}
{
}

Group::Group(Group const& rhs)
  : pimpl{std::make_unique<Impl>(*rhs.pimpl)}
{
}

Group& Group::operator=(Group const& rhs)
{
  if (this == &rhs) return *this;
  *pimpl = *rhs.pimpl;
  return *this;
}

Group::Impl::Impl(unsigned id)
  : id{id}
{
}

Group::Impl::Impl(unsigned id, GROUP_TYPE type)
  : id{id}
  , type{type}
{
}

Group::Impl::Impl(unsigned id, GROUP_TYPE type, Droid& commander)
  : id{id}
  , type{type}
  , psCommander{&commander}
{
}

bool Group::isCommandGroup() const noexcept
{
  return pimpl && pimpl->type == GROUP_TYPE::COMMAND;
}

bool Group::hasElectronicWeapon() const
{
  if (!pimpl) return false;
  return std::any_of(pimpl->members.begin(),
                     pimpl->members.end(),
                     [](const auto droid) {
    return droid->hasElectronicWeapon();
  });
}

Droid const* Group::getCommander() const
{
  return pimpl ? pimpl->psCommander : nullptr;
}

std::vector<Droid*> const& Group::getMembers() const
{
  assert(pimpl); // FIXME
  return pimpl->members;
}

std::unique_ptr<Group> Group::create(unsigned id)
{
	return std::make_unique<Group>(id);
}

void Group::add(Droid* psDroid)
{
  if (!pimpl || !psDroid) return;

  if (std::any_of(pimpl->members.begin(), pimpl->members.end(),
                  [&psDroid](const auto member) {
    return psDroid->playerManager->getPlayer() != member->playerManager->getPlayer();
  })) {
    ASSERT(false, "grpJoin: Cannot have more than one players droids in a group");
    return;
  }

  if (psDroid->pimpl->group != nullptr) {
    psDroid->group->remove(psDroid);
  }
  psDroid->group = this;

  if (isTransporter(*psDroid)) {
    ASSERT_OR_RETURN(, (pimpl->type == GROUP_TYPE::NORMAL), "grpJoin: Cannot have two transporters in a group");

    pimpl->type = GROUP_TYPE::TRANSPORTER;
    pimpl->members.push_back(psDroid);
  }
  else if (psDroid->getType() == DROID_TYPE::COMMAND &&
           pimpl->type != GROUP_TYPE::TRANSPORTER) {
    ASSERT_OR_RETURN(, (pimpl->type == GROUP_TYPE::NORMAL) && (pimpl->psCommander == nullptr),
                     "grpJoin: Cannot have two command droids in a group");
    pimpl->type = GROUP_TYPE::COMMAND;
    pimpl->psCommander = psDroid;
  }
  else {
    pimpl->members.push_back(psDroid);
  }

  if (pimpl->type == GROUP_TYPE::COMMAND) {
    syncDebug("Droid %d joining command group %d", psDroid->getId(),
              pimpl->psCommander != nullptr ? pimpl->psCommander->getId() : 0);
  }
}

// remove a droid from a group
void Group::remove(Droid* psDroid)
{
  if (!pimpl) return;
	if (psDroid != nullptr && psDroid->group != this) {
		ASSERT(false, "grpLeave: droid group does not match");
		return;
	}

	// SyncDebug
	if (psDroid != nullptr && pimpl->type == GROUP_TYPE::COMMAND) {
		syncDebug("Droid %d leaving command group %d",
              psDroid->getId(), pimpl->psCommander != nullptr
              ? pimpl->psCommander->getId() : 0);
	}

  psDroid->group = nullptr;

  // update group's type
  if (psDroid->getType() == DROID_TYPE::COMMAND &&
      pimpl->type == GROUP_TYPE::COMMAND) {
    pimpl->type = GROUP_TYPE::NORMAL;
    pimpl->psCommander = nullptr;
  } else if (isTransporter(*psDroid) &&
             pimpl->type == GROUP_TYPE::TRANSPORTER) {
    pimpl->type = GROUP_TYPE::NORMAL;
  }
}

// Give a group of droids an order
void Group::orderGroup(ORDER_TYPE order)
{
  if (!pimpl) return;
	for (auto droid : pimpl->members)
	{
		orderDroid(droid, order, ModeQueue);
	}
}

// Give a group of droids an order (using a Location)
void Group::orderGroup(ORDER_TYPE order, unsigned x, unsigned y)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Group object in undefined state");
	ASSERT_OR_RETURN(, validOrderForLoc(order), "orderGroup: Bad order");

	for (auto droid : pimpl->members)
	{
		orderDroidLoc(droid, order, x, y, bMultiMessages
    ? ModeQueue
    : ModeImmediate);
	}
}

void Group::orderGroup(ORDER_TYPE order, BaseObject* target)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Group object in undefined state");
	ASSERT_OR_RETURN(, validOrderForObj(order), "orderGroup: Bad order");

	for (auto droid : pimpl->members)
	{
		orderDroidObj(droid, order, target, bMultiMessages
    ? ModeQueue
    : ModeImmediate);
	}
}

void Group::setSecondary(SECONDARY_ORDER sec, SECONDARY_STATE state)
{
  ASSERT_OR_RETURN(, pimpl != nullptr, "Group object in undefined state");
	for (auto droid : pimpl->members)
	{
		droid->secondarySetState(sec, state);
	}
}
