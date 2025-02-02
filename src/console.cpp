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
 * @file console.cpp
 * Functions for the in-game console
 *
 * Alex McLean, Pumpkin Studios, EIDOS Interactive
 */

#include <deque>
#include <set>
#include <sstream>

#include "lib/ivis_opengl/piepalette.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"

#include "ai.h"
#include "console.h"
#include "lib/framework/input.h"
#include "main.h"

#define		TIMER_Y					22
#define RET_FORMWIDTH		132

bool challengeActive;
bool bEnemyAllyRadarColor;
bool isSecondaryWindowUp();
void iV_TransBoxFill(float, float, float, float);
void pie_SetFogStatus(bool);
void drawBlueBox(unsigned, unsigned, unsigned, unsigned);

static std::deque<CONSOLE_MESSAGE> ActiveMessages; // we add all messages to this container
static std::deque<CONSOLE_MESSAGE> TeamMessages; // history of team/private communications
static std::deque<CONSOLE_MESSAGE> HistoryMessages; // history of all other communications
static std::deque<CONSOLE_MESSAGE> InfoMessages;
static bool bConsoleDropped = false; // Is the console history on or off?
static bool HistoryMode = false; // toggle between team & global history
static int updatepos = 0; // if user wants to scroll back the history log
static int linePitch = 0; // the pitch of a line
static bool showBackgroundColor = false; // if user wants to add more contrast to the history display
static CONSOLE mainConsole; // Stores the console dimensions and states
static CONSOLE historyConsole; // Stores the History console dimensions and states
static unsigned messageDuration; /** How long do messages last for? */
static bool bTextBoxActive = false; /** Is there a box under the console text? */
static bool bConsoleDisplayEnabled = false; /** Is the console being displayed? */
static unsigned consoleVisibleLines; /** How many lines are displayed? */
static int allowNewMessages; /** Whether new messages are allowed to be added */
static std::set<std::shared_ptr<CONSOLE_MESSAGE_LISTENER>> messageListeners;

/// Globally available string for new console messages.
char ConsoleString[MAX_CONSOLE_TMP_STRING_LENGTH];

void consoleAddMessageListener(const std::shared_ptr<CONSOLE_MESSAGE_LISTENER>& listener)
{
	messageListeners.insert(listener);
}

void consoleRemoveMessageListener(const std::shared_ptr<CONSOLE_MESSAGE_LISTENER>& listener)
{
	messageListeners.erase(listener);
}

static CONSOLE_CALC_LAYOUT_FUNC calcLayoutFunc;

/**
 * Specify how long messages will stay on screen.
 */
static void setConsoleMessageDuration(std::size_t time)
{
	messageDuration = time;
}

void setConsoleCalcLayout(const CONSOLE_CALC_LAYOUT_FUNC& layoutFunc)
{
	calcLayoutFunc = layoutFunc;
	if (calcLayoutFunc != nullptr)  {
		calcLayoutFunc();
	}
}

/** Sets the system up */
void initConsoleMessages()
{
  unsigned duration = DEFAULT_MESSAGE_DURATION;

	linePitch = iV_GetTextLineSize(font_regular);
	bConsoleDropped = false;
	setConsoleMessageDuration(duration); // Setup how long messages are displayed for
	setConsoleBackdropStatus(true); // No box under the text
	enableConsoleDisplay(true); // Turn on the console display

	//	Set up the main console size and position x,y,width
	setConsoleCalcLayout([]()
	{
    setConsoleSizePos(16, !challengeActive
                          ? 32 : 32 + TIMER_Y,
                      pie_GetVideoBufferWidth() - 32);
	});
	historyConsole.topX = HISTORYBOX_X;
	historyConsole.topY = HISTORYBOX_Y;
	historyConsole.width = pie_GetVideoBufferWidth() - 32;
	setConsoleLineInfo(MAX_CONSOLE_MESSAGES / 4 + 4);
	setConsolePermanence(false, true); // We're not initially having permanent messages
	permitNewConsoleMessages(true); // Allow new messages
}

void shutdownConsoleMessages()
{
	permitNewConsoleMessages(false);
	flushConsoleMessages();
	clearInfoMessages();
}

void consoleScreenDidChangeSize(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth,
                                unsigned int newHeight)
{
	if (calcLayoutFunc == nullptr) return;
	calcLayoutFunc();
}

// toggle between team & global history
void setHistoryMode(bool mode)
{
	HistoryMode = mode;
}

/** Open the console when it's closed and close it when it's open. */
void toggleConsoleDrop()
{
	if (!bConsoleDropped)
	{
		// it was closed, so play open sound
		bConsoleDropped = true;
		audio_PlayTrack(ID_SOUND_WINDOWOPEN);
	}
	else
	{
		// play closing sound
		audio_PlayTrack(ID_SOUND_WINDOWCLOSE);
		bConsoleDropped = false;
	}
}

bool addConsoleMessageDebounced(const char* Text, CONSOLE_TEXT_JUSTIFICATION jusType, SDWORD player,
                                const DEBOUNCED_MESSAGE& message, bool team, UDWORD duration)
{
	// Messages are debounced individually - one debounced message won't stop a different one from appearing
	static std::map<const DEBOUNCED_MESSAGE*, std::chrono::steady_clock::time_point> lastAllowedMessageTimes;

	const std::chrono::milliseconds DEBOUNCE_TIME(message.debounceTime);
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	const auto lastAllowedMessageTime = lastAllowedMessageTimes.find(&message);

	if (lastAllowedMessageTime == lastAllowedMessageTimes.end() || std::chrono::duration_cast<
		std::chrono::milliseconds>(now - lastAllowedMessageTime->second) >= DEBOUNCE_TIME)
	{
		lastAllowedMessageTimes[&message] = now;
		return addConsoleMessage(Text, jusType, player, team, duration);
	}
	else
	{
		return false;
	}
}

/** Add a string to the console. */
bool addConsoleMessage(const std::string& text, CONSOLE_TEXT_JUSTIFICATION jusType,
                       unsigned player, bool team, std::size_t duration)
{
	ConsoleMessage message = {text, jusType, player, team, duration};
	for (const auto& listener : messageListeners)
	{
		(*listener)(message);
	}

	if (!allowNewMessages)  {
    // don't allow it to be added if we've disabled adding of new messages
		return false;
	}

	std::istringstream stream(text);
	std::string lines;

	while (std::getline(stream, lines))
	{
		// We got one "line" from the total string, now we must check
		// to see if it fits, if not, we truncate it. (Full text is in the logs)
		// NOTE: may want to break up line into multi-line so it matches the log
		std::string FitText(lines);
		while (!FitText.empty())
		{
			unsigned pixelWidth = iV_GetTextWidth(FitText.c_str(), font_regular);
			if (pixelWidth <= mainConsole.width)  {
				break;
			}
			FitText.resize(FitText.length() - 1); // Erase last char.
		}

		debug(LOG_CONSOLE, "(to player %d): %s", (int)player, FitText.c_str());

		UDWORD newMsgDuration = (duration != DEFAULT_CONSOLE_MESSAGE_DURATION) ? duration : messageDuration;

		if (player == INFO_MESSAGE)
		{
			InfoMessages.emplace_back(FitText, font_regular, realTime, newMsgDuration, jusType, player);
		}
		else
		{
			ActiveMessages.emplace_back(FitText, font_regular, realTime, newMsgDuration, jusType, player);
			// everything gets logged here for a specific period of time
			if (team)
			{
				TeamMessages.emplace_back(FitText, font_regular, realTime, newMsgDuration, jusType, player);
				// persistent team specific logs
			}
			HistoryMessages.emplace_back(FitText, font_regular, realTime, newMsgDuration, jusType, player);
			// persistent messages (all types)
		}
	}

	return true;
}

/// @return The number of active console messages
std::size_t getNumberConsoleMessages()
{
	return ActiveMessages.size();
}

/**
 * Update the console messages.
 * This function will remove messages that are overdue.
 */
void updateConsoleMessages()
{
	// If there are no messages or we're on permanent (usually for scripts) then exit
	if ((!getNumberConsoleMessages() && InfoMessages.empty()) || mainConsole.permanent)
	{
		return;
	}
	for (auto i = InfoMessages.begin(); i != InfoMessages.end();)
	{
		if ((i->duration != MAX_CONSOLE_MESSAGE_DURATION) && (realTime - i->timeAdded > i->duration))
		{
			i = InfoMessages.erase(i);
		}
		else
		{
			++i;
		}
	}
	// Time to kill all expired ones
	for (auto i = ActiveMessages.begin(); i != ActiveMessages.end();)
	{
		if ((i->duration != MAX_CONSOLE_MESSAGE_DURATION) && (realTime - i->timeAdded > i->duration))
		{
			i = ActiveMessages.erase(i);
		}
		else
		{
			++i;
		}
	}
}

/**
	Remove the top message on screen.
	This and setConsoleMessageDuration should be sufficient to allow
	us to put up messages that stay there until we remove them
	ourselves - be sure and reset message duration afterwards
*/
void removeTopConsoleMessage()
{
	if (getNumberConsoleMessages())
	{
		ActiveMessages.pop_front();
	}
}

/** Clears just Active console messages */
void clearActiveConsole()
{
	ActiveMessages.clear();
}

/** Clears all console messages */
void flushConsoleMessages()
{
	ActiveMessages.clear();
	TeamMessages.clear();
	HistoryMessages.clear();
}

/** Sets console text color depending on message type */
static PIELIGHT getConsoleTextColor(unsigned player)
{
	// System messages
	if (player == SYSTEM_MESSAGE)
	{
		return WZCOL_CONS_TEXT_SYSTEM;
	}
	else if (player == NOTIFY_MESSAGE)
	{
		return WZCOL_YELLOW;
	}
	else if (player == INFO_MESSAGE)
	{
		return WZCOL_CONS_TEXT_INFO;
	}
	else if (player == SPECTATOR_MESSAGE)
	{
		return WZCOL_TEXT_MEDIUM;
	}
	else
	{
		// Only use friend-foe colors if we are (potentially) a player
		if (selectedPlayer < MAX_PLAYERS)
		{
			// Don't use friend-foe colors in the lobby
			if (bEnemyAllyRadarColor && (GetGameMode() == GS_NORMAL))
			{
				if (aiCheckAlliances(player, selectedPlayer))
				{
					if (selectedPlayer == player)
					{
						return WZCOL_TEXT_BRIGHT;
					}
					return WZCOL_CONS_TEXT_USER_ALLY;
				}
				else
				{
					return WZCOL_CONS_TEXT_USER_ENEMY;
				}
			}
		}

		return WZCOL_TEXT_BRIGHT;
	}
}

static void console_drawtext(WzText& display, PIELIGHT colour, int x, int y,
                             CONSOLE_TEXT_JUSTIFICATION justify, int width)
{
  using enum CONSOLE_TEXT_JUSTIFICATION;
	switch (justify)  {
	case LEFT:
		break;
	case RIGHT:
		x = x + width - display.width();
		break;
	case CENTRE:
		x = x + (width - display.width()) / 2;
		break;
	}
	display.render(x, y, colour);
}

// Show global (mode=false) or team (mode=true) history messages
void displayOldMessages(bool mode)
{
	int startpos = 0;
	std::deque<CONSOLE_MESSAGE>* WhichMessages;

	if (mode) {
		WhichMessages = &TeamMessages;
	}
	else {
		WhichMessages = &HistoryMessages;
	}

  if (WhichMessages->empty())
    return;

  auto count = WhichMessages->size();// total number of messages
  if (count > NumDisplayLines) // if we have more than we can display
  {
    startpos = count - NumDisplayLines; // show last X lines
    startpos += updatepos; // unless user wants to start at something else
    if (startpos < 0) { // don't underflow
      startpos = 0;
      updatepos = (count - NumDisplayLines) * -1; // reset back to first entry
      count = NumDisplayLines;
    }
    else if (count + updatepos <= count) {
      count += updatepos; // user may want something different
    }
    else {
      // reset all, we got overflow
      count = WhichMessages->size();
      updatepos = 0;
      startpos = count - NumDisplayLines;
    }
  }

  int nudgeright = 0;
  int TextYpos = historyConsole.topY + linePitch - 2;

  if (isSecondaryWindowUp()) { // see if (build/research/...)window is up
    nudgeright = RET_FORMWIDTH + 2; // move text over
  }
  // if user wants to add a bit more contrast to the text
  if (showBackgroundColor) {
    iV_TransBoxFill(historyConsole.topX + nudgeright - CON_BORDER_WIDTH,
                    historyConsole.topY - historyConsole.textDepth - CON_BORDER_HEIGHT,
                    historyConsole.topX + historyConsole.width,
                    historyConsole.topY + (NumDisplayLines * linePitch) + CON_BORDER_HEIGHT);
  }

  for (auto i = startpos; i < count; ++i)
  {
    auto colour = mode ? WZCOL_CONS_TEXT_USER_ALLY : getConsoleTextColor((*WhichMessages)[i].player);
    console_drawtext((*WhichMessages)[i].display, colour, historyConsole.topX + nudgeright, TextYpos,
                     (*WhichMessages)[i].JustifyType, historyConsole.width);
    TextYpos += (*WhichMessages)[i].display.lineSize();
  }
}

/** Displays all the console messages */
void displayConsoleMessages()
{
	// Check if we have any messages we want to show
	if (ActiveMessages.empty() && !bConsoleDropped && InfoMessages.empty() ||
      !bConsoleDisplayEnabled && InfoMessages.empty()) {
		return;
	}

	pie_SetFogStatus(false);

	if (bConsoleDropped) {
		displayOldMessages(HistoryMode);
	}

	if (!InfoMessages.empty()) {
		auto i = InfoMessages.end() - 1; // we can only show the last one...
		auto tmp = pie_GetVideoBufferWidth();
		drawBlueBox(0, 0, tmp, 18);
		tmp -= i->display.width();
    console_drawtext(i->display, getConsoleTextColor(i->player),
                     tmp - 6, linePitch - 2, i->JustifyType,
                     i->display.width());
	}

  if (ActiveMessages.empty())
    return;

  auto TextYpos = mainConsole.topY;
  // Draw the blue background for the text (only in game, not lobby)
  if (bTextBoxActive && GetGameMode() == GS_NORMAL) {
    iV_TransBoxFill(mainConsole.topX - CON_BORDER_WIDTH,
                    mainConsole.topY - mainConsole.textDepth - CON_BORDER_HEIGHT,
                    mainConsole.topX + mainConsole.width,
                    mainConsole.topY + (getNumberConsoleMessages() * linePitch) + CON_BORDER_HEIGHT -
                    linePitch);
  }

  for (auto& ActiveMessage : ActiveMessages)
  {
    console_drawtext(ActiveMessage.display, getConsoleTextColor(ActiveMessage.player), mainConsole.topX,
                     TextYpos, ActiveMessage.JustifyType, mainConsole.width);
    TextYpos += ActiveMessage.display.lineSize();
  }
}

/** destroy CONPRINTF messages **/
void clearInfoMessages()
{
	InfoMessages.clear();
}

/** Allows toggling of the box under the console text */
void setConsoleBackdropStatus(bool state)
{
	bTextBoxActive = state;
}

/**
	Turns on and off display of console. It's worth
	noting that this is just the display so if you want
	to make sure that when it's turned back on again, there
	are no messages, the call flushConsoleMessages first.
*/
void enableConsoleDisplay(bool state)
{
	bConsoleDisplayEnabled = state;
}

/** Allows positioning of the console on screen */
void setConsoleSizePos(UDWORD x, UDWORD y, UDWORD width)
{
	mainConsole.topX = x;
	mainConsole.topY = y;
	mainConsole.width = width;

	/* Should be done below */
	mainConsole.textDepth = 8;

	// Do not call flushConsoleMessages() when simply changing console size/position -
	// it is possible for the console size/pos to change during display.
}

/**	Establishes whether the console messages stay there */
void setConsolePermanence(bool state, bool bClearOld)
{
	if (mainConsole.permanent && !state) {
		if (bClearOld) {
			flushConsoleMessages();
		}
		mainConsole.permanent = false;
    return;
	}
  if (bClearOld) {
    flushConsoleMessages();
  }
  mainConsole.permanent = state;
}

/** Check if mouse is over the Active console 'window' area */
bool mouseOverConsoleBox()
{
  if (getNumberConsoleMessages() && (UDWORD) mouseX() > mainConsole.topX &&
      (UDWORD) mouseY() > mainConsole.topY &&
      (UDWORD) mouseX() < mainConsole.topX + mainConsole.width &&
      (UDWORD) mouseY() < mainConsole.topY + 4 + linePitch * getNumberConsoleMessages()) {
		return true;
	}
	return false;
}

/** Check if mouse is over the History console 'window' area */
bool mouseOverHistoryConsoleBox()
{
	int nudgeright = 0;
	if (isSecondaryWindowUp()) {
		// if a build/research/... is up, we need to move text over by this much
		nudgeright = RET_FORMWIDTH;
	}

	// check to see if mouse is in the area when console is enabled
  if (!bConsoleDropped || !((UDWORD) mouseX() > historyConsole.topX + nudgeright) ||
      !((UDWORD) mouseY() > historyConsole.topY) ||
      !((UDWORD) mouseX() < historyConsole.topX + historyConsole.width) ||
      !((UDWORD) mouseY() < historyConsole.topY + 4 + linePitch * NumDisplayLines))
    return false;

  if (mousePressed(MOUSE_WUP)) {
    updatepos--;
  }
  else if (mousePressed(MOUSE_WDN)) {
    updatepos++;
  }

  if (keyDown(KEY_LCTRL)) {
    showBackgroundColor = true;
  }
  else {
    showBackgroundColor = false;
  }
  return true;
}

/** Sets up how many lines are allowed and how many are visible */
void setConsoleLineInfo(unsigned vis)
{
	ASSERT(vis <= MAX_CONSOLE_MESSAGES, "Request for more visible lines in the console than exist");
	consoleVisibleLines = vis;
}

/** get how many lines are allowed and how many are visible */
unsigned getConsoleLineInfo()
{
	return consoleVisibleLines;
}

/// Set if new messages may be added to the console
void permitNewConsoleMessages(bool allow)
{
	allowNewMessages = allow;
}

/// \return the visibility of the console
bool getConsoleDisplayStatus()
{
	return (bConsoleDisplayEnabled);
}

/** Use console() for when you want to display a console message,
    and keep it in the history logs.

	Use the macro CONPRINTF if you don't want it to be in the history logs.
**/
void console(const char* pFormat, ...)
{
	char aBuffer[500]; // Output string buffer
	va_list pArgs; // Format arguments

	/* Print out the string */
	va_start(pArgs, pFormat);
	vsnprintf(aBuffer, sizeof(aBuffer), pFormat, pArgs);
	va_end(pArgs);

	/* Output it */
	addConsoleMessage(aBuffer, CONSOLE_TEXT_JUSTIFICATION::DEFAULT, SYSTEM_MESSAGE);
}
