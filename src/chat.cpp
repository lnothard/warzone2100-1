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
 * @file chat.cpp
 */

#include <sstream>

#include "lib/netplay/netplay.h"

#include "chat.h"
#include "multiplay.h"
#include "qtscript.h"
#include "ai.h"

ChatMessage::ChatMessage(unsigned sender, char* text)
  : sender{sender}, text{text}
{
}

bool ChatMessage::is_global() const
{
	return !allies_only && intended_recipients.empty();
}

bool ChatMessage::should_receive(unsigned player) const
{
  // if `is_global()` returns `true`, the `player` does not matter,
  // since all receive the message
  return is_global() ||
         // if `player` is not found in `intended_recipients`, return `false`
         // if `allies_only` is set to `true`, and there is an alliance between `player`
         // and `sender_id`, return `true`
         intended_recipients.find(player) != intended_recipients.end() ||
         (allies_only && sender < MAX_PLAYERS && player < MAX_PLAYERS && aiCheckAlliances(sender, player));
}

std::unique_ptr< std::vector<unsigned> > ChatMessage::get_recipients() const
{
	auto recipients = std::make_unique<std::vector<unsigned>>();

	for (auto player = 0; player < MAX_CONNECTED_PLAYERS; player++)
	{
		if (should_receive(player) && openchannels[player]) {
			recipients->push_back(player);
		}
	}
  return recipients;
}

std::string ChatMessage::formatReceivers() const
{
	if (is_global())
	{
		return _("Global");
	}

	if (allies_only && intended_recipients.empty())
	{
		return _("Allies");
	}

	auto directs = intended_recipients.begin();
	std::stringstream ss;
	if (allies_only)
	{
		ss << _("Allies");
	}
	else
	{
		ss << _("private to ");
		ss << getPlayerName(*directs++);
	}

	while (directs != intended_recipients.end())
	{
		auto nextName = getPlayerName(*directs++);
		if (!nextName)
		{
			continue;
		}
		ss << (directs == intended_recipients.end() ? _(" and ") : ", ");
		ss << nextName;
	}

	return ss.str();
}

void ChatMessage::sendToHumanPlayers() const
{
	char formatted[MAX_CONSOLE_STRING_LENGTH];
	ssprintf(formatted, "%s (%s): %s", getPlayerName(sender), formatReceivers().c_str(), text);

	auto message = NetworkTextMessage(sender, formatted);
	message.teamSpecific = allies_only && intended_recipients.empty();

	if (sender == selectedPlayer ||
      should_receive(selectedPlayer)) {
		printInGameTextMessage(message);
	}

	if (is_global()) {
		message.enqueue(NETbroadcastQueue());
		return;
	}

	for (auto& receiver : *get_recipients())
	{
		if (isHumanPlayer(receiver)) {
			message.enqueue(NETnetQueue(receiver));
		}
	}
}

void ChatMessage::sendToAiPlayer(unsigned receiver)
{
	if (!ingame.localOptionsReceived) {
		return;
	}

	auto responsiblePlayer = whosResponsible(receiver);

	if (responsiblePlayer >= MAX_PLAYERS &&
      responsiblePlayer != NetPlay.hostPlayer) {
		debug(LOG_ERROR, "sendToAiPlayer() - responsiblePlayer >= MAX_PLAYERS");
		return;
	}

	if (!isHumanPlayer(responsiblePlayer)) {
		debug(LOG_ERROR, "sendToAiPlayer() - responsiblePlayer is not human.");
		return;
	}

	NETbeginEncode(NETnetQueue(responsiblePlayer), NET_AITEXTMSG);
	NETuint32_t(&sender);
	NETuint32_t(&receiver);
	NETstring(text, MAX_CONSOLE_STRING_LENGTH);
	NETend();
}

void ChatMessage::sendToAiPlayers()
{
	for (auto receiver : *get_recipients())
	{
    if (isHumanPlayer(receiver)) {
      continue;
    }
    if (myResponsibility(receiver)) {
      triggerEventChat(sender, receiver, text);
    }
    else {
      sendToAiPlayer(receiver);
    }
  }
}

void ChatMessage::sendToSpectators()
{
	if (!ingame.localOptionsReceived)
	{
		return;
	}

	char formatted[MAX_CONSOLE_STRING_LENGTH];
	ssprintf(formatted, "%s (%s): %s", getPlayerName(sender), _("Spectators"), text);

	if ((sender == selectedPlayer || should_receive(selectedPlayer)) && NetPlay.players[selectedPlayer].isSpectator)
	{
		auto message = NetworkTextMessage(SPECTATOR_MESSAGE, formatted);
		printInGameTextMessage(message);
	}

	for (auto receiver : *get_recipients())
	{
		if (isHumanPlayer(receiver) && NetPlay.players[receiver].isSpectator && receiver != selectedPlayer)
		{
			ASSERT(!myResponsibility(receiver), "Should not be my responsibility...");
			enqueueSpectatorMessage(NETnetQueue(receiver), formatted);
		}
	}
}

void ChatMessage::enqueueSpectatorMessage(NETQUEUE queue, char const* formattedMsg)
{
	NETbeginEncode(queue, NET_SPECTEXTMSG);
	NETuint32_t(&sender);
	NETstring(formattedMsg, MAX_CONSOLE_STRING_LENGTH);
	NETend();
}

void ChatMessage::addReceiverByPosition(uint32_t playerPosition)
{
	int32_t playerIndex = findPlayerIndexByPosition(playerPosition);
	if (playerIndex >= 0)
	{
		intended_recipients.insert(playerIndex);
	}
}

void ChatMessage::addReceiverByIndex(uint32_t playerIndex)
{
	intended_recipients.insert(playerIndex);
}

void ChatMessage::send()
{
	if (NetPlay.players[selectedPlayer].isSpectator && !NetPlay.isHost)
	{
		sendToSpectators();
	}
	else
	{
		sendToHumanPlayers();
		if (NetPlay.isHost && NetPlay.players[selectedPlayer].isSpectator)
		{
			// spectator hosts do get to send messages visible to all players,
			// but not AI / scripts
			return;
		}
		sendToAiPlayers();
		triggerEventChat(sender, sender, text);
	}
}
