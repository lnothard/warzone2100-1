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
 * @file console.h
 */

#ifndef __INCLUDED_SRC_CONSOLE_H__
#define __INCLUDED_SRC_CONSOLE_H__

#include "hci.h"

static constexpr auto	DEFAULT_MESSAGE_DURATION = GAME_TICKS_PER_SEC * 5;
static constexpr auto	DEFAULT_MESSAGE_DURATION_CAMPAIGN	= GAME_TICKS_PER_SEC * 12;

// Chat/history "window"
static constexpr auto CON_BORDER_WIDTH = 4;
static constexpr auto CON_BORDER_HEIGHT	=	4;
static constexpr auto HISTORYBOX_X = RET_X;
static const auto HISTORYBOX_Y = RET_Y - 80;
static constexpr auto NumDisplayLines = 4;

static constexpr auto MAX_CONSOLE_MESSAGES = 64;
static constexpr auto MAX_CONSOLE_STRING_LENGTH = 255;
static constexpr auto MAX_CONSOLE_TMP_STRING_LENGTH =	255;

enum class CONSOLE_TEXT_JUSTIFICATION
{
	LEFT,
	RIGHT,
	CENTRE,
  DEFAULT = LEFT
};

// Declare any messages that you want to be debounced here, along with their debounce time.
// This has to be done as a 1-member struct rather than an enum to allow distinguishing
// between different mesages with the same bounce time.
struct DEBOUNCED_MESSAGE
{
	unsigned int debounceTime;
};

struct ConsoleMessage
{
  using enum CONSOLE_TEXT_JUSTIFICATION;
	std::string text;
	CONSOLE_TEXT_JUSTIFICATION justification = DEFAULT;
	unsigned sender;
	bool team;
	std::size_t duration;
};

struct CONSOLE
{
    unsigned topX;
    unsigned topY;
    unsigned width;
    unsigned textDepth;
    bool permanent;
};

struct CONSOLE_MESSAGE
{
    WzText display;
    unsigned timeAdded; // When was it added to our list?
    unsigned duration;
    CONSOLE_TEXT_JUSTIFICATION JustifyType; // text justification
    unsigned player; // Player who sent this message or SYSTEM_MESSAGE
    CONSOLE_MESSAGE(const std::string& text, iV_fonts fontID, UDWORD time, UDWORD duration,
                    CONSOLE_TEXT_JUSTIFICATION justify, int plr)
            : display(text, fontID), timeAdded(time), duration(duration), JustifyType(justify), player(plr)
    {
    }

    CONSOLE_MESSAGE& operator =(CONSOLE_MESSAGE&& input) noexcept
    {
      display = std::move(input.display);
      timeAdded = input.timeAdded;
      duration = input.duration;
      JustifyType = input.JustifyType;
      player = input.player;
      return *this;
    }
};

const DEBOUNCED_MESSAGE CANNOT_BUILD_BURNING = {2500};

/* ID to use for addConsoleMessage() in case of a system message */
#define	SYSTEM_MESSAGE				(-1)
#define NOTIFY_MESSAGE				(-2)	// mainly used for lobby & error messages
#define INFO_MESSAGE				(-3)	// This type is not stored, it is used for simple messages
#define SPECTATOR_MESSAGE			(-4)	// Used for in-game spectator messages (NET_SPECTEXTMSG)

#define MAX_CONSOLE_MESSAGE_DURATION	((UDWORD)-1)
#define DEFAULT_CONSOLE_MESSAGE_DURATION	0

extern char ConsoleString[MAX_CONSOLE_TMP_STRING_LENGTH];

bool addConsoleMessage(const char* Text, CONSOLE_TEXT_JUSTIFICATION jusType, SDWORD player, bool team = false,
                       UDWORD duration = DEFAULT_CONSOLE_MESSAGE_DURATION);
bool addConsoleMessageDebounced(const char* Text, CONSOLE_TEXT_JUSTIFICATION jusType, SDWORD player,
                                const DEBOUNCED_MESSAGE& debouncedMessage, bool team = false,
                                UDWORD duration = DEFAULT_CONSOLE_MESSAGE_DURATION);
void updateConsoleMessages();
void initConsoleMessages();
void shutdownConsoleMessages();
void removeTopConsoleMessage();
void displayConsoleMessages();
void displayOldMessages();
void flushConsoleMessages();
void setConsoleBackdropStatus(bool state);
void enableConsoleDisplay(bool state);
bool getConsoleDisplayStatus();
void setConsoleSizePos(UDWORD x, UDWORD y, UDWORD width);
void setConsolePermanence(bool state, bool bClearOld);
void clearActiveConsole();
bool mouseOverConsoleBox();
bool mouseOverHistoryConsoleBox();
std::size_t getNumberConsoleMessages();
void setConsoleLineInfo(UDWORD vis);
UDWORD getConsoleLineInfo();
void permitNewConsoleMessages(bool allow);
void toggleConsoleDrop();
void setHistoryMode(bool mode);
void clearInfoMessages();

typedef std::function<void(ConsoleMessage const&)> CONSOLE_MESSAGE_LISTENER;
void consoleAddMessageListener(const std::shared_ptr<CONSOLE_MESSAGE_LISTENER>& listener);
void consoleRemoveMessageListener(const std::shared_ptr<CONSOLE_MESSAGE_LISTENER>& listener);

#if defined(DEBUG)
# define debug_console(...) \
	console(__VA_ARGS__)
#else // defined(DEBUG)
# define debug_console(...) (void)0
#endif // !defined(DEBUG)

void console(const char* pFormat, ...); /// Print always to the ingame console

/**
 Usage:
	CONPRINTF("format", data);
	NOTE: This class of messages are NOT saved in the history
	logs.  These are "one shot" type of messages.

 eg.
	CONPRINTF("Hello %d", 123);
*/
template <typename... P>
static inline void CONPRINTF(P&&... params)
{
	snprintf(ConsoleString, sizeof(ConsoleString), std::forward<P>(params)...);
	addConsoleMessage(ConsoleString, CONSOLE_TEXT_JUSTIFICATION::DEFAULT,
                    INFO_MESSAGE);
}

#include <functional>

typedef std::function<void ()> CONSOLE_CALC_LAYOUT_FUNC;
void setConsoleCalcLayout(const CONSOLE_CALC_LAYOUT_FUNC& layoutFunc);

void consoleScreenDidChangeSize(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth,
                                unsigned int newHeight);

#endif // __INCLUDED_SRC_CONSOLE_H__
