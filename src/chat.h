/*
	This file is part of Warzone 2100.
	Copyright (C) 2020  Warzone 2100 Project

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
 * @file chat.h
 *
 */

#ifndef __INCLUDED_SRC_CHAT_H__
#define __INCLUDED_SRC_CHAT_H__

#include <set>
#include <vector>
#include <sstream>

#include "multiplay.h"

struct ChatMessage
{
  ChatMessage() = default;
	ChatMessage(unsigned sender, std::string text);

	void send();
	void addReceiverByPosition(unsigned playerPosition);
	void addReceiverByIndex(unsigned playerIndex);

  /// @return `true` if visible to all players
	[[nodiscard]] bool is_global() const;

  /// @return `true` if `player` is a valid recipient for this message
	[[nodiscard]] bool should_receive(unsigned player) const;

  /// @return A list of the actual recipients of this message
	[[nodiscard]] std::unique_ptr< std::vector<unsigned> > get_recipients() const;

	[[nodiscard]] std::string formatReceivers() const;


	void sendToHumanPlayers();
	void sendToAiPlayers();
	void sendToAiPlayer(unsigned receiver);
	void sendToSpectators();
	void enqueueSpectatorMessage(NETQUEUE queue, char const* formattedMsg);

  unsigned sender = 0;

  std::string text {};

  /**
   * Set to `true` if this message should be private, i.e.,
   * it should only be visible to allies of `sender`
   */
  bool allies_only = false;

  /**
   * If `set.empty()` returns `true`, call `is_global()`. If
   * `is_global()` returns `true`, send this message to all players.
   * If `set.empty()` returned `false`, send only to the players
   * contained within this set.
   */
  std::set<unsigned> intended_recipients;
};

#endif // __INCLUDED_SRC_CHAT_H__
