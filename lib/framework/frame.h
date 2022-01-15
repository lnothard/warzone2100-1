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
 * @file frame.h
 * @brief The framework library initialisation and shutdown routines
 */

#ifndef _frame_h
#define _frame_h

/// Workaround X11 headers #defining Status @todo << see if still needed
#ifdef Status
# undef Status
#endif

/**
* @NOTE: the next two #include lines are needed by MSVC to override the default,
* non C99 compliant routines, and redefinition; different linkage errors
*/

#include "stdio_ext.h"
#include "string_ext.h"

#include <cstdlib>

#include "cursors.h"
#include "debug.h"
#include "i18n.h"
#include "macros.h"
#include "wzglobal.h"
#include "trig.h"

#define REALCONCAT(x, y) x ## y
#define CONCAT(x, y) REALCONCAT(x, y)

/// The player number corresponding to this client
extern unsigned selectedPlayer;

/**
 * The player number corresponding to this client (same as
 * selectedPlayer, unless changing players in the debug menu)
 */
extern unsigned realSelectedPlayer;

/// Maximum number of players in the game
static constexpr auto MAX_PLAYERS = 11;

/// One player slot is reserved for scavengers
static constexpr auto MAX_PLAYERS_IN_GUI  = MAX_PLAYERS - 1;
static constexpr auto PLAYER_FEATURE = MAX_PLAYERS + 1;

/// Max players plus 1 baba and 1 reserved for features
static constexpr auto MAX_PLAYER_SLOTS = MAX_PLAYERS + 2;

#if MAX_PLAYERS <= 8
typedef uint8_t PlayerMask;
#elif MAX_PLAYERS <= 16
typedef uint16_t PlayerMask;
#elif MAX_PLAYERS <= 32
typedef uint32_t PlayerMask;
#else
typedef uint64_t PlayerMask;
#endif

enum QUEUE_MODE
{
  /**
   * Sends a message on the game queue, which will get synchronised,
   * by sending a GAME_ message
   */
	ModeQueue,

  /**
   * Performs the action immediately. Must already have been
   * synchronised, for example by sending a GAME_ message
   */
	ModeImmediate
};


/**
 * Initialise the framework library
 * @param pWindowName the text to appear in the window title bar
 * @param width the display width
 * @param height the display height
 * @param bitDepth the display bit depth
 * @param fullScreen whether to start full screen or windowed
 * @param vsync if to sync to the vertical blanking interval or not
 *
 * @return `true` when the framework library is successfully initialised, `false`
 *   when a part of the initialisation failed
 */
bool frameInitialise();

/// Shut down the framework library
void frameShutDown();

/**
 * Call this each tick to allow the framework to deal with
 * windows messages, and do general house keeping
 */
void frameUpdate();

/// @return the current frame - used to establish whats on screen
unsigned frameGetFrameNumber();

/// @return the framerate of the last second
int frameRate();

static constexpr __attribute__((__warn_unused_result__)) std::string bool2string(bool var)
{
  return var ? "true" : "false";
}

enum class VIDEO_BACKEND
{
	opengl,
	opengles,
	vulkan,
#if defined(WZ_BACKEND_DIRECTX)
	directx,
#endif
	num_backends // Must be last!
};

std::optional<VIDEO_BACKEND> video_backend_from_str(const std::string& str);
std::string to_string(VIDEO_BACKEND backend);
std::string to_display_string(const VIDEO_BACKEND& backend);

enum class WINDOW_MODE : int
{
	desktop_fullscreen = -1,
	windowed = 0,
	fullscreen = 1
};

std::string to_display_string(const WINDOW_MODE& mode);

static constexpr auto MIN_VALID_WINDOW_MODE = WINDOW_MODE::desktop_fullscreen;
static constexpr auto MAX_VALID_WINDOW_MODE = WINDOW_MODE::fullscreen;

#endif
