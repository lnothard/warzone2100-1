/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2021  Warzone 2100 Project

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
#include "lib/framework/wzapp.h"

/* Standard library headers */
#include <physfs.h>
#include <string.h>

/* Warzone src and library headers */
#include "lib/framework/endian_hack.h"
#include "lib/framework/math_ext.h"
#include "lib/framework/wzconfig.h"
#include "lib/framework/file.h"
#include "lib/framework/physfs_ext.h"
#include "lib/framework/strres.h"
#include "lib/framework/frameresource.h"
#include "lib/framework/wztime.h"

#include <wzmaplib/map_terrain_types.h>

#include "lib/gamelib/gtime.h"
#include "lib/ivis_opengl/ivisdef.h"
#include "lib/ivis_opengl/pieblitfunc.h"
#include "lib/ivis_opengl/piestate.h"
#include "lib/ivis_opengl/piepalette.h"
#include "lib/ivis_opengl/textdraw.h"
#include "lib/netplay/netplay.h"
#include "lib/sound/audio.h"
#include "lib/sound/audio_id.h"
#include "modding.h"
#include "main.h"
#include "game.h"
#include "qtscript.h"
#include "fpath.h"
#include "difficulty.h"
#include "map.h"
#include "move.h"
#include "droid.h"
#include "order.h"
#include "group.h"
#include "action.h"
#include "research.h"
#include "power.h"
#include "projectile.h"
#include "loadsave.h"
#include "text.h"
#include "message.h"
#include "hci.h"
#include "display.h"
#include "display3d.h"
#include "map.h"
#include "effects.h"
#include "init.h"
#include "scores.h"
#include "design.h"
#include "component.h"
#include "radar.h"
#include "cmddroid.h"
#include "warzoneconfig.h"
#include "multiplay.h"
#include "frontend.h"
#include "levels.h"
#include "geometry.h"
#include "gateway.h"
#include "multistat.h"
#include "multiint.h"
#include "wrappers.h"
#include "combat.h"
#include "template.h"
#include "version.h"
#include "lib/ivis_opengl/screen.h"
#include <ctime>
#include "multimenu.h"
#include "console.h"
#include "wzscriptdebug.h"
#include "build_tools/autorevision.h"

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wcast-align"	// TODO: FIXME!
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wcast-align"	// TODO: FIXME!
#endif

#define UNUSED(x) (void)(x)

// Ignore unused functions for now (until full cleanup of old binary savegame .gam writing)
#if defined( _MSC_VER )
# pragma warning( disable : 4505 ) // warning C4505: unreferenced function with internal linkage has been removed
#endif
#if defined(__clang__)
# pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
# pragma GCC diagnostic ignored "-Wunused-function"
#endif

bool saveJSONToFile(const nlohmann::json& obj, const char* pFileName)
{
	std::ostringstream stream;
	stream << obj.dump(4) << std::endl;
	std::string jsonString = stream.str();
	debug(LOG_SAVE, "%s %s", "Saving", pFileName);
	return saveFile(pFileName, jsonString.c_str(), jsonString.size());
}

void gameScreenSizeDidChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth, unsigned int newHeight)
{
	intScreenSizeDidChange(oldWidth, oldHeight, newWidth, newHeight);
	loadSaveScreenSizeDidChange(oldWidth, oldHeight, newWidth, newHeight);
	multiMenuScreenSizeDidChange(oldWidth, oldHeight, newWidth, newHeight);
	display3dScreenSizeDidChange(oldWidth, oldHeight, newWidth, newHeight);
	consoleScreenDidChangeSize(oldWidth, oldHeight, newWidth, newHeight);
	frontendScreenSizeDidChange(oldWidth, oldHeight, newWidth, newHeight);
	widgOverlaysScreenSizeDidChange(oldWidth, oldHeight, newWidth, newHeight); // must be last!
}

void gameDisplayScaleFactorDidChange(float newDisplayScaleFactor)
{
	// The text subsystem requires the game -> renderer scale factor, which potentially differs from
	// the display scale factor.
	float horizGameToRendererScaleFactor = 0.f, vertGameToRendererScaleFactor = 0.f;
	wzGetGameToRendererScaleFactor(&horizGameToRendererScaleFactor, &vertGameToRendererScaleFactor);
	iV_TextUpdateScaleFactor(horizGameToRendererScaleFactor, vertGameToRendererScaleFactor);
}

static unsigned int currentGameVersion = 410;

#define MAX_SAVE_NAME_SIZE_V19	40
#define MAX_SAVE_NAME_SIZE	60

static const UDWORD NULL_ID = UDWORD_MAX;
#define SAVEKEY_ONMISSION	0x100
static UDWORD RemapPlayerNumber(UDWORD OldNumber);
bool writeGameInfo(const char *pFileName);
/** struct used to store the data for retreating. */
struct RUN_DATA
{
	Vector2i sPos = Vector2i(0, 0); ///< position to where units should flee to.
	uint8_t forceLevel = 0;  ///< number of units below which others might flee.
	uint8_t healthLevel = 0; ///< health percentage value below which it might flee. This value is used for groups only.
	uint8_t leadership = 0;  ///< basic value that will be used on calculations of the flee probability.
};

// return positions for vtols, at one time.
Vector2i asVTOLReturnPos[MAX_PLAYERS];

struct GAME_SAVEHEADER
{
	char        aFileType[4];
	uint32_t    version;
};

static bool serializeSaveGameHeader(PHYSFS_file *fileHandle, const GAME_SAVEHEADER *serializeHeader)
{
	if (WZ_PHYSFS_writeBytes(fileHandle, serializeHeader->aFileType, 4) != 4)
	{
		return false;
	}

	// Write version numbers below version 35 as little-endian, and those above as big-endian
	if (serializeHeader->version < VERSION_35)
	{
		return PHYSFS_writeULE32(fileHandle, serializeHeader->version);
	}
	else
	{
		return PHYSFS_writeUBE32(fileHandle, serializeHeader->version);
	}
}

static bool deserializeSaveGameHeader(PHYSFS_file *fileHandle, GAME_SAVEHEADER *serializeHeader)
{
	// Read in the header from the file
	if (WZ_PHYSFS_readBytes(fileHandle, serializeHeader->aFileType, 4) != 4
	    || WZ_PHYSFS_readBytes(fileHandle, &serializeHeader->version, sizeof(uint32_t)) != sizeof(uint32_t))
	{
		return false;
	}

	// All save game file versions below version 35 (i.e. _not_ version 35 itself)
	// have their version numbers stored as little endian. Versions from 35 and
	// onward use big-endian. This basically means that, because of endian
	// swapping, numbers from 35 and onward will be ridiculously high if a
	// little-endian byte-order is assumed.

	// Convert from little endian to native byte-order and check if we get a
	// ridiculously high number
	endian_udword(&serializeHeader->version);

	if (serializeHeader->version <= VERSION_34)
	{
		// Apparently we don't get a ridiculously high number if we assume
		// little-endian, so lets assume our version number is 34 at max and return
		debug(LOG_SAVE, "Version = %u (little-endian)", serializeHeader->version);

		return true;
	}
	else
	{
		// Apparently we get a larger number than expected if using little-endian.
		// So assume we have a version of 35 and onward

		// Reverse the little-endian decoding
		endian_udword(&serializeHeader->version);
	}

	// Considering that little-endian didn't work we now use big-endian instead
	serializeHeader->version = PHYSFS_swapUBE32(serializeHeader->version);
	debug(LOG_SAVE, "Version %u = (big-endian)", serializeHeader->version);

	return true;
}

struct STRUCT_SAVEHEADER : public GAME_SAVEHEADER
{
	UDWORD		quantity;
};

struct FEATURE_SAVEHEADER : public GAME_SAVEHEADER
{
	UDWORD		quantity;
};

/* Structure definitions for loading and saving map data */
struct TILETYPE_SAVEHEADER : public GAME_SAVEHEADER
{
	UDWORD quantity;
};

/* Sanity check definitions for the save struct file sizes */
#define DROIDINIT_HEADER_SIZE		12
#define STRUCT_HEADER_SIZE			12
#define FEATURE_HEADER_SIZE			12
#define TILETYPE_HEADER_SIZE		12

// general save definitions
#define MAX_LEVEL_SIZE 20

#define OBJECT_SAVE_V19 \
	char				name[MAX_SAVE_NAME_SIZE_V19]; \
	UDWORD				id; \
	UDWORD				x,y,z; \
	UDWORD				direction; \
	UDWORD				player; \
	int32_t				inFire; \
	UDWORD				periodicalDamageStart; \
	UDWORD				periodicalDamage

#define OBJECT_SAVE_V20 \
	char				name[MAX_SAVE_NAME_SIZE]; \
	UDWORD				id; \
	UDWORD				x,y,z; \
	UDWORD				direction; \
	UDWORD				player; \
	int32_t		inFire; \
	UDWORD				periodicalDamageStart; \
	UDWORD				periodicalDamage

struct SAVE_POWER
{
	uint32_t    currentPower;
	uint32_t    extractedPower; // used for hacks
};
static void serializeSavePowerData_json(nlohmann::json &o, const SAVE_POWER *serializePower)
{
	o["currentPower"] = serializePower->currentPower;
	o["extractedPower"] = serializePower->extractedPower;
}
static bool serializeSavePowerData(PHYSFS_file *fileHandle, const SAVE_POWER *serializePower)
{
	return (PHYSFS_writeUBE32(fileHandle, serializePower->currentPower)
	        && PHYSFS_writeUBE32(fileHandle, serializePower->extractedPower));
}
static void deserializeSavePowerData_json(const nlohmann::json &o, SAVE_POWER *serializePower)
{
	serializePower->currentPower = o.at("currentPower").get<uint32_t>();
	serializePower->extractedPower = o.at("extractedPower").get<uint32_t>();
}
static bool deserializeSavePowerData(PHYSFS_file *fileHandle, SAVE_POWER *serializePower)
{
	return (PHYSFS_readUBE32(fileHandle, &serializePower->currentPower)
	        && PHYSFS_readUBE32(fileHandle, &serializePower->extractedPower));
}
static void serializeVector3i_json(nlohmann::json &o, const Vector3i *serializeVector)
{
	o["x"] = serializeVector->x;
	o["y"] = serializeVector->y;
	o["z"] = serializeVector->z;
}
static bool serializeVector3i(PHYSFS_file *fileHandle, const Vector3i *serializeVector)
{
	return (PHYSFS_writeSBE32(fileHandle, serializeVector->x)
	        && PHYSFS_writeSBE32(fileHandle, serializeVector->y)
	        && PHYSFS_writeSBE32(fileHandle, serializeVector->z));
}

static void deserializeVector3i_json(const nlohmann::json &o, Vector3i *serializeVector)
{
	serializeVector->x = o.at("x").get<int32_t>();
	serializeVector->y = o.at("y").get<int32_t>();
	serializeVector->z = o.at("z").get<int32_t>();
}

static bool deserializeVector3i(PHYSFS_file *fileHandle, Vector3i *serializeVector)
{
	int32_t x, y, z;

	if (!PHYSFS_readSBE32(fileHandle, &x)
	    || !PHYSFS_readSBE32(fileHandle, &y)
	    || !PHYSFS_readSBE32(fileHandle, &z))
	{
		return false;
	}

	serializeVector-> x = x;
	serializeVector-> y = y;
	serializeVector-> z = z;

	return true;
}
static void serializeVector2i_json(nlohmann::json &o,  const Vector2i *serializeVector)
{
	o["x"] = serializeVector->x;
	o["y"] = serializeVector->y;
}

static bool serializeVector2i(PHYSFS_file *fileHandle, const Vector2i *serializeVector)
{
	return (PHYSFS_writeSBE32(fileHandle, serializeVector->x)
	        && PHYSFS_writeSBE32(fileHandle, serializeVector->y));
}

static void deserializeVector2i_json(const nlohmann::json &o, Vector2i *serializeVector)
{
	serializeVector->x = o.at("x").get<int32_t>();
	serializeVector->y = o.at("y").get<int32_t>();
}

static bool deserializeVector2i(PHYSFS_file *fileHandle, Vector2i *serializeVector)
{
	int32_t x, y;

	if (!PHYSFS_readSBE32(fileHandle, &x)
	    || !PHYSFS_readSBE32(fileHandle, &y))
	{
		return false;
	}

	serializeVector-> x = x;
	serializeVector-> y = y;

	return true;
}
static void serializeiViewData_json(nlohmann::json &o, const iView *serializeView)
{
	auto viewP = nlohmann::json::object();
	serializeVector3i_json(viewP, &serializeView->p);
	o["viewDataP"] = viewP;
	auto viewR = nlohmann::json::object();
	serializeVector3i_json(viewR, &serializeView->r);
	o["viewDataR"] = viewR;
}

static bool serializeiViewData(PHYSFS_file *fileHandle, const iView *serializeView)
{
	return (serializeVector3i(fileHandle, &serializeView->p)
	        && serializeVector3i(fileHandle, &serializeView->r));
}

static void deserializeiViewData_json(const nlohmann::json &o, iView *serializeView)
{
	deserializeVector3i_json(o.at("viewDataP"), &serializeView->p);
	deserializeVector3i_json(o.at("viewDataR"), &serializeView->r);
}

static bool deserializeiViewData(PHYSFS_file *fileHandle, iView *serializeView)
{
	return (deserializeVector3i(fileHandle, &serializeView->p)
	        && deserializeVector3i(fileHandle, &serializeView->r));
}
static void serializeRunData_json(nlohmann::json &o, const RUN_DATA *serializeRun)
{
	serializeVector2i_json(o, &serializeRun->sPos);
	o["forceLevel"] = serializeRun->forceLevel;
	o["healthLevel"] = serializeRun->healthLevel;
	o["leadership"] = serializeRun->leadership;
}
static bool serializeRunData(PHYSFS_file *fileHandle, const RUN_DATA *serializeRun)
{
	return (serializeVector2i(fileHandle, &serializeRun->sPos)
	        && PHYSFS_writeUBE8(fileHandle, serializeRun->forceLevel)
	        && PHYSFS_writeUBE8(fileHandle, serializeRun->healthLevel)
	        && PHYSFS_writeUBE8(fileHandle, serializeRun->leadership));
}
static void deserializeRunData_json(const nlohmann::json &o, RUN_DATA *serializeRun)
{
	deserializeVector2i_json(o, &serializeRun->sPos);
	serializeRun->forceLevel = o.at("forceLevel").get<uint8_t>();
	serializeRun->healthLevel = o.at("healthLevel").get<uint8_t>();
	serializeRun->leadership = o.at("leadership").get<uint8_t>();
}
static bool deserializeRunData(PHYSFS_file *fileHandle, RUN_DATA *serializeRun)
{
	return (deserializeVector2i(fileHandle, &serializeRun->sPos)
	        && PHYSFS_readUBE8(fileHandle, &serializeRun->forceLevel)
	        && PHYSFS_readUBE8(fileHandle, &serializeRun->healthLevel)
	        && PHYSFS_readUBE8(fileHandle, &serializeRun->leadership));
}

static void serializeMultiplayerGame_json(nlohmann::json &o, const MULTIPLAYERGAME *serializeMulti)
{
	o["multiType"] = serializeMulti->type;
	o["multiMapName"] = serializeMulti->map;
	o["multiMaxPlayers"] = serializeMulti->maxPlayers;
	o["multiGameName"] = serializeMulti->name;
	o["multiPower"] = serializeMulti->power;
	o["multiBase"] = serializeMulti->base;
	o["multiAlliance"] = serializeMulti->alliance;
	o["multiHashBytes"] = 32; // serializeMulti->hash.Bytes
	o["multiHash"] = serializeMulti->hash.toString();
	// skip more dummy

}
static bool serializeMultiplayerGame(PHYSFS_file *fileHandle, const MULTIPLAYERGAME *serializeMulti)
{
	const char *dummy8c = "DUMMYSTRING";

	if (!PHYSFS_writeUBE8(fileHandle, static_cast<uint8_t>(serializeMulti->type))
	    || WZ_PHYSFS_writeBytes(fileHandle, serializeMulti->map, 128) != 128
	    || WZ_PHYSFS_writeBytes(fileHandle, dummy8c, 8) != 8
	    || !PHYSFS_writeUBE8(fileHandle, serializeMulti->maxPlayers)
	    || WZ_PHYSFS_writeBytes(fileHandle, serializeMulti->name, 128) != 128
	    || !PHYSFS_writeSBE32(fileHandle, 0)
	    || !PHYSFS_writeUBE32(fileHandle, serializeMulti->power)
	    || !PHYSFS_writeUBE8(fileHandle, serializeMulti->base)
	    || !PHYSFS_writeUBE8(fileHandle, serializeMulti->alliance)
	    || !PHYSFS_writeUBE8(fileHandle, serializeMulti->hash.Bytes)
	    || !WZ_PHYSFS_writeBytes(fileHandle, serializeMulti->hash.bytes, serializeMulti->hash.Bytes)
	    || !PHYSFS_writeUBE16(fileHandle, 0)	// dummy, was bytesPerSec
	    || !PHYSFS_writeUBE8(fileHandle, 0)	// dummy, was packetsPerSec
	    || !PHYSFS_writeUBE8(fileHandle, false))	// reuse available field, was encryptKey
	{
		return false;
	}

	for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
	{
		// dummy, was `skDiff` for each player
		if (!PHYSFS_writeUBE8(fileHandle, 0))
		{
			return false;
		}
	}

	return true;
}
static void deserializeMultiplayerGame_json(const nlohmann::json &o, MULTIPLAYERGAME *serializeMulti)
{
	serializeMulti->type = static_cast<LEVEL_TYPE>(o.at("multiType").get<uint8_t>());
	sstrcpy(serializeMulti->map,  o.at("multiMapName").get<std::string>().c_str());
	serializeMulti->maxPlayers = o.at("multiMaxPlayers").get<uint8_t>();
	sstrcpy(serializeMulti->name, o.at("multiGameName").get<std::string>().c_str());
	serializeMulti->power = o.at("multiPower").get<uint32_t>();
	serializeMulti->base = o.at("multiBase").get<uint8_t>();
	serializeMulti->alliance = o.at("multiAlliance").get<uint8_t>();
	Sha256 sha256;
	sha256.fromString(o.at("multiHash").get<std::string>());
	serializeMulti->hash = sha256;
}
static bool deserializeMultiplayerGame(PHYSFS_file *fileHandle, MULTIPLAYERGAME *serializeMulti)
{
	int32_t boolFog;
	uint8_t dummy8;
	uint16_t dummy16;
	char dummy8c[8];
	uint8_t hashSize;

	serializeMulti->hash.setZero();

	if (!PHYSFS_readUBE8(fileHandle, reinterpret_cast<uint8_t*>(&serializeMulti->type))
	    || WZ_PHYSFS_readBytes(fileHandle, serializeMulti->map, 128) != 128
	    || WZ_PHYSFS_readBytes(fileHandle, dummy8c, 8) != 8
	    || !PHYSFS_readUBE8(fileHandle, &serializeMulti->maxPlayers)
	    || WZ_PHYSFS_readBytes(fileHandle, serializeMulti->name, 128) != 128
	    || !PHYSFS_readSBE32(fileHandle, &boolFog)
	    || !PHYSFS_readUBE32(fileHandle, &serializeMulti->power)
	    || !PHYSFS_readUBE8(fileHandle, &serializeMulti->base)
	    || !PHYSFS_readUBE8(fileHandle, &serializeMulti->alliance)
	    || !PHYSFS_readUBE8(fileHandle, &hashSize)
	    || (hashSize == serializeMulti->hash.Bytes && !WZ_PHYSFS_readBytes(fileHandle, serializeMulti->hash.bytes, serializeMulti->hash.Bytes))
	    || !PHYSFS_readUBE16(fileHandle, &dummy16)	// dummy, was bytesPerSec
	    || !PHYSFS_readUBE8(fileHandle, &dummy8)	// dummy, was packetsPerSec
	    || !PHYSFS_readUBE8(fileHandle, &dummy8))	// reused for challenge, was encryptKey
	{
		return false;
	}

	for (unsigned int i = 0; i < MAX_PLAYERS; ++i)
	{
		// dummy, was `skDiff` for each player
		if (!PHYSFS_readUBE8(fileHandle, &dummy8))
		{
			return false;
		}
	}

	return true;
}
static void serializePlayer_json(nlohmann::json &o, const PLAYER *serializePlayer, int player)
{
	o["position"] = serializePlayer->position;
	o["name"] = serializePlayer->name;
	o["aiName"] = getAIName(player);
	o["difficulty"] = static_cast<int8_t>(serializePlayer->difficulty);
	o["allocated"] = (uint8_t)serializePlayer->allocated;
	o["colour"] = serializePlayer->colour;
	o["team"] = serializePlayer->team;
}

static bool serializePlayer(PHYSFS_file *fileHandle, const PLAYER *serializePlayer, int player)
{
	return (PHYSFS_writeUBE32(fileHandle, serializePlayer->position)
	        && WZ_PHYSFS_writeBytes(fileHandle, serializePlayer->name, StringSize) == StringSize
	        && WZ_PHYSFS_writeBytes(fileHandle, getAIName(player), MAX_LEN_AI_NAME) == MAX_LEN_AI_NAME
	        && PHYSFS_writeSBE8(fileHandle, static_cast<int8_t>(serializePlayer->difficulty))
	        && PHYSFS_writeUBE8(fileHandle, (uint8_t)serializePlayer->allocated)
	        && PHYSFS_writeUBE32(fileHandle, serializePlayer->colour)
	        && PHYSFS_writeUBE32(fileHandle, serializePlayer->team));
}
static void deserializePlayer_json(const nlohmann::json &o, PLAYER *serializePlayer, int player)
{
	char aiName[MAX_LEN_AI_NAME] = { "THEREISNOAI" };
	ASSERT(o.is_object(), "unexpected type, wanted object");
	sstrcpy(serializePlayer->name, o.at("name").get<std::string>().c_str());
	sstrcpy(aiName, o.at("aiName").get<std::string>().c_str());
	serializePlayer->difficulty = static_cast<AIDifficulty>(o.at("difficulty").get<int8_t>());
	serializePlayer->allocated = o.at("allocated").get<uint8_t>();
	if (player < game.maxPlayers)
	{
		serializePlayer->ai = matchAIbyName(aiName);
		ASSERT(serializePlayer->ai != AI_NOT_FOUND, "AI \"%s\" not found -- script loading will fail (player %d / %d)",
				aiName, player, game.maxPlayers);
	}
	serializePlayer->position = o.at("position").get<uint32_t>();
	serializePlayer->colour = o.at("colour").get<uint32_t>();
	serializePlayer->team = o.at("team").get<uint32_t>();
}
static bool deserializePlayer(PHYSFS_file *fileHandle, PLAYER *serializePlayer, int player)
{
	char aiName[MAX_LEN_AI_NAME] = { "THEREISNOAI" };
	uint32_t position = 0, colour = 0, team = 0;
	bool retval = false;
	uint8_t allocated = 0;

	retval = (PHYSFS_readUBE32(fileHandle, &position)
	          && WZ_PHYSFS_readBytes(fileHandle, serializePlayer->name, StringSize) == StringSize
	          && WZ_PHYSFS_readBytes(fileHandle, aiName, MAX_LEN_AI_NAME) == MAX_LEN_AI_NAME
	          && PHYSFS_readSBE8(fileHandle, reinterpret_cast<int8_t*>(&serializePlayer->difficulty))
	          && PHYSFS_readUBE8(fileHandle, &allocated)
	          && PHYSFS_readUBE32(fileHandle, &colour)
	          && PHYSFS_readUBE32(fileHandle, &team));

	serializePlayer->allocated = allocated;
	if (player < game.maxPlayers)
	{
		serializePlayer->ai = matchAIbyName(aiName);
		ASSERT(serializePlayer->ai != AI_NOT_FOUND, "AI \"%s\" not found -- script loading will fail (player %d / %d)",
		       aiName, player, game.maxPlayers);
	}
	serializePlayer->position = position;
	serializePlayer->colour = colour;
	serializePlayer->team = team;
	return retval;
}
static void serializeNetPlay_json(nlohmann::json &o, const NETPLAY *serializeNetPlay)
{
	auto arr = nlohmann::json::array();
	for (unsigned i = 0; i < MAX_PLAYERS; ++i)
	{
		auto tmp = nlohmann::json::object();
		serializePlayer_json(tmp, &serializeNetPlay->players[i], i);
		arr.insert(arr.end(), tmp);
	}
	o["netbComms"] = serializeNetPlay->bComms;
	o["netPlayerCount"] = serializeNetPlay->playercount;
	o["netHostPlayer"] = serializeNetPlay->hostPlayer;
	o["netSelectedPlayer"] = selectedPlayer;
	o["netScavengers"] = game.scavengers;
	// skip dummy
	o["netPlayers"] = arr;
}
static bool serializeNetPlay(PHYSFS_file *fileHandle, const NETPLAY *serializeNetPlay)
{
	for (unsigned i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!serializePlayer(fileHandle, &serializeNetPlay->players[i], i))
		{
			return false;
		}
	}

	return (PHYSFS_writeUBE32(fileHandle, (uint32_t)serializeNetPlay->bComms)
	        && PHYSFS_writeUBE32(fileHandle, serializeNetPlay->playercount)
	        && PHYSFS_writeUBE32(fileHandle, serializeNetPlay->hostPlayer)
	        && PHYSFS_writeUBE32(fileHandle, selectedPlayer)
	        && PHYSFS_writeUBE32(fileHandle, (uint32_t)game.scavengers)
	        && PHYSFS_writeUBE32(fileHandle, 0)
	        && PHYSFS_writeUBE32(fileHandle, 0));
}

static void deserializeNetPlay_json(const nlohmann::json &o, NETPLAY *serializeNetPlay)
{
	const auto players = o.at("netPlayers");
	ASSERT_OR_RETURN(, players.is_array(), "unexpected type, wanted array");
	for (unsigned i = 0; i < MAX_PLAYERS; ++i)
	{
		deserializePlayer_json(players.at(i), &serializeNetPlay->players[i], i);
	}
	serializeNetPlay->isHost = true; // only host can load
	serializeNetPlay->playercount = o.at("netPlayerCount").get<uint32_t>();
	serializeNetPlay->bComms = o.at("netbComms").get<bool>();
	selectedPlayer = o.at("netSelectedPlayer").get<uint32_t>();
	game.scavengers = o.at("netScavengers").get<uint8_t>();
}
static bool deserializeNetPlay(PHYSFS_file *fileHandle, NETPLAY *serializeNetPlay)
{
	unsigned int i;
	bool retv;

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!deserializePlayer(fileHandle, &serializeNetPlay->players[i], i))
		{
			return false;
		}
	}

	uint32_t dummy, bComms = serializeNetPlay->bComms, scavs = game.scavengers;

	serializeNetPlay->isHost = true;	// only host can load
	retv = (PHYSFS_readUBE32(fileHandle, &bComms)
	        && PHYSFS_readUBE32(fileHandle, &serializeNetPlay->playercount)
	        && PHYSFS_readUBE32(fileHandle, &serializeNetPlay->hostPlayer)
	        && PHYSFS_readUBE32(fileHandle, &selectedPlayer)
	        && PHYSFS_readUBE32(fileHandle, &scavs)
	        && PHYSFS_readUBE32(fileHandle, &dummy)
	        && PHYSFS_readUBE32(fileHandle, &dummy));
	serializeNetPlay->bComms = bComms;
	game.scavengers = scavs;
	return retv;
}

struct SAVE_GAME_V7
{
	uint32_t    gameTime;
	uint32_t    GameType;                   /* Type of game , one of the GTYPE_... enums. */
	int32_t     ScrollMinX;                 /* Scroll Limits */
	int32_t     ScrollMinY;
	uint32_t    ScrollMaxX;
	uint32_t    ScrollMaxY;
	char        levelName[MAX_LEVEL_SIZE];  //name of the level to load up when mid game
};

static void serializeSaveGameV7Data_json(nlohmann::json &o, const SAVE_GAME_V7 *serializeGame)
{
	o["gameTime"] = serializeGame->gameTime;
	o["GameType"] = serializeGame->GameType;
	o["ScrollMinX"] = serializeGame->ScrollMinX;
	o["ScrollMinY"] = serializeGame->ScrollMinY;
	o["ScrollMaxX"] = serializeGame->ScrollMaxX;
	o["ScrollMaxY"] = serializeGame->ScrollMaxY;
	o["levelName"] = serializeGame->levelName;
}

static bool serializeSaveGameV7Data(PHYSFS_file *fileHandle, const SAVE_GAME_V7 *serializeGame)
{
	return (PHYSFS_writeUBE32(fileHandle, serializeGame->gameTime)
	        && PHYSFS_writeUBE32(fileHandle, serializeGame->GameType)
	        && PHYSFS_writeSBE32(fileHandle, serializeGame->ScrollMinX)
	        && PHYSFS_writeSBE32(fileHandle, serializeGame->ScrollMinY)
	        && PHYSFS_writeUBE32(fileHandle, serializeGame->ScrollMaxX)
	        && PHYSFS_writeUBE32(fileHandle, serializeGame->ScrollMaxY)
	        && WZ_PHYSFS_writeBytes(fileHandle, serializeGame->levelName, MAX_LEVEL_SIZE) == MAX_LEVEL_SIZE);
}
static void deserializeSaveGameV7Data_json(const nlohmann::json &o, SAVE_GAME_V7 *serializeGame)
{
	serializeGame->gameTime = o.at("gameTime").get<uint32_t>();
	serializeGame->GameType = o.at("GameType").get<uint32_t>();
	serializeGame->ScrollMinX = o.at("ScrollMinX").get<int32_t>();
	serializeGame->ScrollMinY = o.at("ScrollMinY").get<int32_t>();
	serializeGame->ScrollMaxX = o.at("ScrollMaxX").get<uint32_t>();
	serializeGame->ScrollMaxY = o.at("ScrollMaxY").get<uint32_t>();
	sstrcpy(serializeGame->levelName, o.at("levelName").get<std::string>().c_str());
}

static bool deserializeSaveGameV7Data(PHYSFS_file *fileHandle, SAVE_GAME_V7 *serializeGame)
{
	return (PHYSFS_readUBE32(fileHandle, &serializeGame->gameTime)
	        && PHYSFS_readUBE32(fileHandle, &serializeGame->GameType)
	        && PHYSFS_readSBE32(fileHandle, &serializeGame->ScrollMinX)
	        && PHYSFS_readSBE32(fileHandle, &serializeGame->ScrollMinY)
	        && PHYSFS_readUBE32(fileHandle, &serializeGame->ScrollMaxX)
	        && PHYSFS_readUBE32(fileHandle, &serializeGame->ScrollMaxY)
	        && WZ_PHYSFS_readBytes(fileHandle, serializeGame->levelName, MAX_LEVEL_SIZE) == MAX_LEVEL_SIZE);
}

struct SAVE_GAME_V10 : public SAVE_GAME_V7
{
	SAVE_POWER  power[MAX_PLAYERS];
};

static void serializeSaveGameV10Data_json(nlohmann::json &o, const SAVE_GAME_V10 *serializeGame)
{
	serializeSaveGameV7Data_json(o, (const SAVE_GAME_V7 *) serializeGame);
	auto arr = nlohmann::json::array();
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		auto tmp = nlohmann::json::object();
		serializeSavePowerData_json(tmp, &serializeGame->power[i]);
		arr.insert(arr.end(), tmp);
	}
	o["power"] = arr;
}
static bool serializeSaveGameV10Data(PHYSFS_file *fileHandle, const SAVE_GAME_V10 *serializeGame)
{
	unsigned int i;

	if (!serializeSaveGameV7Data(fileHandle, (const SAVE_GAME_V7 *) serializeGame))
	{
		return false;
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!serializeSavePowerData(fileHandle, &serializeGame->power[i]))
		{
			return false;
		}
	}

	return true;
}
static void deserializeSaveGameV10Data_json(const nlohmann::json &o, SAVE_GAME_V10 *serializeGame)
{
	deserializeSaveGameV7Data_json(o,  (SAVE_GAME_V7 *) serializeGame);
	nlohmann::json power= o.at("power");
	ASSERT(power.is_array(), "unexpected type");
	for (unsigned i = 0; i < MAX_PLAYERS; ++i)
	{
		deserializeSavePowerData_json(power.at(i), &serializeGame->power[i]);
	}
}
static bool deserializeSaveGameV10Data(PHYSFS_file *fileHandle, SAVE_GAME_V10 *serializeGame)
{
	unsigned int i;

	if (!deserializeSaveGameV7Data(fileHandle, (SAVE_GAME_V7 *) serializeGame))
	{
		return false;
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!deserializeSavePowerData(fileHandle, &serializeGame->power[i]))
		{
			return false;
		}
	}

	return true;
}

struct SAVE_GAME_V11 : public SAVE_GAME_V10
{
	iView currentPlayerPos;
};

static void serializeSaveGameV11Data_json(nlohmann::json &o, const SAVE_GAME_V11 *serializeGame)
{
	serializeSaveGameV10Data_json(o, (const SAVE_GAME_V10 *) serializeGame);
	serializeiViewData_json(o, &serializeGame->currentPlayerPos);

}

static bool serializeSaveGameV11Data(PHYSFS_file *fileHandle, const SAVE_GAME_V11 *serializeGame)
{
	return (serializeSaveGameV10Data(fileHandle, (const SAVE_GAME_V10 *) serializeGame)
	        && serializeiViewData(fileHandle, &serializeGame->currentPlayerPos));
}
static void deserializeSaveGameV11Data_json(const nlohmann::json &o, SAVE_GAME_V11 *serializeGame)
{
	deserializeSaveGameV10Data_json(o, (SAVE_GAME_V10 *) serializeGame);
	deserializeiViewData_json(o, &serializeGame->currentPlayerPos);
}
static bool deserializeSaveGameV11Data(PHYSFS_file *fileHandle, SAVE_GAME_V11 *serializeGame)
{
	return (deserializeSaveGameV10Data(fileHandle, (SAVE_GAME_V10 *) serializeGame)
	        && deserializeiViewData(fileHandle, &serializeGame->currentPlayerPos));
}

struct SAVE_GAME_V12 : public SAVE_GAME_V11
{
	uint32_t    missionTime;
	uint32_t    saveKey;
};
static void serializeSaveGameV12Data_json(nlohmann::json &o, const SAVE_GAME_V12 *serializeGame)
{
	serializeSaveGameV11Data_json(o,  (const SAVE_GAME_V11 *) serializeGame);
	o["missionTime"] = serializeGame->missionTime;
	o["saveKey"] = serializeGame->saveKey;
}
static bool serializeSaveGameV12Data(PHYSFS_file *fileHandle, const SAVE_GAME_V12 *serializeGame)
{
	return (serializeSaveGameV11Data(fileHandle, (const SAVE_GAME_V11 *) serializeGame)
	        && PHYSFS_writeUBE32(fileHandle, serializeGame->missionTime)
	        && PHYSFS_writeUBE32(fileHandle, serializeGame->saveKey));
}

static void deserializeSaveGameV12Data_json(const nlohmann::json &o, SAVE_GAME_V12 *serializeGame)
{
	deserializeSaveGameV11Data_json(o, (SAVE_GAME_V11 *) serializeGame);
	serializeGame->missionTime = o.at("missionTime").get<uint32_t>();
	serializeGame->saveKey = o.at("saveKey").get<uint32_t>();
}

static bool deserializeSaveGameV12Data(PHYSFS_file *fileHandle, SAVE_GAME_V12 *serializeGame)
{
	return (deserializeSaveGameV11Data(fileHandle, (SAVE_GAME_V11 *) serializeGame)
	        && PHYSFS_readUBE32(fileHandle, &serializeGame->missionTime)
	        && PHYSFS_readUBE32(fileHandle, &serializeGame->saveKey));
}

struct SAVE_GAME_V14 : public SAVE_GAME_V12
{
	int32_t     missionOffTime;
	int32_t     missionETA;
	uint16_t    missionHomeLZ_X;
	uint16_t    missionHomeLZ_Y;
	int32_t     missionPlayerX;
	int32_t     missionPlayerY;
	uint16_t    iTranspEntryTileX[MAX_PLAYERS];
	uint16_t    iTranspEntryTileY[MAX_PLAYERS];
	uint16_t    iTranspExitTileX[MAX_PLAYERS];
	uint16_t    iTranspExitTileY[MAX_PLAYERS];
	uint32_t    aDefaultSensor[MAX_PLAYERS];
	uint32_t    aDefaultECM[MAX_PLAYERS];
	uint32_t    aDefaultRepair[MAX_PLAYERS];
};
static void serializeSaveGameV14Data_json(nlohmann::json &o, const SAVE_GAME_V14 *serializeGame)
{
	serializeSaveGameV12Data_json(o, (const SAVE_GAME_V12 *) serializeGame);
	o["missionOffTime"] = serializeGame->missionOffTime;
	o["missionETA"] = serializeGame->missionETA;
	o["missionHomeLZ_X"] = serializeGame->missionHomeLZ_X;
	o["missionHomeLZ_Y"] = serializeGame->missionHomeLZ_Y;
	o["missionPlayerX"] = serializeGame->missionPlayerX;
	o["missionPlayerY"] = serializeGame->missionPlayerY;
	auto arr = nlohmann::json::array();
	for (unsigned i = 0; i < MAX_PLAYERS; ++i)
	{
		auto tmp = nlohmann::json::object();
		tmp["iTranspEntryTileX"] =  serializeGame->iTranspEntryTileX[i];
		tmp["iTranspEntryTileY"] =  serializeGame->iTranspEntryTileY[i];
		tmp["iTranspExitTileX"] = serializeGame->iTranspExitTileX[i];
		tmp["iTranspExitTileY"] = serializeGame->iTranspExitTileY[i];
		tmp["aDefaultSensor"] = serializeGame->aDefaultSensor[i];
		tmp["aDefaultECM"] = serializeGame->aDefaultECM[i];
		tmp["aDefaultRepair"] = serializeGame->aDefaultRepair[i];
		arr.insert(arr.end(), tmp);
	}
	o["data"] = arr;
}
static bool serializeSaveGameV14Data(PHYSFS_file *fileHandle, const SAVE_GAME_V14 *serializeGame)
{
	unsigned int i;

	if (!serializeSaveGameV12Data(fileHandle, (const SAVE_GAME_V12 *) serializeGame)
	    || !PHYSFS_writeSBE32(fileHandle, serializeGame->missionOffTime)
	    || !PHYSFS_writeSBE32(fileHandle, serializeGame->missionETA)
	    || !PHYSFS_writeUBE16(fileHandle, serializeGame->missionHomeLZ_X)
	    || !PHYSFS_writeUBE16(fileHandle, serializeGame->missionHomeLZ_Y)
	    || !PHYSFS_writeSBE32(fileHandle, serializeGame->missionPlayerX)
	    || !PHYSFS_writeSBE32(fileHandle, serializeGame->missionPlayerY))
	{
		return false;
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_writeUBE16(fileHandle, serializeGame->iTranspEntryTileX[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_writeUBE16(fileHandle, serializeGame->iTranspEntryTileY[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_writeUBE16(fileHandle, serializeGame->iTranspExitTileX[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_writeUBE16(fileHandle, serializeGame->iTranspExitTileY[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_writeUBE32(fileHandle, serializeGame->aDefaultSensor[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_writeUBE32(fileHandle, serializeGame->aDefaultECM[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_writeUBE32(fileHandle, serializeGame->aDefaultRepair[i]))
		{
			return false;
		}
	}

	return true;
}
static void deserializeSaveGameV14Data_json(const nlohmann::json &o, SAVE_GAME_V14 *serializeGame)
{
	deserializeSaveGameV12Data_json(o, (SAVE_GAME_V12 *) serializeGame);
	 serializeGame->missionOffTime = o.at("missionOffTime").get<int32_t>();
	 serializeGame->missionETA = o.at("missionETA").get<int32_t>();
	 serializeGame->missionHomeLZ_X = o.at("missionHomeLZ_X").get<uint32_t>();
	 serializeGame->missionHomeLZ_Y = o.at("missionHomeLZ_Y").get<uint32_t>();
	 serializeGame->missionPlayerX = o.at("missionPlayerX").get<int32_t>();
	 serializeGame->missionPlayerY = o.at("missionPlayerY").get<int32_t>();
	nlohmann::json arr = o.at("data");
	ASSERT_OR_RETURN(, arr.is_array(), "unexpected type, wanted array");
	for (unsigned i = 0; i < MAX_PLAYERS; ++i)
	{
		serializeGame->iTranspEntryTileX[i] = o.at("data").at(i).at("iTranspEntryTileX").get<uint16_t>();
		serializeGame->iTranspEntryTileY[i] = o.at("data").at(i).at("iTranspEntryTileY").get<uint16_t>();
		serializeGame->iTranspExitTileX[i] = o.at("data").at(i).at("iTranspExitTileX").get<uint16_t>();
		serializeGame->iTranspExitTileY[i] = o.at("data").at(i).at("iTranspExitTileY").get<uint16_t>();
		serializeGame->aDefaultSensor[i] = o.at("data").at(i).at("aDefaultSensor").get<uint32_t>();
		serializeGame->aDefaultECM[i] = o.at("data").at(i).at("aDefaultECM").get<uint32_t>();
		serializeGame->aDefaultRepair[i] = o.at("data").at(i).at("aDefaultRepair").get<uint32_t>();
	}
}
static bool deserializeSaveGameV14Data(PHYSFS_file *fileHandle, SAVE_GAME_V14 *serializeGame)
{
	unsigned int i;

	if (!deserializeSaveGameV12Data(fileHandle, (SAVE_GAME_V12 *) serializeGame)
	    || !PHYSFS_readSBE32(fileHandle, &serializeGame->missionOffTime)
	    || !PHYSFS_readSBE32(fileHandle, &serializeGame->missionETA)
	    || !PHYSFS_readUBE16(fileHandle, &serializeGame->missionHomeLZ_X)
	    || !PHYSFS_readUBE16(fileHandle, &serializeGame->missionHomeLZ_Y)
	    || !PHYSFS_readSBE32(fileHandle, &serializeGame->missionPlayerX)
	    || !PHYSFS_readSBE32(fileHandle, &serializeGame->missionPlayerY))
	{
		return false;
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_readUBE16(fileHandle, &serializeGame->iTranspEntryTileX[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_readUBE16(fileHandle, &serializeGame->iTranspEntryTileY[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_readUBE16(fileHandle, &serializeGame->iTranspExitTileX[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_readUBE16(fileHandle, &serializeGame->iTranspExitTileY[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_readUBE32(fileHandle, &serializeGame->aDefaultSensor[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_readUBE32(fileHandle, &serializeGame->aDefaultECM[i]))
		{
			return false;
		}
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		if (!PHYSFS_readUBE32(fileHandle, &serializeGame->aDefaultRepair[i]))
		{
			return false;
		}
	}

	return true;
}

struct SAVE_GAME_V15 : public SAVE_GAME_V14
{
	int32_t    offWorldKeepLists;	// was BOOL (which was a int)
	uint8_t     aDroidExperience[MAX_PLAYERS][MAX_RECYCLED_DROIDS];
	uint32_t    RubbleTile;
	uint32_t    WaterTile;
	uint32_t    fogColour;
	uint32_t    fogState;
};

static void serializeSaveGameV15Data_json(nlohmann::json &o, const SAVE_GAME_V15 *serializeGame)
{
	serializeSaveGameV14Data_json(o, (const SAVE_GAME_V14 *) serializeGame);
	o["offWorldKeepLists"] = serializeGame->offWorldKeepLists;
	o["RubbleTile"] = serializeGame->RubbleTile;
	o["WaterTile"] = serializeGame->WaterTile;

}
static bool serializeSaveGameV15Data(PHYSFS_file *fileHandle, const SAVE_GAME_V15 *serializeGame)
{
	unsigned int i, j;

	if (!serializeSaveGameV14Data(fileHandle, (const SAVE_GAME_V14 *) serializeGame)
	    || !PHYSFS_writeSBE32(fileHandle, serializeGame->offWorldKeepLists))
	{
		return false;
	}

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		for (j = 0; j < MAX_RECYCLED_DROIDS; ++j)
		{
			if (!PHYSFS_writeUBE8(fileHandle, 0)) // no longer saved in binary form
			{
				return false;
			}
		}
	}

	return (PHYSFS_writeUBE32(fileHandle, serializeGame->RubbleTile)
	        && PHYSFS_writeUBE32(fileHandle, serializeGame->WaterTile)
	        && PHYSFS_writeUBE32(fileHandle, 0)
	        && PHYSFS_writeUBE32(fileHandle, 0));
}
static void deserializeSaveGameV15Data_json(const nlohmann::json &o, SAVE_GAME_V15 *serializeGame)
{
	deserializeSaveGameV14Data_json(o, (SAVE_GAME_V14 *) serializeGame);
	serializeGame->offWorldKeepLists = o.at("offWorldKeepLists").get<int32_t>();
	serializeGame->RubbleTile = o.at("RubbleTile").get<uint32_t>();
	serializeGame->WaterTile = o.at("WaterTile").get<uint32_t>();
	serializeGame->fogColour = 0;
	serializeGame->fogState = 0;
}
static bool deserializeSaveGameV15Data(PHYSFS_file *fileHandle, SAVE_GAME_V15 *serializeGame)
{
	unsigned int i, j;
	int32_t boolOffWorldKeepLists;

	if (!deserializeSaveGameV14Data(fileHandle, (SAVE_GAME_V14 *) serializeGame)
	    || !PHYSFS_readSBE32(fileHandle, &boolOffWorldKeepLists))
	{
		return false;
	}

	serializeGame->offWorldKeepLists = boolOffWorldKeepLists;

	for (i = 0; i < MAX_PLAYERS; ++i)
	{
		for (j = 0; j < MAX_RECYCLED_DROIDS; ++j)
		{
			uint8_t tmp;
			if (!PHYSFS_readUBE8(fileHandle, &tmp))
			{
				return false;
			}
			if (tmp > 0)
			{
				add_to_experience_queue(i, tmp * 65536);
			}
		}
	}

	return (PHYSFS_readUBE32(fileHandle, &serializeGame->RubbleTile)
	        && PHYSFS_readUBE32(fileHandle, &serializeGame->WaterTile)
	        && PHYSFS_readUBE32(fileHandle, &serializeGame->fogColour)
	        && PHYSFS_readUBE32(fileHandle, &serializeGame->fogState));
}

struct DROIDINIT_SAVEHEADER : public GAME_SAVEHEADER
{
	UDWORD		quantity;
};

struct SAVE_DROIDINIT
{
	OBJECT_SAVE_V19;
};

/*
 *	STRUCTURE Definitions
 */

#define STRUCTURE_SAVE_V2 \
	OBJECT_SAVE_V19; \
	UBYTE				status; \
	SDWORD				currentBuildPts; \
	UDWORD				body; \
	UDWORD				armour; \
	UDWORD				resistance; \
	UDWORD				dummy1; \
	UDWORD				subjectInc;  /*research inc or factory prod id*/\
	UDWORD				timeStarted; \
	UDWORD				output; \
	UDWORD				capacity; \
	UDWORD				quantity

struct SAVE_STRUCTURE_V2
{
	STRUCTURE_SAVE_V2;
};

#define STRUCTURE_SAVE_V12 \
	STRUCTURE_SAVE_V2; \
	UDWORD				factoryInc;			\
	UBYTE				loopsPerformed;		\
	UDWORD				powerAccrued;		\
	UDWORD				dummy2;			\
	UDWORD				droidTimeStarted;	\
	UDWORD				timeToBuild;		\
	UDWORD				timeStartHold

struct SAVE_STRUCTURE_V12
{
	STRUCTURE_SAVE_V12;
};

#define STRUCTURE_SAVE_V14 \
	STRUCTURE_SAVE_V12; \
	UBYTE	visible[MAX_PLAYERS]

struct SAVE_STRUCTURE_V14
{
	STRUCTURE_SAVE_V14;
};

#define STRUCTURE_SAVE_V15 \
	STRUCTURE_SAVE_V14; \
	char	researchName[MAX_SAVE_NAME_SIZE_V19]

struct SAVE_STRUCTURE_V15
{
	STRUCTURE_SAVE_V15;
};

#define STRUCTURE_SAVE_V17 \
	STRUCTURE_SAVE_V15;\
	SWORD				currentPowerAccrued

struct SAVE_STRUCTURE_V17
{
	STRUCTURE_SAVE_V17;
};

#define STRUCTURE_SAVE_V20 \
	OBJECT_SAVE_V20; \
	UBYTE				status; \
	SDWORD				currentBuildPts; \
	UDWORD				body; \
	UDWORD				armour; \
	UDWORD				resistance; \
	UDWORD				dummy1; \
	UDWORD				subjectInc;  /*research inc or factory prod id*/\
	UDWORD				timeStarted; \
	UDWORD				output; \
	UDWORD				capacity; \
	UDWORD				quantity; \
	UDWORD				factoryInc;			\
	UBYTE				loopsPerformed;		\
	UDWORD				powerAccrued;		\
	UDWORD				dummy2;			\
	UDWORD				droidTimeStarted;	\
	UDWORD				timeToBuild;		\
	UDWORD				timeStartHold; \
	UBYTE				visible[MAX_PLAYERS]; \
	char				researchName[MAX_SAVE_NAME_SIZE]; \
	SWORD				currentPowerAccrued

struct SAVE_STRUCTURE_V20
{
	STRUCTURE_SAVE_V20;
};

#define STRUCTURE_SAVE_V21 \
	STRUCTURE_SAVE_V20; \
	UDWORD				commandId

struct SAVE_STRUCTURE_V21
{
	STRUCTURE_SAVE_V21;
};

struct SAVE_STRUCTURE
{
	STRUCTURE_SAVE_V21;
};

#define FEATURE_SAVE_V2 \
	OBJECT_SAVE_V19

struct SAVE_FEATURE_V2
{
	FEATURE_SAVE_V2;
};

#define FEATURE_SAVE_V14 \
	FEATURE_SAVE_V2; \
	UBYTE	visible[MAX_PLAYERS]

struct SAVE_FEATURE_V14
{
	FEATURE_SAVE_V14;
};

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/
extern uint32_t unsynchObjID;  // unique ID creation thing..
extern uint32_t synchObjID;    // unique ID creation thing..

static UDWORD			saveGameVersion = 0;
static bool				saveGameOnMission = false;

static UDWORD		savedGameTime;
static UDWORD		savedObjId;
static SDWORD		startX, startY;
static UDWORD		width, height;
static GAME_TYPE	gameType;
static bool IsScenario;

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/
static bool gameLoadV7(PHYSFS_file *fileHandle, nonstd::optional<nlohmann::json>&);
static bool gameLoadV(PHYSFS_file *fileHandle, unsigned int version, nonstd::optional<nlohmann::json>&);
static bool loadMainFile(const std::string &fileName);
static bool loadMainFileFinal(const std::string &fileName);
static bool writeMainFile(const std::string &fileName, SDWORD saveType);
static bool writeGameFile(const char *fileName, SDWORD saveType);
static bool writeMapFile(const char *fileName);

static bool loadWzMapDroidInit(WzMap::Map &wzMap);

static bool loadSaveDroid(const char *pFileName, DROID **ppsCurrentDroidLists);
static bool loadSaveDroidPointers(const WzString &pFileName, DROID **ppsCurrentDroidLists);
static bool writeDroidFile(const char *pFileName, DROID **ppsCurrentDroidLists);

static bool loadSaveStructure(char *pFileData, UDWORD filesize);
static bool loadSaveStructure2(const char *pFileName, STRUCTURE **ppList);
static bool loadWzMapStructure(WzMap::Map& wzMap);
static bool loadSaveStructurePointers(const WzString& filename, STRUCTURE **ppList);
static bool writeStructFile(const char *pFileName);

static bool loadSaveTemplate(const char *pFileName);
static bool writeTemplateFile(const char *pFileName);

static bool loadSaveFeature(char *pFileData, UDWORD filesize);
static bool writeFeatureFile(const char *pFileName);
static bool loadSaveFeature2(const char *pFileName);
static bool loadWzMapFeature(WzMap::Map &wzMap);

static bool writeTerrainTypeMapFile(char *pFileName);

static bool loadSaveCompList(const char *pFileName);
static bool writeCompListFile(const char *pFileName);

static bool loadSaveStructTypeList(const char *pFileName);
static bool writeStructTypeListFile(const char *pFileName);

static bool loadSaveResearch(const char *pFileName);
static bool writeResearchFile(char *pFileName);

static bool loadSaveMessage(const char* pFileName, LEVEL_TYPE levelType);
static bool writeMessageFile(const char *pFileName);

static bool loadSaveStructLimits(const char *pFileName);
static bool writeStructLimitsFile(const char *pFileName);

static bool readFiresupportDesignators(const char *pFileName);
static bool writeFiresupportDesignators(const char *pFileName);

static bool writeScriptState(const char *pFileName);

/* set the global scroll values to use for the save game */
static void setMapScroll();

static char *getSaveStructNameV19(SAVE_STRUCTURE_V17 *psSaveStructure)
{
	return (psSaveStructure->name);
}

/*This just loads up the .gam file to determine which level data to set up - split up
so can be called in levLoadData when starting a game from a load save game*/

// -----------------------------------------------------------------------------------------
bool loadGameInit(const char *fileName)
{
	ASSERT_OR_RETURN(false, fileName != nullptr, "fileName is null??");

	if (strEndsWith(fileName, ".wzrp"))
	{
		SetGameMode(GS_TITLE_SCREEN); // hack - the caller sets this to GS_NORMAL but we actually want to proceed with normal startGameLoop

		// if it ends in .wzrp, try to load the replay!
		WZGameReplayOptionsHandler optionsHandler;
		if (!NETloadReplay(fileName, optionsHandler))
		{
			return false;
		}

		bMultiPlayer = true;
		bMultiMessages = true;
		changeTitleMode(STARTGAME);
	}

	return true;
}


// -----------------------------------------------------------------------------------------
// Load a file from a save game into the psx.
// This is divided up into 2 parts ...
//
// if it is a level loaded up from CD then UserSaveGame will by false
// UserSaveGame ... Extra stuff to load after scripts
bool loadMissionExtras(const char* pGameToLoad, LEVEL_TYPE levelType)
{
	char			aFileName[256];
	size_t			fileExten;

	sstrcpy(aFileName, pGameToLoad);
	fileExten = strlen(pGameToLoad) - 3;
	aFileName[fileExten - 1] = '\0';
	strcat(aFileName, "/");

	if (saveGameVersion >= VERSION_11)
	{
		//if user save game then load up the messages AFTER any droids or structures are loaded
		if (gameType == GTYPE_SAVE_START || gameType == GTYPE_SAVE_MIDMISSION)
		{
			//load in the message list file
			aFileName[fileExten] = '\0';
			strcat(aFileName, "messtate.json");
			if (!loadSaveMessage(aFileName, levelType))
			{
				debug(LOG_ERROR, "Failed to load mission extras from %s", aFileName);
				return false;
			}
		}
	}

	return true;
}

static void sanityUpdate()
{
	for (auto player = 0; player < game.maxPlayers; player++)
	{
		for (auto& psDroid : apsDroidLists[player])
		{
			orderCheckList(&psDroid);
			actionSanity(&psDroid);
		}
	}
}

static void getIniBaseObject(WzConfig &ini, WzString const &key, BASE_OBJECT *&object)
{
	object = nullptr;
	if (ini.contains(key + "/id"))
	{
		int tid = ini.value(key + "/id", -1).toInt();
		int tplayer = ini.value(key + "/player", -1).toInt();
		OBJECT_TYPE ttype = (OBJECT_TYPE)ini.value(key + "/type", 0).toInt();
		ASSERT_OR_RETURN(, tid >= 0 && tplayer >= 0, "Bad ID");
		object = getBaseObjFromData(tid, tplayer, ttype);
		ASSERT(object != nullptr, "Failed to find target");
	}
}

static void getIniStructureStats(WzConfig &ini, WzString const &key, STRUCTURE_STATS *&stats)
{
	stats = nullptr;
	if (ini.contains(key)) {
		WzString statName = ini.value(key).toWzString();
		int tid = getStructStatFromName(statName);
		ASSERT_OR_RETURN(, tid >= 0, "Target stats not found %s", statName.toUtf8().c_str());
		stats = &asStructureStats[tid];
	}
}

static void getIniDroidOrder(WzConfig &ini, WzString const &key, DroidOrder &order)
{
	order.type = (DroidOrderType)ini.value(key + "/type", DORDER_NONE).toInt();
	order.pos = ini.vector2i(key + "/pos");
	order.pos2 = ini.vector2i(key + "/pos2");
	order.direction = ini.value(key + "/direction").toInt();
	getIniBaseObject(ini, key + "/obj", order.psObj);
	getIniStructureStats(ini, key + "/stats", order.psStats);
}

static void setIniBaseObject(nlohmann::json &json, WzString const &key, BASE_OBJECT const *object)
{
	if (object != nullptr && object->died <= 1)
	{
		const auto& keyStr = key.toStdString();
		json[keyStr + "/id"] = object->id;
		json[keyStr + "/player"] = object->player;
		json[keyStr + "/type"] = object->type;
#ifdef DEBUG
		//ini.setValue(key + "/debugfunc", WzString::fromUtf8(psCurr->targetFunc));
		//ini.setValue(key + "/debugline", psCurr->targetLine);
#endif
	}
}

static inline void setIniStructureStats(nlohmann::json &jsonObj, WzString const &key, STRUCTURE_STATS const *stats)
{
	if (stats != nullptr)
	{
		jsonObj[key.toStdString()] = stats->id;
	}
}

static inline void setIniDroidOrder(nlohmann::json &jsonObj, WzString const &key, DroidOrder const &order)
{
	const auto& keyStr = key.toStdString();
	jsonObj[keyStr + "/type"] = order.type;
	jsonObj[keyStr + "/pos"] = order.pos;
	jsonObj[keyStr + "/pos2"] = order.pos2;
	jsonObj[keyStr + "/direction"] = order.direction;
	setIniBaseObject(jsonObj, key + "/obj", order.psObj);
	setIniStructureStats(jsonObj, key + "/stats", order.psStats);
}

static void allocatePlayers()
{
	DebugInputManager& dbgInputManager = gInputManager.debugManager();
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (NetPlay.players[i].difficulty == AIDifficulty::HUMAN || (game.type == LEVEL_TYPE::CAMPAIGN && i == 0))
		{
			NetPlay.players[i].allocated = true;
			//processDebugMappings ensures game does not start in DEBUG mode
			dbgInputManager.setPlayerWantsDebugMappings(i, false);
		}
		else
		{
			NetPlay.players[i].allocated = false;
		}
	}
}

static void getPlayerNames()
{
	/* Get human and AI players names */
  if (saveGameVersion < VERSION_34)
    return;
}

static WzMap::MapType getWzMapType(bool UserSaveGame)
{
	if (UserSaveGame) {
		return WzMap::MapType::SAVEGAME;
	}

  return game.type == LEVEL_TYPE::CAMPAIGN
         ? WzMap::MapType::CAMPAIGN
         : WzMap::MapType::SKIRMISH;
}

static bool writeMapFile(const char *fileName)
{
	ASSERT_OR_RETURN(false, fileName != nullptr, "filename is null");

	/* Get the save data */
	WzMap::MapData mapData;
	bool status = mapSaveToWzMapData(mapData);
	if (!status)
	{
		return false;
	}

	/* Write out the map data */
	WzMapPhysFSIO mapIO;
	WzMapDebugLogger debugLoggerInstance;
	status = WzMap::writeMapData(mapData, fileName, mapIO, WzMap::LatestOutputFormat, &debugLoggerInstance);

	return status;
}

/* code specific to version 7 of a save game */
bool gameLoadV7(PHYSFS_file *fileHandle, nonstd::optional<nlohmann::json> &gamJson)
{
	SAVE_GAME_V7 saveGame;
	if (gamJson.has_value())
	{
		// this seems to be still used by maps/mission loading routines
		deserializeSaveGameV7Data_json(gamJson.value(), &saveGame);
	}
	else
	{
		if (WZ_PHYSFS_readBytes(fileHandle, &saveGame, sizeof(saveGame)) != sizeof(saveGame))
		{
			debug(LOG_ERROR, "gameLoadV7: error while reading file: %s", WZ_PHYSFS_getLastError());

			return false;
		}
	}


	/* GAME_SAVE_V7 */
	endian_udword(&saveGame.gameTime);
	endian_udword(&saveGame.GameType);
	endian_sdword(&saveGame.ScrollMinX);
	endian_sdword(&saveGame.ScrollMinY);
	endian_udword(&saveGame.ScrollMaxX);
	endian_udword(&saveGame.ScrollMaxY);

	savedGameTime = saveGame.gameTime;

	//set the scroll varaibles
	startX = saveGame.ScrollMinX;
	startY = saveGame.ScrollMinY;
	width = saveGame.ScrollMaxX - saveGame.ScrollMinX;
	height = saveGame.ScrollMaxY - saveGame.ScrollMinY;
	gameType = static_cast<GAME_TYPE>(saveGame.GameType);
	//set IsScenario to true if not a user saved game
	if (gameType == GTYPE_SAVE_START)
	{
		LEVEL_DATASET *psNewLevel;

		IsScenario = false;
		//copy the level name across
		sstrcpy(aLevelName, saveGame.levelName);
		//load up the level dataset
		if (!levLoadData(aLevelName, nullptr, saveGameName, (GAME_TYPE)gameType))
		{
			return false;
		}
		// find the level dataset
		psNewLevel = levFindDataSet(aLevelName);
		if (psNewLevel == nullptr)
		{
			debug(LOG_ERROR, "gameLoadV7: couldn't find level data");

			return false;
		}
	}
	else
	{
		IsScenario = true;
	}

	return true;
}

// Load main game data from JSON. Only implement stuff here that we actually use instead of
// the binary blobbery.
static bool loadMainFile(const std::string &fileName)
{
	WzConfig save(WzString::fromUtf8(fileName), WzConfig::ReadOnly);

	if (save.contains("gameType"))
	{
		game.type = static_cast<LEVEL_TYPE>(save.value("gameType").toInt());
	}
	if (save.contains("scavengers"))
	{
		auto saveScavValue = save.value("scavengers").toUInt();
		if (saveScavValue <= ULTIMATE_SCAVENGERS)
		{
			game.scavengers = static_cast<uint8_t>(saveScavValue);
		}
		else
		{
			debug(LOG_ERROR, "Invalid scavengers value: %u", saveScavValue);
		}
	}
	if (save.contains("maxPlayers"))
	{
		game.maxPlayers = save.value("maxPlayers").toUInt();
	}
	if (save.contains("mapHasScavengers"))
	{
		game.mapHasScavengers = save.value("mapHasScavengers").toBool();
	}
	if (save.contains("playerBuiltHQ"))
	{
		playerBuiltHQ = save.value("playerBuiltHQ").toBool();
	}
	if (save.contains("challengeFileName"))
	{
	}
	if (save.contains("challengeActive"))
	{
	}
	if (save.contains("builtInMap"))
	{
		builtInMap = save.value("builtInMap").toBool();
	}
	if (save.contains("inactivityMinutes"))
	{
		game.inactivityMinutes = save.value("inactivityMinutes").toUInt();
	}

	save.beginArray("players");
	while (save.remainingArrayItems() > 0)
	{
		int index = save.value("index").toInt();
		if (!(index >= 0 && index < MAX_PLAYERS))
		{
			debug(LOG_ERROR, "Invalid player index: %d", index);
			save.nextArrayItem();
			continue;
		}
		unsigned int FactionValue = save.value("faction", static_cast<uint8_t>(FACTION_NORMAL)).toUInt();
		NetPlay.players[index].faction = static_cast<FactionID>(FactionValue);
		save.nextArrayItem();
	}
	save.endArray();

	return true;
}

static bool loadMainFileFinal(const std::string &fileName)
{
	WzConfig save(WzString::fromUtf8(fileName), WzConfig::ReadOnly);

	if (save.contains("techLevel"))
	{
		game.techLevel = save.value("techLevel").toInt();
	}

	save.beginArray("players");
	while (save.remainingArrayItems() > 0)
	{
		int index = save.value("index").toInt();
		if (!(index >= 0 && index < MAX_PLAYERS))
		{
			debug(LOG_ERROR, "Invalid player index: %d", index);
			save.nextArrayItem();
			continue;
		}
		auto value = save.value("recycled_droids").jsonValue();
		for (const auto &v : value)
		{
			add_to_experience_queue(index, json_variant(v).toInt());
		}
		save.nextArrayItem();
	}
	save.endArray();

	return true;
}

nonstd::optional<nlohmann::json> parseJsonFile(const char *filename)
{
	UDWORD pFileSize;
	char *ppFileData = nullptr;
	debug(LOG_SAVEGAME, "starting deserialize %s", filename);
	if (!loadFile(filename, &ppFileData, &pFileSize, false))
	{
		debug(LOG_SAVE, "No %s found, sad", filename);
		return nullopt;
	}
	return nlohmann::json::parse(ppFileData);
}

static uint32_t RemapWzMapPlayerNumber(int8_t oldNumber)
{
	if (oldNumber < 0)
	{
		game.mapHasScavengers = true;
		return static_cast<uint32_t>(scavengerSlot());
	}

	if (game.type == LEVEL_TYPE::CAMPAIGN)		// don't remap for SP games
	{
		return oldNumber;
	}

	for (uint32_t i = 0; i < MAX_PLAYERS; i++)
	{
		if (oldNumber == NetPlay.players[i].position)
		{
			game.mapHasScavengers = game.mapHasScavengers || i == scavengerSlot();
			return i;
		}
	}
	ASSERT(false, "Found no player position for player %d", (int)oldNumber);
	return 0;
}

static bool loadWzMapDroidInit(WzMap::Map &wzMap)
{
	uint32_t NumberOfSkippedDroids = 0;
	auto pDroids = wzMap.mapDroids();
	ASSERT_OR_RETURN(false, pDroids.get() != nullptr, "No data.");

	for (auto &droid : *pDroids)
	{
		unsigned player = RemapWzMapPlayerNumber(droid.player);
		if (player >= MAX_PLAYERS)
		{
			player = MAX_PLAYERS - 1;	// now don't lose any droids ... force them to be the last player
			NumberOfSkippedDroids++;
		}
		auto psTemplate = getTemplateFromTranslatedNameNoPlayer(droid.name.c_str());
		if (psTemplate == nullptr)
		{
			debug(LOG_ERROR, "Unable to find template for %s for player %d -- unit skipped", droid.name.c_str(), player);
			continue;
		}
		turnOffMultiMsg(true);
		auto psDroid = reallyBuildDroid(psTemplate, Position(droid.position.x, droid.position.y, 0), player, false, {droid.direction, 0, 0});
		turnOffMultiMsg(false);
		if (psDroid == nullptr)
		{
			debug(LOG_ERROR, "Failed to build unit %s", droid.name.c_str());
			continue;
		}
		if (droid.id.has_value())
		{
			psDroid->id = droid.id.value() > 0 ? droid.id.value() : 0xFEDBCA98;	// hack to remove droid id zero
		}
		ASSERT(psDroid->id != 0, "Droid ID should never be zero here");

		// HACK!!
		Vector2i startpos = getPlayerStartPosition(player);
		if (psDroid->droidType == DROID_CONSTRUCT && startpos.x == 0 && startpos.y == 0)
		{
			scriptSetStartPos(psDroid->player, psDroid->pos.x, psDroid->pos.y);	// set map start position, FIXME - save properly elsewhere!
		}

		addDroid(psDroid);
	}
	if (NumberOfSkippedDroids)
	{
		debug(LOG_ERROR, "Bad Player number in %d unit(s)... assigned to the last player!", NumberOfSkippedDroids);
		return false;
	}

	return true;
}

// Remaps old player number based on position on map to new owner
static UDWORD RemapPlayerNumber(UDWORD OldNumber)
{
	int i;

	if (game.type == LEVEL_TYPE::CAMPAIGN)		// don't remap for SP games
	{
		return OldNumber;
	}

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		if (OldNumber == NetPlay.players[i].position)
		{
			game.mapHasScavengers = game.mapHasScavengers || i == scavengerSlot();
			return i;
		}
	}
	ASSERT(false, "Found no player position for player %d", (int)OldNumber);
	return 0;
}

static int getPlayer(WzConfig &ini)
{
	if (ini.contains("player"))
	{
		json_variant result = ini.value("player");
		if (result.toWzString().startsWith("scavenger"))
		{
			game.mapHasScavengers = true;
			return scavengerSlot();
		}
		return result.toInt();
	}
	else if (ini.contains("startpos"))
	{
		int position = ini.value("startpos").toInt();
		for (int i = 0; i < game.maxPlayers; i++)
		{
			if (NetPlay.players[i].position == position)
			{
				return i;
			}
		}
	}
	ASSERT(false, "No player info found!");
	return 0;
}

static void setPlayer(WzConfig &ini, int player)
{
	if (scavengerSlot() == player)
	{
		ini.setValue("player", "scavenger");
	}
	else
	{
		ini.setValue("player", player);
	}
}

static inline void setPlayerJSON(nlohmann::json &jsonObj, int player)
{
	if (scavengerSlot() == player)
	{
		jsonObj["player"] = "scavenger";
	}
	else
	{
		jsonObj["player"] = player;
	}
}

static bool skipForDifficulty(WzConfig &ini, int player)
{ 
	if (ini.contains("difficulty")) // optionally skip this object
	{
		int difficulty = ini.value("difficulty").toInt();
		if (game.type == LEVEL_TYPE::CAMPAIGN && difficulty > (int) getDifficultyLevel()
        || game.type == LEVEL_TYPE::SKIRMISH && difficulty > static_cast<int8_t>(NetPlay.players[player].difficulty))
		{
			return true;
		}
	}
	return false;
}

static bool loadSaveDroidPointers(const WzString &pFileName, DROID **ppsCurrentDroidLists)
{
	WzConfig ini(pFileName, WzConfig::ReadOnly);
	std::vector<WzString> list = ini.childGroups();

	for (size_t i = 0; i < list.size(); ++i) {
    ini.beginGroup(list[i]);
    int id = ini.value("id", -1).toInt();
    int player = getPlayer(ini);

    if (id <= 0) {
      ini.endGroup();
      continue; // special hack for campaign missions, cannot have targets
    }
    if (skipForDifficulty(ini, player)) {
      ini.endGroup();
      continue; // another hack for campaign missions, cannot have targets
    }

    for (auto &psDroid: apsDroidLists[player]) {
      if (psDroid.id == id) break;
      if (isTransporter(&psDroid) && psDroid.psGroup != nullptr)  // Check for droids in the transporter.
      {
        for (auto psTrDroid : psDroid.psGroup->psList) {
          if (psTrDroid->id == id) {
            auto droid = psTrDroid;
            if (!droid) {
              // FIXME
              if (droid) {
                debug(LOG_ERROR, "Droid %s (%d) was in wrong file/list (was in %s)...", objInfo(droid), id,
                      pFileName.toUtf8().c_str());
              }
            }
            ASSERT_OR_RETURN(false, droid, "Droid %d not found", id);
            droid->listSize = clip(ini.value("orderList/size", 0).toInt(), 0, 10000);
            droid->asOrderList.resize(
                    droid->listSize);  // Must resize before setting any orders, and must set in-place, since pointers are updated later.
            for (int droidIdx = 0; droidIdx < droid->listSize; ++droidIdx) {
              getIniDroidOrder(ini, "orderList/" + WzString::number(droidIdx), droid->asOrderList[droidIdx]);
            }
            droid->listPendingBegin = 0;
            for (int j = 0; j < MAX_WEAPONS; j++) {
              objTrace(droid->id, "weapon %d, nStat %d", j, droid->asWeaps[j].nStat);
              getIniBaseObject(ini, "actionTarget/" + WzString::number(j), droid->psActionTarget[j]);
            }
            if (ini.contains("baseStruct/id")) {
              int tid = ini.value("baseStruct/id", -1).toInt();
              int tplayer = ini.value("baseStruct/player", -1).toInt();
              OBJECT_TYPE ttype = (OBJECT_TYPE) ini.value("baseStruct/type", 0).toInt();
              ASSERT(tid >= 0 && tplayer >= 0, "Bad ID");
              BASE_OBJECT *psObj = getBaseObjFromData(tid, tplayer, ttype);
              ASSERT(psObj, "Failed to find droid base structure");
              ASSERT(!psObj || psObj->type == OBJ_STRUCTURE, "Droid base structure not a structure");
              setSaveDroidBase(droid, (STRUCTURE *) psObj);
            }
            if (ini.contains("commander")) {
              int tid = ini.value("commander", -1).toInt();
              DROID *psCommander = (DROID *) getBaseObjFromData(tid, droid->player, OBJ_DROID);
              ASSERT(psCommander, "Failed to find droid commander");
              cmdDroidAddDroid(psCommander, droid);
            }
            getIniDroidOrder(ini, "order", droid->order);
            ini.endGroup();
          }
        }
      }
    }
  }
	return true;
}

static int healthValue(WzConfig &ini, int defaultValue)
{
	WzString health = ini.value("health").toWzString();
	if (health.isEmpty() || defaultValue == 0)
	{
		return defaultValue;
	}
	else if (health.contains(WzUniCodepoint::fromASCII('%')))
	{
		int perc = health.replace("%", "").toInt();
		return MAX(defaultValue * perc / 100, 1); //hp not supposed to be 0
	}
	else
	{
		return MIN(health.toInt(), defaultValue);
	}
}

static void loadSaveObject(WzConfig &ini, BASE_OBJECT *psObj)
{
	psObj->died = ini.value("died", 0).toInt();
  psObj->visible.fill(0);
	for (int j = 0; j < game.maxPlayers; j++)
	{
		psObj->visible[j] = ini.value("visible/" + WzString::number(j), 0).toInt();
	}
	psObj->periodicalDamage = ini.value("periodicalDamage", 0).toInt();
	psObj->periodicalDamageStart = ini.value("periodicalDamageStart", 0).toInt();
	psObj->timeAnimationStarted = ini.value("timeAnimationStarted", 0).toInt();
	psObj->animationEvent = ini.value("animationEvent", 0).toInt();
	psObj->timeLastHit = ini.value("timeLastHit", UDWORD_MAX).toInt();
	psObj->lastEmission = ini.value("lastEmission", 0).toInt();
	psObj->selected = ini.value("selected", false).toBool();
	psObj->born = ini.value("born", 2).toInt();
}

static void writeSaveObject(WzConfig &ini, BASE_OBJECT *psObj)
{
	ini.setValue("id", psObj->id);
	setPlayer(ini, psObj->player);
	ini.setValue("health", psObj->body);
	ini.setVector3i("position", psObj->pos);
	ini.setVector3i("rotation", toVector(psObj->rot));
	if (psObj->timeAnimationStarted)
	{
		ini.setValue("timeAnimationStarted", psObj->timeAnimationStarted);
	}
	if (psObj->animationEvent)
	{
		ini.setValue("animationEvent", psObj->animationEvent);
	}
	ini.setValue("selected", psObj->selected);	// third kind of group
	if (psObj->lastEmission)
	{
		ini.setValue("lastEmission", psObj->lastEmission);
	}
	if (psObj->periodicalDamageStart > 0)
	{
		ini.setValue("periodicalDamageStart", psObj->periodicalDamageStart);
	}
	if (psObj->periodicalDamage > 0)
	{
		ini.setValue("periodicalDamage", psObj->periodicalDamage);
	}
	ini.setValue("born", psObj->born);
	if (psObj->died > 0)
	{
		ini.setValue("died", psObj->died);
	}
	if (psObj->timeLastHit != UDWORD_MAX)
	{
		ini.setValue("timeLastHit", psObj->timeLastHit);
	}
	if (psObj->selected)
	{
		ini.setValue("selected", psObj->selected);
	}
	for (int i = 0; i < game.maxPlayers; i++)
	{
		if (psObj->visible[i])
		{
			ini.setValue("visible/" + WzString::number(i), psObj->visible[i]);
		}
	}
}

static void writeSaveObjectJSON(nlohmann::json &jsonObj, BASE_OBJECT *psObj)
{
	jsonObj["id"] = psObj->id;
	setPlayerJSON(jsonObj, psObj->player);
	jsonObj["health"] = psObj->body;
	jsonObj["position"] = psObj->pos;
	jsonObj["rotation"] = toVector(psObj->rot);
	if (psObj->timeAnimationStarted)
	{
		jsonObj["timeAnimationStarted"] = psObj->timeAnimationStarted;
	}
	if (psObj->animationEvent)
	{
		jsonObj["animationEvent"] = psObj->animationEvent;
	}
	jsonObj["selected"] = psObj->selected;	// third kind of group
	if (psObj->lastEmission)
	{
		jsonObj["lastEmission"] = psObj->lastEmission;
	}
	if (psObj->periodicalDamageStart > 0)
	{
		jsonObj["periodicalDamageStart"] = psObj->periodicalDamageStart;
	}
	if (psObj->periodicalDamage > 0)
	{
		jsonObj["periodicalDamage"] = psObj->periodicalDamage;
	}
	jsonObj["born"] = psObj->born;
	if (psObj->died > 0)
	{
		jsonObj["died"] = psObj->died;
	}
	if (psObj->timeLastHit != UDWORD_MAX)
	{
		jsonObj["timeLastHit"] = psObj->timeLastHit;
	}
	if (psObj->selected)
	{
		jsonObj["selected"] = psObj->selected;
	}
	for (int i = 0; i < game.maxPlayers; i++)
	{
		if (psObj->visible[i])
		{
			jsonObj["visible/" + WzString::number(i).toStdString()] = psObj->visible[i];
		}
	}
}

static bool loadSaveDroid(const char *pFileName, DROID **ppsCurrentDroidLists)
{
	if (!PHYSFS_exists(pFileName))
	{
		debug(LOG_SAVE, "No %s found -- use fallback method", pFileName);
		return false;	// try to use fallback method
	}
	WzString fName = WzString::fromUtf8(pFileName);
	WzConfig ini(fName, WzConfig::ReadOnly);
	std::vector<WzString> list = ini.childGroups();
	// Sort list so transports are loaded first, since they must be loaded before the droids they contain.
	std::vector<std::pair<int, WzString>> sortedList;
	bool missionList = fName.compare("mdroid");
	for (size_t i = 0; i < list.size(); ++i)
	{
		ini.beginGroup(list[i]);
		DROID_TYPE droidType = (DROID_TYPE)ini.value("droidType").toInt();
		int priority = 0;
		switch (droidType)
		{
		case DROID_TRANSPORTER:
			++priority; // fallthrough
		case DROID_SUPERTRANSPORTER:
			++priority; // fallthrough
		case DROID_COMMAND:
			//Don't care about sorting commanders in the mission list for safety missions. They
			//don't have a group to command and it messes up the order of the list sorting them
			//which causes problems getting the first transporter group for Gamma-1.
		default:
			break;
		}
		sortedList.push_back(std::make_pair(-priority, list[i]));
		ini.endGroup();
	}
	std::sort(sortedList.begin(), sortedList.end());

	for (unsigned i = 0; i < sortedList.size(); ++i)
	{
		ini.beginGroup(sortedList[i].second);
		DROID *psDroid;
		int player = getPlayer(ini);
		int id = ini.value("id", -1).toInt();
		Position pos = ini.vector3i("position");
		Rotation rot = ini.vector3i("rotation");
		bool onMission = ini.value("onMission", false).toBool();
		DROID_TEMPLATE templ;
		const DROID_TEMPLATE *psTemplate = nullptr;

		if (skipForDifficulty(ini, player))
		{
			ini.endGroup();
			continue;
		}

		if (ini.contains("template"))
		{
			// Use real template (for maps)
			WzString templName(ini.value("template").toWzString());
			psTemplate = getTemplateFromTranslatedNameNoPlayer(templName.toUtf8().c_str());
			if (psTemplate == nullptr)
			{
				debug(LOG_ERROR, "Unable to find template for %s for player %d -- unit skipped", templName.toUtf8().c_str(), player);
				ini.endGroup();
				continue;
			}
		}
		else
		{
			// Create fake template
			templ.name = ini.string("name", "UNKNOWN");
			templ.droidType = (DROID_TYPE)ini.value("droidType").toInt();
			templ.numWeaps = ini.value("weapons", 0).toInt();
			ini.beginGroup("parts");	// the following is copy-pasted from loadSaveTemplate() -- fixme somehow
			templ.asParts[COMP_BODY] = getCompFromName(COMP_BODY, ini.value("body", "ZNULLBODY").toWzString());
			templ.asParts[COMP_BRAIN] = getCompFromName(COMP_BRAIN, ini.value("brain", "ZNULLBRAIN").toWzString());
			templ.asParts[COMP_PROPULSION] = getCompFromName(COMP_PROPULSION, ini.value("propulsion", "ZNULLPROP").toWzString());
			templ.asParts[COMP_REPAIRUNIT] = getCompFromName(COMP_REPAIRUNIT, ini.value("repair", "ZNULLREPAIR").toWzString());
			templ.asParts[COMP_ECM] = getCompFromName(COMP_ECM, ini.value("ecm", "ZNULLECM").toWzString());
			templ.asParts[COMP_SENSOR] = getCompFromName(COMP_SENSOR, ini.value("sensor", "ZNULLSENSOR").toWzString());
			templ.asParts[COMP_CONSTRUCT] = getCompFromName(COMP_CONSTRUCT, ini.value("construct", "ZNULLCONSTRUCT").toWzString());
			templ.asWeaps[0] = getCompFromName(COMP_WEAPON, ini.value("weapon/1", "ZNULLWEAPON").toWzString());
			templ.asWeaps[1] = getCompFromName(COMP_WEAPON, ini.value("weapon/2", "ZNULLWEAPON").toWzString());
			templ.asWeaps[2] = getCompFromName(COMP_WEAPON, ini.value("weapon/3", "ZNULLWEAPON").toWzString());
			ini.endGroup();
			psTemplate = &templ;
		}

		// If droid is on a mission, calling with the saved position might cause an assertion. Or something like that.
		if (!onMission)
		{
			pos.x = clip(pos.x, world_coord(1), world_coord(mapWidth - 1));
			pos.y = clip(pos.y, world_coord(1), world_coord(mapHeight - 1));
		}

		/* Create the Droid */
		turnOffMultiMsg(true);
		psDroid = reallyBuildDroid(psTemplate, pos, player, onMission, rot);
		ASSERT_OR_RETURN(false, psDroid != nullptr, "Failed to build unit %s", sortedList[i].second.toUtf8().c_str());
		turnOffMultiMsg(false);

		// Copy the values across
		if (id > 0)
		{
			psDroid->id = id; // force correct ID, unless ID is set to eg -1, in which case we should keep new ID (useful for starting units in campaign)
		}
		ASSERT(id != 0, "Droid ID should never be zero here");
		// conditional check so that existing saved games don't break
		if (ini.contains("originalBody"))
		{
			// we need to set "originalBody" before setting "body", otherwise CHECK_DROID throws assertion errors
			// we cannot use droidUpgradeBody here to calculate "originalBody", because upgrades aren't loaded yet
			// so it's much simplier just store/retrieve originalBody value
			psDroid->originalBody = ini.value("originalBody").toInt();
		}
		psDroid->body = healthValue(ini, psDroid->originalBody);
		ASSERT(psDroid->body != 0, "%s : %d has zero hp!", pFileName, i);
		psDroid->experience = ini.value("experience", 0).toInt();
		psDroid->kills = ini.value("kills", 0).toInt();
		psDroid->secondaryOrder = ini.value("secondaryOrder", psDroid->secondaryOrder).toInt();
		psDroid->secondaryOrderPending = psDroid->secondaryOrder;
		psDroid->action = (DROID_ACTION)ini.value("action", DACTION_NONE).toInt();
		psDroid->actionPos = ini.vector2i("action/pos");
		psDroid->actionStarted = ini.value("actionStarted", 0).toInt();
		psDroid->actionPoints = ini.value("actionPoints", 0).toInt();
		psDroid->resistance = ini.value("resistance", 0).toInt(); // zero resistance == no electronic damage
		psDroid->lastFrustratedTime = ini.value("lastFrustratedTime", 0).toInt();

		// common BASE_OBJECT info
		loadSaveObject(ini, psDroid);

		// copy the droid's weapon stats
		for (int j = 0; j < psDroid->numWeaps; j++)
		{
			if (psDroid->asWeaps[j].nStat > 0)
			{
				psDroid->asWeaps[j].ammo = ini.value("ammo/" + WzString::number(j)).toInt();
				psDroid->asWeaps[j].lastFired = ini.value("lastFired/" + WzString::number(j)).toInt();
				psDroid->asWeaps[j].shotsFired = ini.value("shotsFired/" + WzString::number(j)).toInt();
				psDroid->asWeaps[j].rot = ini.vector3i("rotation/" + WzString::number(j));
			}
		}

		psDroid->group = ini.value("group", UBYTE_MAX).toInt();
		int aigroup = ini.value("aigroup", -1).toInt();
		if (aigroup >= 0)
		{
			DROID_GROUP *psGroup = grpFind(aigroup);
			psGroup->add(psDroid);
			if (psGroup->type == GT_TRANSPORTER)
			{
				psDroid->selected = false;  // Droid should be visible in the transporter interface.
				visRemoveVisibility(psDroid); // should not have visibility data when in a transporter
			}
		}
		else
		{
			if (isTransporter(psDroid) || psDroid->droidType == DROID_COMMAND)
			{
				DROID_GROUP *psGroup = grpCreate();
				psGroup->add(psDroid);
			}
			else
			{
				psDroid->psGroup = nullptr;
			}
		}

		psDroid->sMove.Status = (MOVE_STATUS)ini.value("moveStatus", 0).toInt();
		psDroid->sMove.pathIndex = ini.value("pathIndex", 0).toInt();
		const int numPoints = ini.value("pathLength", 0).toInt();
		psDroid->sMove.asPath.resize(numPoints);
		for (int j = 0; j < numPoints; j++)
		{
			psDroid->sMove.asPath[j] = ini.vector2i("pathNode/" + WzString::number(j));
		}
		psDroid->sMove.destination = ini.vector2i("moveDestination");
		psDroid->sMove.src = ini.vector2i("moveSource");
		psDroid->sMove.target = ini.vector2i("moveTarget");
		psDroid->sMove.speed = ini.value("moveSpeed").toInt();
		psDroid->sMove.moveDir = ini.value("moveDirection").toInt();
		psDroid->sMove.bumpDir = ini.value("bumpDir").toInt();
		psDroid->sMove.iVertSpeed = ini.value("vertSpeed").toInt();
		psDroid->sMove.bumpTime = ini.value("bumpTime").toInt();
		psDroid->sMove.shuffleStart = ini.value("shuffleStart").toInt();
		for (int j = 0; j < MAX_WEAPONS; ++j)
		{
			psDroid->asWeaps[j].usedAmmo = ini.value("attackRun/" + WzString::number(j)).toInt();
		}
		psDroid->sMove.lastBump = ini.value("lastBump").toInt();
		psDroid->sMove.pauseTime = ini.value("pauseTime").toInt();
		Vector2i tmp = ini.vector2i("bumpPosition");
		psDroid->sMove.bumpPos = Vector3i(tmp.x, tmp.y, 0);

		// Recreate path-finding jobs
		if (psDroid->sMove.Status == MOVEWAITROUTE)
		{
			psDroid->sMove.Status = MOVEINACTIVE;
			fpathDroidRoute(psDroid, psDroid->sMove.destination.x, psDroid->sMove.destination.y, FMT_MOVE);
			psDroid->sMove.Status = MOVEWAITROUTE;

			// Droid might be on a mission, so finish pathfinding now, in case pointers swap and map size changes.
			FPATH_RETVAL dr = fpathDroidRoute(psDroid, psDroid->sMove.destination.x, psDroid->sMove.destination.y, FMT_MOVE);
			if (dr == FPR_OK)
			{
				psDroid->sMove.Status = MOVENAVIGATE;
				psDroid->sMove.pathIndex = 0;
			}
			else // if (retVal == FPR_FAILED)
			{
				psDroid->sMove.Status = MOVEINACTIVE;
				actionDroid(psDroid, DACTION_SULK);
			}
			ASSERT(dr != FPR_WAIT, " ");
		}

		// HACK!!
		Vector2i startpos = getPlayerStartPosition(player);
		if (psDroid->droidType == DROID_CONSTRUCT && startpos.x == 0 && startpos.y == 0)
		{
			scriptSetStartPos(psDroid->player, psDroid->pos.x, psDroid->pos.y);	// set map start position, FIXME - save properly elsewhere!
		}

		if (psDroid->psGroup == nullptr || psDroid->psGroup->type != GT_TRANSPORTER || isTransporter(psDroid))  // do not add to list if on a transport, then the group list is used instead
		{
			addDroid(psDroid);
		}

		ini.endGroup();
	}
	return true;
}

// -----------------------------------------------------------------------------------------
/*
Writes the linked list of droids for each player to a file
*/
static nlohmann::json writeDroid(DROID *psCurr, bool onMission, int &counter)
{
	nlohmann::json droidObj = nlohmann::json::object();
	droidObj["name"] = psCurr->aName;
	droidObj["originalBody"] = psCurr->originalBody;
	// write common BASE_OBJECT info
	writeSaveObjectJSON(droidObj, psCurr);

	for (unsigned i = 0; i < psCurr->numWeaps; i++)
	{
		if (psCurr->asWeaps[i].nStat > 0)
		{
			auto numberWzStr = WzString::number(i);
			const std::string& numStr = numberWzStr.toStdString();
			droidObj["ammo/" + numStr] = psCurr->asWeaps[i].ammo;
			droidObj["lastFired/" + numStr] = psCurr->asWeaps[i].lastFired;
			droidObj["shotsFired/" + numStr] = psCurr->asWeaps[i].shotsFired;
			droidObj["rotation/" + numStr] = toVector(psCurr->asWeaps[i].rot);
		}
	}
	for (unsigned i = 0; i < MAX_WEAPONS; i++)
	{
		setIniBaseObject(droidObj, "actionTarget/" + WzString::number(i), psCurr->psActionTarget[i]);
	}
	if (psCurr->lastFrustratedTime > 0)
	{
		droidObj["lastFrustratedTime"] = psCurr->lastFrustratedTime;
	}
	if (psCurr->experience > 0)
	{
		droidObj["experience"] = psCurr->experience;
	}
	if (psCurr->kills > 0)
	{
		droidObj["kills"] = psCurr->kills;
	}

	setIniDroidOrder(droidObj, "order", psCurr->order);
	droidObj["orderList/size"] = psCurr->listSize;
	for (int i = 0; i < psCurr->listSize; ++i)
	{
		setIniDroidOrder(droidObj, "orderList/" + WzString::number(i), psCurr->asOrderList[i]);
	}
	if (psCurr->timeLastHit != UDWORD_MAX)
	{
		droidObj["timeLastHit"] = psCurr->timeLastHit;
	}
	droidObj["secondaryOrder"] = psCurr->secondaryOrder;
	droidObj["action"] = psCurr->action;
	droidObj["actionString"] = getDroidActionName(psCurr->action); // future-proofing
	droidObj["action/pos"] = psCurr->actionPos;
	droidObj["actionStarted"] = psCurr->actionStarted;
	droidObj["actionPoints"] = psCurr->actionPoints;
	if (psCurr->psBaseStruct != nullptr)
	{
		droidObj["baseStruct/id"] = psCurr->psBaseStruct->id;
		droidObj["baseStruct/player"] = psCurr->psBaseStruct->player;	// always ours, but for completeness
		droidObj["baseStruct/type"] = psCurr->psBaseStruct->type;		// always a building, but for completeness
	}
	if (psCurr->psGroup)
	{
		droidObj["aigroup"] = psCurr->psGroup->id;	// AI and commander/transport group
		droidObj["aigroup/type"] = psCurr->psGroup->type;
	}
	droidObj["group"] = psCurr->group;	// different kind of group. of course.
	if (hasCommander(psCurr) && psCurr->psGroup->psCommander->died <= 1)
	{
		droidObj["commander"] = psCurr->psGroup->psCommander->id;
	}
	if (psCurr->resistance > 0)
	{
		droidObj["resistance"] = psCurr->resistance;
	}
	droidObj["droidType"] = psCurr->droidType;
	droidObj["weapons"] = psCurr->numWeaps;
	nlohmann::json partsObj = nlohmann::json::object();
	partsObj["body"] = (asBodyStats + psCurr->asBits[COMP_BODY])->id;
	partsObj["propulsion"] = (asPropulsionStats + psCurr->asBits[COMP_PROPULSION])->id;
	partsObj["brain"] = (asBrainStats + psCurr->asBits[COMP_BRAIN])->id;
	partsObj["repair"] = (asRepairStats + psCurr->asBits[COMP_REPAIRUNIT])->id;
	partsObj["ecm"] = (asECMStats + psCurr->asBits[COMP_ECM])->id;
	partsObj["sensor"] = (asSensorStats + psCurr->asBits[COMP_SENSOR])->id;
	partsObj["construct"] = (asConstructStats + psCurr->asBits[COMP_CONSTRUCT])->id;
	for (int j = 0; j < psCurr->numWeaps; j++)
	{
		partsObj["weapon/" + WzString::number(j + 1).toStdString()] = (asWeaponStats + psCurr->asWeaps[j].nStat)->id;
	}
	droidObj["parts"] = partsObj;
	droidObj["moveStatus"] = psCurr->sMove.Status;
	droidObj["pathIndex"] = psCurr->sMove.pathIndex;
	droidObj["pathLength"] = psCurr->sMove.asPath.size();
	for (unsigned i = 0; i < psCurr->sMove.asPath.size(); i++)
	{
		droidObj["pathNode/" + WzString::number(i).toStdString()] = psCurr->sMove.asPath[i];
	}
	droidObj["moveDestination"] = psCurr->sMove.destination;
	droidObj["moveSource"] = psCurr->sMove.src;
	droidObj["moveTarget"] = psCurr->sMove.target;
	droidObj["moveSpeed"] = psCurr->sMove.speed;
	droidObj["moveDirection"] = psCurr->sMove.moveDir;
	droidObj["bumpDir"] = psCurr->sMove.bumpDir;
	droidObj["vertSpeed"] = psCurr->sMove.iVertSpeed;
	droidObj["bumpTime"] = psCurr->sMove.bumpTime;
	droidObj["shuffleStart"] = psCurr->sMove.shuffleStart;
	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		droidObj["attackRun/" + WzString::number(i).toStdString()] = psCurr->asWeaps[i].usedAmmo;
	}
	droidObj["lastBump"] = psCurr->sMove.lastBump;
	droidObj["pauseTime"] = psCurr->sMove.pauseTime;
	droidObj["bumpPosition"] = psCurr->sMove.bumpPos.xy();
	droidObj["onMission"] = onMission;
	return droidObj;
}

static bool writeDroidFile(const char *pFileName)
{
	nlohmann::json mRoot = nlohmann::json::object();
	int counter = 0;

	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		for (auto& psCurr : apsDroidLists[player])
		{
			auto droidKey = "droid_" + (WzString::number(counter++).leftPadToMinimumLength(WzUniCodepoint::fromASCII('0'), 10));  // Zero padded so that alphabetical sort works.
			mRoot[droidKey.toStdString()] = writeDroid(&psCurr, false, counter);
			if (isTransporter(&psCurr))	// if transporter save any droids in the grp
			{
				for (auto& psTrans : psCurr.psGroup->psList)
				{
					if (psTrans != &psCurr)
					{
						droidKey = "droid_" + (WzString::number(counter++).leftPadToMinimumLength(WzUniCodepoint::fromASCII('0'), 10));  // Zero padded so that alphabetical sort works.
						mRoot[droidKey.toStdString()] = writeDroid(psTrans, false, counter);
					}
				}
				//always save transporter droids that are in the mission list with an invalid value
			}
		}
	}

	saveJSONToFile(mRoot, pFileName);
	return true;
}


// -----------------------------------------------------------------------------------------
bool loadSaveStructure(char *pFileData, UDWORD filesize)
{
	STRUCT_SAVEHEADER		*psHeader;
	SAVE_STRUCTURE_V2		*psSaveStructure, sSaveStructure;
	STRUCTURE			*psStructure;
	STRUCTURE_STATS			*psStats = nullptr;
	UDWORD				count, statInc;
	int32_t				found;
	UDWORD				NumberOfSkippedStructures = 0;
	UDWORD				periodicalDamageTime;

	/* Check the file type */
	psHeader = (STRUCT_SAVEHEADER *)pFileData;
	if (psHeader->aFileType[0] != 's' || psHeader->aFileType[1] != 't' ||
	    psHeader->aFileType[2] != 'r' || psHeader->aFileType[3] != 'u')
	{
		debug(LOG_ERROR, "loadSaveStructure: Incorrect file type");

		return false;
	}

	/* STRUCT_SAVEHEADER */
	endian_udword(&psHeader->version);
	endian_udword(&psHeader->quantity);

	//increment to the start of the data
	pFileData += STRUCT_HEADER_SIZE;

	debug(LOG_SAVE, "file version is %u ", psHeader->version);

	/* Check the file version */
	if (psHeader->version < VERSION_7 || psHeader->version > VERSION_8)
	{
		debug(LOG_ERROR, "StructLoad: unsupported save format version %d", psHeader->version);

		return false;
	}

	psSaveStructure = &sSaveStructure;

	if ((sizeof(SAVE_STRUCTURE_V2) * psHeader->quantity + STRUCT_HEADER_SIZE) > filesize)
	{
		debug(LOG_ERROR, "structureLoad: unexpected end of file");
		return false;
	}

	/* Load in the structure data */
	for (count = 0; count < psHeader->quantity; count ++, pFileData += sizeof(SAVE_STRUCTURE_V2))
	{
		memcpy(psSaveStructure, pFileData, sizeof(SAVE_STRUCTURE_V2));

		/* STRUCTURE_SAVE_V2 includes OBJECT_SAVE_V19 */
		endian_sdword(&psSaveStructure->currentBuildPts);
		endian_udword(&psSaveStructure->body);
		endian_udword(&psSaveStructure->armour);
		endian_udword(&psSaveStructure->resistance);
		endian_udword(&psSaveStructure->dummy1);
		endian_udword(&psSaveStructure->subjectInc);
		endian_udword(&psSaveStructure->timeStarted);
		endian_udword(&psSaveStructure->output);
		endian_udword(&psSaveStructure->capacity);
		endian_udword(&psSaveStructure->quantity);
		/* OBJECT_SAVE_V19 */
		endian_udword(&psSaveStructure->id);
		endian_udword(&psSaveStructure->x);
		endian_udword(&psSaveStructure->y);
		endian_udword(&psSaveStructure->z);
		endian_udword(&psSaveStructure->direction);
		endian_udword(&psSaveStructure->player);
		endian_udword(&psSaveStructure->periodicalDamageStart);
		endian_udword(&psSaveStructure->periodicalDamage);

		psSaveStructure->player = RemapPlayerNumber(psSaveStructure->player);

		if (psSaveStructure->player >= MAX_PLAYERS)
		{
			psSaveStructure->player = MAX_PLAYERS - 1;
			NumberOfSkippedStructures++;
		}
		//get the stats for this structure
		found = false;

		for (statInc = 0; statInc < numStructureStats; statInc++)
		{
			psStats = asStructureStats + statInc;
			//loop until find the same name

			if (psStats->id.compare(psSaveStructure->name) == 0)
			{
				found = true;
				break;
			}
		}
		//if haven't found the structure - ignore this record!
		if (!found)
		{
			debug(LOG_ERROR, "This structure no longer exists - %s", getSaveStructNameV19((SAVE_STRUCTURE_V17 *)psSaveStructure));
			//ignore this
			continue;
		}

		//for modules - need to check the base structure exists
		if (IsStatExpansionModule(psStats))
		{
			psStructure = getTileStructure(map_coord(psSaveStructure->x), map_coord(psSaveStructure->y));
			if (psStructure == nullptr)
			{
				debug(LOG_ERROR, "No owning structure for module - %s for player - %d", getSaveStructNameV19((SAVE_STRUCTURE_V17 *)psSaveStructure), psSaveStructure->player);
				//ignore this module
				continue;
			}
		}

		//check not trying to build too near the edge
		if (map_coord(psSaveStructure->x) < TOO_NEAR_EDGE || map_coord(psSaveStructure->x) > mapWidth - TOO_NEAR_EDGE)
		{
			debug(LOG_ERROR, "Structure %s, x coord too near the edge of the map. id - %d", getSaveStructNameV19((SAVE_STRUCTURE_V17 *)psSaveStructure), psSaveStructure->id);
			//ignore this
			continue;
		}
		if (map_coord(psSaveStructure->y) < TOO_NEAR_EDGE || map_coord(psSaveStructure->y) > mapHeight - TOO_NEAR_EDGE)
		{
			debug(LOG_ERROR, "Structure %s, y coord too near the edge of the map. id - %d", getSaveStructNameV19((SAVE_STRUCTURE_V17 *)psSaveStructure), psSaveStructure->id);
			//ignore this
			continue;
		}

		psStructure = buildStructureDir(psStats, psSaveStructure->x, psSaveStructure->y, DEG(psSaveStructure->direction), psSaveStructure->player, true);
		ASSERT(psStructure, "Unable to create structure");
		if (!psStructure)
		{
			continue;
		}
		// The original code here didn't work and so the scriptwriters worked round it by using the module ID - so making it work now will screw up
		// the scripts -so in ALL CASES overwrite the ID!
		psStructure->id = psSaveStructure->id > 0 ? psSaveStructure->id : 0xFEDBCA98; // hack to remove struct id zero
		psStructure->periodicalDamage = psSaveStructure->periodicalDamage;
		periodicalDamageTime = psSaveStructure->periodicalDamageStart;
		psStructure->periodicalDamageStart = periodicalDamageTime;
		psStructure->status = (STRUCT_STATES)psSaveStructure->status;
		if (psStructure->status == SS_BUILT)
		{
			buildingComplete(psStructure);
		}
		if (psStructure->pStructureType->type == REF_HQ)
		{
			scriptSetStartPos(psSaveStructure->player, psStructure->pos.x, psStructure->pos.y);
		}
		else if (psStructure->pStructureType->type == REF_RESOURCE_EXTRACTOR)
		{
			scriptSetDerrickPos(psStructure->pos.x, psStructure->pos.y);
		}
	}

	if (NumberOfSkippedStructures > 0)
	{
		debug(LOG_ERROR, "structureLoad: invalid player number in %d structures ... assigned to the last player!\n\n", NumberOfSkippedStructures);
		return false;
	}

	return true;
}

// -----------------------------------------------------------------------------------------
//return id of a research topic based on the name
static UDWORD getResearchIdFromName(const WzString &name)
{
	for (size_t inc = 0; inc < asResearch.size(); inc++)
	{
		if (asResearch[inc].id.compare(name) == 0)
		{
			return inc;
		}
	}
	debug(LOG_ERROR, "Unknown research - %s", name.toUtf8().c_str());
	return NULL_ID;
}

static bool loadWzMapStructure(WzMap::Map& wzMap)
{
	uint32_t NumberOfSkippedStructures = 0;
	auto pStructures = wzMap.mapStructures();
	if (!pStructures)
	{
		return false;
	}

	for (auto &structure : *pStructures) {
		auto psStats = std::find_if(asStructureStats, asStructureStats + numStructureStats, [&](STRUCTURE_STATS &stat) { return stat.id.compare(structure.name.c_str()) == 0; });
		if (psStats == asStructureStats + numStructureStats)
		{
			debug(LOG_ERROR, "Structure type \"%s\" unknown", structure.name.c_str());
			continue;  // ignore this
		}
		//for modules - need to check the base structure exists
		if (IsStatExpansionModule(psStats))
		{
			STRUCTURE *psStructure = getTileStructure(map_coord(structure.position.x), map_coord(structure.position.y));
			if (psStructure == nullptr)
			{
				debug(LOG_ERROR, "No owning structure for module - %s for player - %d", structure.name.c_str(), structure.player);
				continue; // ignore this module
			}
		}
		//check not trying to build too near the edge
		if (map_coord(structure.position.x) < TOO_NEAR_EDGE || map_coord(structure.position.x) > mapWidth - TOO_NEAR_EDGE
		 || map_coord(structure.position.y) < TOO_NEAR_EDGE || map_coord(structure.position.y) > mapHeight - TOO_NEAR_EDGE)
		{
			debug(LOG_ERROR, "Structure %s, coord too near the edge of the map", structure.name.c_str());
			continue; // skip it
		}
		auto player = RemapWzMapPlayerNumber(structure.player);
		if (player >= MAX_PLAYERS)
		{
			player = MAX_PLAYERS - 1;
			NumberOfSkippedStructures++;
		}
		STRUCTURE *psStructure = buildStructureDir(psStats, structure.position.x, structure.position.y, structure.direction, player, true);
		if (psStructure == nullptr)
		{
			debug(LOG_ERROR, "Structure %s couldn't be built (probably on top of another structure).", structure.name.c_str());
			continue;
		}
		if (structure.id.has_value())
		{
			// The original code here didn't work and so the scriptwriters worked round it by using the module ID - so making it work now will screw up
			// the scripts -so in ALL CASES overwrite the ID!
			psStructure->id = structure.id.value() > 0 ? structure.id.value() : 0xFEDBCA98; // hack to remove struct id zero
		}
		if (structure.modules > 0)
		{
			auto moduleStat = getModuleStat(psStructure);
			if (moduleStat == nullptr)
			{
				debug(LOG_ERROR, "Structure %s can't have modules.", structure.name.c_str());
				continue;
			}
			for (int i = 0; i < structure.modules; ++i)
			{
				buildStructure(moduleStat, structure.position.x, structure.position.y, player, true);
			}
		}
		buildingComplete(psStructure);
		if (psStructure->pStructureType->type == REF_HQ)
		{
			scriptSetStartPos(player, psStructure->pos.x, psStructure->pos.y);
		}
		else if (psStructure->pStructureType->type == REF_RESOURCE_EXTRACTOR)
		{
			scriptSetDerrickPos(psStructure->pos.x, psStructure->pos.y);
		}
	}

	if (NumberOfSkippedStructures > 0)
	{
		debug(LOG_ERROR, "structureLoad: invalid player number in %d structures ... assigned to the last player!\n\n", NumberOfSkippedStructures);
		return false;
	}

	return true;
}

// -----------------------------------------------------------------------------------------
/* code for versions after version 20 of a save structure */
static bool loadSaveStructure2(const char *pFileName)
{
	if (!PHYSFS_exists(pFileName))
	{
		debug(LOG_SAVE, "No %s found -- use fallback method", pFileName);
		return false;	// try to use fallback method
	}
	WzConfig ini(WzString::fromUtf8(pFileName), WzConfig::ReadOnly);

	freeAllFlagPositions();		//clear any flags put in during level loads

	std::vector<WzString> list = ini.childGroups();
	for (size_t i = 0; i < list.size(); ++i)
	{
		FACTORY *psFactory;
		RESEARCH_FACILITY *psResearch;
		REPAIR_FACILITY *psRepair;
		REARM_PAD *psReArmPad;
		STRUCTURE_STATS *psModule;
		int capacity, researchId;
		STRUCTURE *psStructure;

		ini.beginGroup(list[i]);
		int player = getPlayer(ini);
		int id = ini.value("id", -1).toInt();
		Position pos = ini.vector3i("position");
		Rotation rot = ini.vector3i("rotation");
		WzString name = ini.string("name");

		//get the stats for this structure
		auto psStats = std::find_if(asStructureStats, asStructureStats + numStructureStats, [&](STRUCTURE_STATS &stat) { return stat.id == name; });
		//if haven't found the structure - ignore this record!
		ASSERT(psStats != asStructureStats + numStructureStats, "This structure no longer exists - %s", name.toUtf8().c_str());
		if (psStats == asStructureStats + numStructureStats)
		{
			ini.endGroup();
			continue;	// ignore this
		}
		/*create the Structure */
		//for modules - need to check the base structure exists
		if (IsStatExpansionModule(psStats))
		{
			STRUCTURE *psTileStructure = getTileStructure(map_coord(pos.x), map_coord(pos.y));
			if (psTileStructure == nullptr)
			{
				debug(LOG_ERROR, "No owning structure for module - %s for player - %d", name.toUtf8().c_str(), player);
				ini.endGroup();
				continue; // ignore this module
			}
		}
		//check not trying to build too near the edge
		if (map_coord(pos.x) < TOO_NEAR_EDGE || map_coord(pos.x) > mapWidth - TOO_NEAR_EDGE
		    || map_coord(pos.y) < TOO_NEAR_EDGE || map_coord(pos.y) > mapHeight - TOO_NEAR_EDGE)
		{
			debug(LOG_ERROR, "Structure %s (%s), coord too near the edge of the map", name.toUtf8().c_str(), list[i].toUtf8().c_str());
			ini.endGroup();
			continue; // skip it
		}
		psStructure = buildStructureDir(psStats, pos.x, pos.y, rot.direction, player, true);
		ASSERT(psStructure, "Unable to create structure");
		if (!psStructure)
		{
			ini.endGroup();
			continue;
		}
		if (id > 0)
		{
			psStructure->id = id;	// force correct ID
		}

		// common BASE_OBJECT info
		loadSaveObject(ini, psStructure);

		if (psStructure->pStructureType->type == REF_HQ)
		{
			scriptSetStartPos(player, psStructure->pos.x, psStructure->pos.y);
		}
		psStructure->resistance = ini.value("resistance", psStructure->resistance).toInt();
		capacity = ini.value("modules", 0).toInt();
		psStructure->capacity = 0; // increased when modules are built
		switch (psStructure->pStructureType->type)
		{
		case REF_FACTORY:
		case REF_VTOL_FACTORY:
		case REF_CYBORG_FACTORY:
			//if factory save the current build info
			psFactory = ((FACTORY *)psStructure->pFunctionality);
			psFactory->productionLoops = ini.value("Factory/productionLoops", psFactory->productionLoops).toUInt();
			psFactory->timeStarted = ini.value("Factory/timeStarted", psFactory->timeStarted).toInt();
			psFactory->buildPointsRemaining = ini.value("Factory/buildPointsRemaining", psFactory->buildPointsRemaining).toInt();
			psFactory->timeStartHold = ini.value("Factory/timeStartHold", psFactory->timeStartHold).toInt();
			psFactory->loopsPerformed = ini.value("Factory/loopsPerformed", psFactory->loopsPerformed).toInt();
			// statusPending and pendingCount belong to the GUI, not the game state.
			psFactory->secondaryOrder = ini.value("Factory/secondaryOrder", psFactory->secondaryOrder).toInt();
			//adjust the module structures IMD
			if (capacity)
			{
				psModule = getModuleStat(psStructure);
				//build the appropriate number of modules
				for (int moduleIdx = 0; moduleIdx < capacity; moduleIdx++)
				{
					buildStructure(psModule, psStructure->pos.x, psStructure->pos.y, psStructure->player, true);
				}
			}
			if (ini.contains("Factory/template"))
			{
				int templId(ini.value("Factory/template").toInt());
				psFactory->psSubject = getTemplateFromMultiPlayerID(templId);
			}
			if (ini.contains("Factory/assemblyPoint/pos"))
			{
				Position point = ini.vector3i("Factory/assemblyPoint/pos");
				setAssemblyPoint(psFactory->psAssemblyPoint, point.x, point.y, player, true);
				psFactory->psAssemblyPoint->selected = ini.value("Factory/assemblyPoint/selected", false).toBool();
			}
			if (ini.contains("Factory/assemblyPoint/number"))
			{
				psFactory->psAssemblyPoint->factoryInc = ini.value("Factory/assemblyPoint/number", 42).toInt();
			}
			if (player == productionPlayer)
			{
				for (int runNum = 0; runNum < ini.value("Factory/productionRuns", 0).toInt(); runNum++)
				{
					ProductionRunEntry currentProd;
					currentProd.quantity = ini.value("Factory/Run/" + WzString::number(runNum) + "/quantity").toInt();
					currentProd.built = ini.value("Factory/Run/" + WzString::number(runNum) + "/built").toInt();
					if (ini.contains("Factory/Run/" + WzString::number(runNum) + "/template"))
					{
						int tid = ini.value("Factory/Run/" + WzString::number(runNum) + "/template").toInt();
						DROID_TEMPLATE *psTempl = getTemplateFromMultiPlayerID(tid);
						currentProd.psTemplate = psTempl;
						ASSERT(psTempl, "No template found for template ID %d for %s (%d)", tid, objInfo(psStructure), id);
					}
					if (psFactory->psAssemblyPoint->factoryInc >= asProductionRun[psFactory->psAssemblyPoint->factoryType].size())
					{
						asProductionRun[psFactory->psAssemblyPoint->factoryType].resize(psFactory->psAssemblyPoint->factoryInc + 1);
					}
					asProductionRun[psFactory->psAssemblyPoint->factoryType][psFactory->psAssemblyPoint->factoryInc].push_back(currentProd);
				}
			}
			break;
		case REF_RESEARCH:
			psResearch = ((RESEARCH_FACILITY *)psStructure->pFunctionality);
			//adjust the module structures IMD
			if (capacity)
			{
				psModule = getModuleStat(psStructure);
				buildStructure(psModule, psStructure->pos.x, psStructure->pos.y, psStructure->player, true);
			}
			//clear subject
			psResearch->psSubject = nullptr;
			psResearch->timeStartHold = 0;
			//set the subject
			if (ini.contains("Research/target"))
			{
				researchId = getResearchIdFromName(ini.value("Research/target").toWzString());
				if (researchId != NULL_ID)
				{
					psResearch->psSubject = &asResearch[researchId];
					psResearch->timeStartHold = ini.value("Research/timeStartHold").toInt();
				}
				else
				{
					debug(LOG_ERROR, "Failed to look up research target %s", ini.value("Research/target").toWzString().toUtf8().c_str());
				}
			}
			break;
		case REF_POWER_GEN:
			// adjust the module structures IMD
			if (capacity)
			{
				psModule = getModuleStat(psStructure);
				buildStructure(psModule, psStructure->pos.x, psStructure->pos.y, psStructure->player, true);
			}
			break;
		case REF_RESOURCE_EXTRACTOR:
			break;
		case REF_REPAIR_FACILITY:
			psRepair = ((REPAIR_FACILITY *)psStructure->pFunctionality);
			if (ini.contains("Repair/deliveryPoint/pos"))
			{
				Position point = ini.vector3i("Repair/deliveryPoint/pos");
				setAssemblyPoint(psRepair->psDeliveryPoint, point.x, point.y, player, true);
				psRepair->psDeliveryPoint->selected = ini.value("Repair/deliveryPoint/selected", false).toBool();
			}
			break;
		case REF_REARM_PAD:
			psReArmPad = ((REARM_PAD *)psStructure->pFunctionality);
			psReArmPad->timeStarted = ini.value("Rearm/timeStarted", psReArmPad->timeStarted).toInt();
			psReArmPad->timeLastUpdated = ini.value("Rearm/timeLastUpdated", psReArmPad->timeLastUpdated).toInt();
			break;
		case REF_WALL:
		case REF_GATE:
			psStructure->pFunctionality->wall.type = ini.value("Wall/type").toInt();
			psStructure->sDisplay.imd = psStructure->pStructureType->pIMD[std::min<unsigned>(psStructure->pFunctionality->wall.type, psStructure->pStructureType->pIMD.size() - 1)];
			break;
		default:
			break;
		}
		psStructure->body = healthValue(ini, structureBody(psStructure));
		psStructure->currentBuildPts = ini.value("currentBuildPts", structureBuildPointsToCompletion(*psStructure)).toInt();
		if (psStructure->status == SS_BUILT)
		{
			switch (psStructure->pStructureType->type)
			{
			case REF_POWER_GEN:
				checkForResExtractors(psStructure);
				if (selectedPlayer == psStructure->player)
				{
					audio_PlayObjStaticTrack(psStructure, ID_SOUND_POWER_HUM);
				}
				break;
			case REF_RESOURCE_EXTRACTOR:
				checkForPowerGen(psStructure);
				break;
			default:
				//do nothing for factories etc
				break;
			}
		}
		// weapons
		for (int j = 0; j < psStructure->pStructureType->numWeaps; j++)
		{
			if (psStructure->asWeaps[j].nStat > 0)
			{
				psStructure->asWeaps[j].ammo = ini.value("ammo/" + WzString::number(j)).toInt();
				psStructure->asWeaps[j].lastFired = ini.value("lastFired/" + WzString::number(j)).toInt();
				psStructure->asWeaps[j].shotsFired = ini.value("shotsFired/" + WzString::number(j)).toInt();
				psStructure->asWeaps[j].rot = ini.vector3i("rotation/" + WzString::number(j));
			}
		}
		psStructure->status = (STRUCT_STATES)ini.value("status", SS_BUILT).toInt();
		if (psStructure->status == SS_BUILT)
		{
			buildingComplete(psStructure);
		}
		ini.endGroup();
	}
	resetFactoryNumFlag();	//reset flags into the masks

	return true;
}

// -----------------------------------------------------------------------------------------
/*
Writes some version info
*/
bool writeGameInfo(const char *pFileName)
{
	const DebugInputManager& dbgInputManager = gInputManager.debugManager();

	WzConfig ini(WzString::fromUtf8(pFileName), WzConfig::ReadAndWrite);
	char ourtime[100] = {'\0'};
	const time_t currentTime = time(nullptr);
	std::string time(ctime(&currentTime));

	ini.beginGroup("GameProperties");
	ini.setValue("current_time", time.data());
	getAsciiTime(ourtime, graphicsTime);
	ini.setValue("graphics_time", ourtime);
	getAsciiTime(ourtime, gameTime);
	ini.setValue("game_time", ourtime);
	getAsciiTime(ourtime, gameTime - missionData.missionStarted);
	ini.setValue("playing_time", ourtime);
	ini.setValue("version", version_getVersionString());
	ini.setValue("full_version", version_getFormattedVersionString());
	ini.setValue("cheated", false);
	ini.setValue("debug", dbgInputManager.debugMappingsAllowed());
	ini.setValue("level/map", getLevelName());
	ini.setValue("mods", !getModList().empty() ? getModList().c_str() : "None");
	auto backendInfo = gfx_api::context::get().getBackendGameInfo();
	for (auto& kv : backendInfo)
	{
		ini.setValue(WzString::fromUtf8(kv.first), WzString::fromUtf8(kv.second));
	}
	ini.endGroup();
	return true;
}

/*
Writes the linked list of structure for each player to a file
*/
bool writeStructFile(const char *pFileName)
{
	WzConfig ini(WzString::fromUtf8(pFileName), WzConfig::ReadAndWrite);
	int counter = 0;

	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		for (auto& psCurr : apsStructLists[player])
		{
			ini.beginGroup("structure_" + (WzString::number(counter++).leftPadToMinimumLength(WzUniCodepoint::fromASCII('0'), 10)));  // Zero padded so that alphabetical sort works.
			ini.setValue("name", psCurr.pStructureType->id);

			writeSaveObject(ini, &psCurr);

			if (psCurr.resistance > 0) {
				ini.setValue("resistance", psCurr.resistance);
			}
			if (psCurr.status != SS_BUILT) {
				ini.setValue("status", psCurr.status);
			}
			ini.setValue("weapons", psCurr.numWeaps);
			for (unsigned j = 0; j < psCurr.numWeaps; j++)
			{
				ini.setValue("parts/weapon/" + WzString::number(j + 1), (asWeaponStats + psCurr.asWeaps[j].nStat)->id);
				if (psCurr.asWeaps[j].nStat > 0)
				{
					ini.setValue("ammo/" + WzString::number(j), psCurr.asWeaps[j].ammo);
					ini.setValue("lastFired/" + WzString::number(j), psCurr.asWeaps[j].lastFired);
					ini.setValue("shotsFired/" + WzString::number(j), psCurr.asWeaps[j].shotsFired);
					ini.setVector3i("rotation/" + WzString::number(j), toVector(psCurr.asWeaps[j].rot));
				}
			}
			for (unsigned i = 0; i < psCurr.numWeaps; i++)
			{
				if (psCurr.psTarget[i] && !psCurr.psTarget[i]->died)
				{
					ini.setValue("target/" + WzString::number(i) + "/id", psCurr.psTarget[i]->id);
					ini.setValue("target/" + WzString::number(i) + "/player", psCurr.psTarget[i]->player);
					ini.setValue("target/" + WzString::number(i) + "/type", psCurr.psTarget[i]->type);
#ifdef DEBUG
					ini.setValue("target/" + WzString::number(i) + "/debugfunc", WzString::fromUtf8(psCurr->targetFunc[i]));
					ini.setValue("target/" + WzString::number(i) + "/debugline", psCurr->targetLine[i]);
#endif
				}
			}
			ini.setValue("currentBuildPts", psCurr.currentBuildPts);
			if (psCurr.pFunctionality)
			{
				if (psCurr.pStructureType->type == REF_FACTORY || psCurr.pStructureType->type == REF_CYBORG_FACTORY
				    || psCurr.pStructureType->type == REF_VTOL_FACTORY)
				{
					FACTORY *psFactory = (FACTORY *)psCurr.pFunctionality;
					ini.setValue("modules", psCurr.capacity);
					ini.setValue("Factory/productionLoops", psFactory->productionLoops);
					ini.setValue("Factory/timeStarted", psFactory->timeStarted);
					ini.setValue("Factory/buildPointsRemaining", psFactory->buildPointsRemaining);
					ini.setValue("Factory/timeStartHold", psFactory->timeStartHold);
					ini.setValue("Factory/loopsPerformed", psFactory->loopsPerformed);
					// statusPending and pendingCount belong to the GUI, not the game state.
					ini.setValue("Factory/secondaryOrder", psFactory->secondaryOrder);

					if (psFactory->psSubject != nullptr)
					{
						ini.setValue("Factory/template", psFactory->psSubject->multiPlayerID);
					}
					FLAG_POSITION *psFlag = ((FACTORY *)psCurr.pFunctionality)->psAssemblyPoint;
					if (psFlag != nullptr)
					{
						ini.setVector3i("Factory/assemblyPoint/pos", psFlag->coords);
						if (psFlag->selected)
						{
							ini.setValue("Factory/assemblyPoint/selected", psFlag->selected);
						}
						ini.setValue("Factory/assemblyPoint/number", psFlag->factoryInc);
					}
					if (psFactory->psCommander)
					{
						ini.setValue("Factory/commander/id", psFactory->psCommander->id);
						ini.setValue("Factory/commander/player", psFactory->psCommander->player);
					}
					ini.setValue("Factory/secondaryOrder", psFactory->secondaryOrder);
					if (player == productionPlayer)
					{
						ProductionRun emptyRun;
						bool haveRun = psFactory->psAssemblyPoint->factoryInc < asProductionRun[psFactory->psAssemblyPoint->factoryType].size();
						ProductionRun const &productionRun = haveRun ? asProductionRun[psFactory->psAssemblyPoint->factoryType][psFactory->psAssemblyPoint->factoryInc] : emptyRun;
						ini.setValue("Factory/productionRuns", (int)productionRun.size());
						for (size_t runNum = 0; runNum < productionRun.size(); runNum++)
						{
							ProductionRunEntry psCurrentProd = productionRun.at(runNum);
							ini.setValue("Factory/Run/" + WzString::number(runNum) + "/quantity", psCurrentProd.quantity);
							ini.setValue("Factory/Run/" + WzString::number(runNum) + "/built", psCurrentProd.built);
							if (psCurrentProd.psTemplate) ini.setValue("Factory/Run/" + WzString::number(runNum) + "/template",
										psCurrentProd.psTemplate->multiPlayerID);
						}
					}
					else
					{
						ini.setValue("Factory/productionRuns", 0);
					}
				}
				else if (psCurr.pStructureType->type == REF_RESEARCH)
				{
					ini.setValue("modules", psCurr.capacity);
					ini.setValue("Research/timeStartHold", ((RESEARCH_FACILITY *)psCurr.pFunctionality)->timeStartHold);
					if (((RESEARCH_FACILITY *)psCurr.pFunctionality)->psSubject)
					{
						ini.setValue("Research/target", ((RESEARCH_FACILITY *)psCurr.pFunctionality)->psSubject->id);
					}
				}
				else if (psCurr.pStructureType->type == REF_POWER_GEN)
				{
					ini.setValue("modules", psCurr.capacity);
				}
				else if (psCurr.pStructureType->type == REF_REPAIR_FACILITY)
				{
					REPAIR_FACILITY *psRepair = ((REPAIR_FACILITY *)psCurr.pFunctionality);
					if (psRepair->psObj)
					{
						ini.setValue("Repair/target/id", psRepair->psObj->id);
						ini.setValue("Repair/target/player", psRepair->psObj->player);
						ini.setValue("Repair/target/type", psRepair->psObj->type);
					}
					FLAG_POSITION *psFlag = psRepair->psDeliveryPoint;
					if (psFlag)
					{
						ini.setVector3i("Repair/deliveryPoint/pos", psFlag->coords);
						if (psFlag->selected)
						{
							ini.setValue("Repair/deliveryPoint/selected", psFlag->selected);
						}
					}
				}
				else if (psCurr.pStructureType->type == REF_REARM_PAD)
				{
					REARM_PAD *psReArmPad = ((REARM_PAD *)psCurr.pFunctionality);
					ini.setValue("Rearm/timeStarted", psReArmPad->timeStarted);
					ini.setValue("Rearm/timeLastUpdated", psReArmPad->timeLastUpdated);
					if (psReArmPad->psObj)
					{
						ini.setValue("Rearm/target/id", psReArmPad->psObj->id);
						ini.setValue("Rearm/target/player", psReArmPad->psObj->player);
						ini.setValue("Rearm/target/type", psReArmPad->psObj->type);
					}
				}
				else if (psCurr.pStructureType->type == REF_WALL || psCurr.pStructureType->type == REF_GATE)
				{
					ini.setValue("Wall/type", psCurr.pFunctionality->wall.type);
				}
			}
			ini.endGroup();
		}
	}
	return true;
}

bool loadSaveStructurePointers(const WzString& filename)
{
	WzConfig ini(filename, WzConfig::ReadOnly);
	std::vector<WzString> list = ini.childGroups();

	for (size_t i = 0; i < list.size(); ++i)
	{
		ini.beginGroup(list[i]);
		int player = getPlayer(ini);
		int id = ini.value("id", -1).toInt();
		for (auto& psStruct : apsStructLists[player]) {
      if (psStruct.id == id) {
        for (int j = 0; j < MAX_WEAPONS; j++)
        {
          objTrace(psStruct.id, "weapon %d, nStat %d", j, psStruct.asWeaps[j].nStat);
          if (ini.contains("target/" + WzString::number(j) + "/id"))
          {
            int tid = ini.value("target/" + WzString::number(j) + "/id", -1).toInt();
            int tplayer = ini.value("target/" + WzString::number(j) + "/player", -1).toInt();
            OBJECT_TYPE ttype = (OBJECT_TYPE)ini.value("target/" + WzString::number(j) + "/type", 0).toInt();
            ASSERT(tid >= 0 && tplayer >= 0, "Bad ID");
            setStructureTarget(&psStruct, getBaseObjFromData(tid, tplayer, ttype), j, ORIGIN_UNKNOWN);
            ASSERT(psStruct.psTarget[j], "Failed to find target");
          }
        }
        if (ini.contains("Factory/commander/id")) {
          ASSERT(psStruct.pStructureType->type == REF_FACTORY || psStruct.pStructureType->type == REF_CYBORG_FACTORY
                 || psStruct.pStructureType->type == REF_VTOL_FACTORY, "Bad type");
          FACTORY *psFactory = (FACTORY *)psStruct.pFunctionality;
          OBJECT_TYPE ttype = OBJ_DROID;
          int tid = ini.value("Factory/commander/id", -1).toInt();
          int tplayer = ini.value("Factory/commander/player", -1).toInt();
          ASSERT(tid >= 0 && tplayer >= 0, "Bad commander ID %d for player %d for building %d", tid, tplayer, id);
          DROID *psCommander = (DROID *)getBaseObjFromData(tid, tplayer, ttype);
          ASSERT(psCommander, "Commander %d not found for building %d", tid, id);
          assignFactoryCommandDroid(&psStruct, psCommander);
        }
        if (ini.contains("Repair/target/id")){
          ASSERT(psStruct.pStructureType->type == REF_REPAIR_FACILITY, "Bad type");
          REPAIR_FACILITY *psRepair = ((REPAIR_FACILITY *)psStruct.pFunctionality);
          OBJECT_TYPE ttype = (OBJECT_TYPE)ini.value("Repair/target/type", OBJ_DROID).toInt();
          int tid = ini.value("Repair/target/id", -1).toInt();
          int tplayer = ini.value("Repair/target/player", -1).toInt();
          ASSERT(tid >= 0 && tplayer >= 0, "Bad repair ID %d for player %d for building %d", tid, tplayer, id);
          psRepair->psObj = getBaseObjFromData(tid, tplayer, ttype);
          ASSERT(psRepair->psObj, "Repair target %d not found for building %d", tid, id);
        }
        if (ini.contains("Rearm/target/id")) {
          ASSERT(psStruct.pStructureType->type == REF_REARM_PAD, "Bad type");
          REARM_PAD *psReArmPad = ((REARM_PAD *)psStruct.pFunctionality);
          OBJECT_TYPE ttype = OBJ_DROID; // always, for now
          int tid = ini.value("Rearm/target/id", -1).toInt();
          int tplayer = ini.value("Rearm/target/player", -1).toInt();
          ASSERT(tid >= 0 && tplayer >= 0, "Bad rearm ID %d for player %d for building %d", tid, tplayer, id);
          psReArmPad->psObj = getBaseObjFromData(tid, tplayer, ttype);
          ASSERT(psReArmPad->psObj, "Rearm target %d not found for building %d", tid, id);
        }

        ini.endGroup();

      }
    }
    ini.endGroup();
    continue;	// it is not unusual for a structure to 'disappear' like this; it can happen eg because of module upgrades


	}
	return true;
}

// -----------------------------------------------------------------------------------------
bool loadSaveFeature(char *pFileData, UDWORD filesize)
{
	FEATURE_SAVEHEADER		*psHeader;
	SAVE_FEATURE_V14			*psSaveFeature;
	FEATURE					*pFeature;
	UDWORD					count, i, statInc;
	FEATURE_STATS			*psStats = nullptr;
	bool					found;
	UDWORD					sizeOfSaveFeature;

	/* Check the file type */
	psHeader = (FEATURE_SAVEHEADER *)pFileData;
	if (psHeader->aFileType[0] != 'f' || psHeader->aFileType[1] != 'e' ||
	    psHeader->aFileType[2] != 'a' || psHeader->aFileType[3] != 't')
	{
		debug(LOG_ERROR, "loadSaveFeature: Incorrect file type");
		return false;
	}

	/* FEATURE_SAVEHEADER */
	endian_udword(&psHeader->version);
	endian_udword(&psHeader->quantity);

	debug(LOG_SAVE, "Feature file version is %u ", psHeader->version);

	//increment to the start of the data
	pFileData += FEATURE_HEADER_SIZE;

	/* Check the file version */
	if (psHeader->version < VERSION_7 || psHeader->version > VERSION_19)
	{
		debug(LOG_ERROR, "Unsupported save format version %u", psHeader->version);
		return false;
	}
	if (psHeader->version < VERSION_14)
	{
		sizeOfSaveFeature = sizeof(SAVE_FEATURE_V2);
	}
	else
	{
		sizeOfSaveFeature = sizeof(SAVE_FEATURE_V14);
	}
	if ((sizeOfSaveFeature * psHeader->quantity + FEATURE_HEADER_SIZE) > filesize)
	{
		debug(LOG_ERROR, "featureLoad: unexpected end of file");
		return false;
	}

	/* Load in the feature data */
	for (count = 0; count < psHeader->quantity; count ++, pFileData += sizeOfSaveFeature)
	{
		psSaveFeature = (SAVE_FEATURE_V14 *) pFileData;

		/* FEATURE_SAVE_V14 is FEATURE_SAVE_V2 */
		/* FEATURE_SAVE_V2 is OBJECT_SAVE_V19 */
		/* OBJECT_SAVE_V19 */
		endian_udword(&psSaveFeature->id);
		endian_udword(&psSaveFeature->x);
		endian_udword(&psSaveFeature->y);
		endian_udword(&psSaveFeature->z);
		endian_udword(&psSaveFeature->direction);
		endian_udword(&psSaveFeature->player);
		endian_udword(&psSaveFeature->periodicalDamageStart);
		endian_udword(&psSaveFeature->periodicalDamage);

		//get the stats for this feature
		found = false;

		for (statInc = 0; statInc < numFeatureStats; statInc++)
		{
			psStats = asFeatureStats + statInc;
			//loop until find the same name
			if (psStats->id.compare(psSaveFeature->name) == 0)
			{
				found = true;
				break;
			}
		}
		//if haven't found the feature - ignore this record!
		if (!found)
		{
			debug(LOG_ERROR, "This feature no longer exists - %s", psSaveFeature->name);
			//ignore this
			continue;
		}
		//create the Feature
		pFeature = buildFeature(psStats, psSaveFeature->x, psSaveFeature->y, true);
		if (!pFeature)
		{
			debug(LOG_ERROR, "Unable to create feature %s", psSaveFeature->name);
			continue;
		}
		if (pFeature->psStats->subType == FEAT_OIL_RESOURCE)
		{
			scriptSetDerrickPos(pFeature->pos.x, pFeature->pos.y);
		}
		//restore values
		pFeature->id = psSaveFeature->id;
		pFeature->rot.direction = DEG(psSaveFeature->direction);
		pFeature->periodicalDamage = psSaveFeature->periodicalDamage;
		if (psHeader->version >= VERSION_14)
		{
			for (i = 0; i < MAX_PLAYERS; i++)
			{
				pFeature->visible[i] = psSaveFeature->visible[i];
			}
		}
	}

	return true;
}

static bool loadWzMapFeature(WzMap::Map &wzMap)
{
	auto pFeatures = wzMap.mapFeatures();
	if (!pFeatures)
	{
		return false;
	}

	for (auto &feature : *pFeatures)
	{
		auto psStats = std::find_if(asFeatureStats, asFeatureStats + numFeatureStats, [&](FEATURE_STATS &stat) { return stat.id.compare(feature.name.c_str()) == 0; });
		if (psStats == asFeatureStats + numFeatureStats)
		{
			debug(LOG_ERROR, "Feature type \"%s\" unknown", feature.name.c_str());
			continue;  // ignore this
		}
		// Create the Feature
		auto pFeature = buildFeature(psStats, feature.position.x, feature.position.y, true);
		if (!pFeature)
		{
			debug(LOG_ERROR, "Unable to create feature %s", feature.name.c_str());
			continue;
		}
		if (pFeature->psStats->subType == FEAT_OIL_RESOURCE)
		{
			scriptSetDerrickPos(pFeature->pos.x, pFeature->pos.y);
		}
		//restore values
		if (feature.id.has_value())
		{
			pFeature->id = feature.id.value();
		}
		else
		{
			pFeature->id = generateSynchronisedObjectId();
		}
		pFeature->rot.direction = feature.direction;
		pFeature->player = (feature.player.has_value()) ? feature.player.value() : PLAYER_FEATURE;
	}

	return true;
}

bool loadSaveFeature2(const char *pFileName)
{
	if (!PHYSFS_exists(pFileName))
	{
		debug(LOG_SAVE, "No %s found -- use fallback method", pFileName);
		return false;
	}
	WzConfig ini(pFileName, WzConfig::ReadOnly);
	std::vector<WzString> list = ini.childGroups();
	debug(LOG_SAVE, "Loading new style features (%zu found)", list.size());

	for (size_t i = 0; i < list.size(); ++i)
	{
		FEATURE *pFeature;
		ini.beginGroup(list[i]);
		WzString name = ini.string("name");
		Position pos = ini.vector3i("position");
		int statInc;
		bool found = false;
		FEATURE_STATS *psStats = nullptr;

		//get the stats for this feature
		for (statInc = 0; statInc < numFeatureStats; statInc++)
		{
			psStats = asFeatureStats + statInc;
			//loop until find the same name
			if (psStats->id.compare(name) == 0)
			{
				found = true;
				break;
			}
		}
		//if haven't found the feature - ignore this record!
		if (!found)
		{
			debug(LOG_ERROR, "This feature no longer exists - %s", name.toUtf8().c_str());
			//ignore this
			continue;
		}
		//create the Feature
		pFeature = buildFeature(psStats, pos.x, pos.y, true);
		if (!pFeature)
		{
			debug(LOG_ERROR, "Unable to create feature %s", name.toUtf8().c_str());
			continue;
		}
		if (pFeature->psStats->subType == FEAT_OIL_RESOURCE)
		{
			scriptSetDerrickPos(pFeature->pos.x, pFeature->pos.y);
		}
		//restore values
		int id = ini.value("id", -1).toInt();
		if (id > 0)
		{
			pFeature->id = id;
		}
		else
		{
			pFeature->id = generateSynchronisedObjectId();
		}
		pFeature->rot = ini.vector3i("rotation");
		pFeature->player = ini.value("player", PLAYER_FEATURE).toInt();

		// common BASE_OBJECT info
		loadSaveObject(ini, pFeature);

		pFeature->body = healthValue(ini, pFeature->psStats->body);

		ini.endGroup();
	}
	return true;
}

// -----------------------------------------------------------------------------------------
/*
Writes the linked list of features to a file
*/
bool writeFeatureFile(const char *pFileName)
{
	WzConfig ini(WzString::fromUtf8(pFileName), WzConfig::ReadAndWrite);
	int counter = 0;

	for (auto& psCurr : apsFeatureLists)
	{
		ini.beginGroup("feature_" + (WzString::number(counter++).leftPadToMinimumLength(WzUniCodepoint::fromASCII('0'), 10)));  // Zero padded so that alphabetical sort works.
		ini.setValue("name", psCurr.psStats->id);
		writeSaveObject(ini, &psCurr);
		ini.endGroup();
	}
	return true;
}

bool loadSaveTemplate(const char *pFileName)
{
	WzConfig ini(WzString::fromUtf8(pFileName), WzConfig::ReadOnly);
	std::vector<WzString> list = ini.childGroups();

	auto loadTemplate = [&]() {
		DROID_TEMPLATE t;
		if (!loadTemplateCommon(ini, t))
		{
			debug(LOG_ERROR, "Stored template \"%s\" contains an unknown component.", ini.string("name").toUtf8().c_str());
		}
		t.name = ini.string("name");
		t.multiPlayerID = ini.value("multiPlayerID", generateNewObjectId()).toInt();
		t.enabled = ini.value("enabled", false).toBool();
		t.stored = ini.value("stored", false).toBool();
		t.prefab = ini.value("prefab", false).toBool();
		ini.nextArrayItem();
		return t;
	};

	int version = ini.value("version", 0).toInt();
	if (version == 0)
	{
		return false;
	}
	for (size_t i = 0; i < list.size(); ++i)
	{
		ini.beginGroup(list[i]);
		int player = getPlayer(ini);
		ini.beginArray("templates");
		while (ini.remainingArrayItems() > 0)
		{
			addTemplate(player, std::unique_ptr<DROID_TEMPLATE>(new DROID_TEMPLATE(loadTemplate())));
		}
		ini.endArray();
		ini.endGroup();
	}

	if (ini.contains("localTemplates"))
	{
		ini.beginArray("localTemplates");
		while (ini.remainingArrayItems() > 0)
		{
			localTemplates.emplace_back(loadTemplate());
		}
		ini.endArray();
	}
	else
	{
		// Old savegame compatibility, should remove this branch sometime.
		enumerateTemplates(selectedPlayer, [](DROID_TEMPLATE * psTempl) {
			localTemplates.push_back(*psTempl);
			return true;
		});
	}

	return true;
}

static nlohmann::json convGameTemplateToJSON(DROID_TEMPLATE *psCurr)
{
	nlohmann::json templateObj = saveTemplateCommon(psCurr);
	templateObj["ref"] = psCurr->ref;
	templateObj["multiPlayerID"] = psCurr->multiPlayerID;
	templateObj["enabled"] = psCurr->enabled;
	templateObj["stored"] = psCurr->stored;
	templateObj["prefab"] = psCurr->prefab;
	return templateObj;
}

bool writeTemplateFile(const char *pFileName)
{
	nlohmann::json mRoot = nlohmann::json::object();

	mRoot["version"] = 1;
	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		if (apsDroidLists[player].empty() && apsStructLists[player].empty())	// only write out templates of players that are still 'alive'
		{
			continue;
		}
		nlohmann::json playerTemplatesObj = nlohmann::json::object();
		setPlayerJSON(playerTemplatesObj, player);
		nlohmann::json templates_array = nlohmann::json::array();
		enumerateTemplates(player, [&templates_array](DROID_TEMPLATE* psTemplate) {
			templates_array.push_back(convGameTemplateToJSON(psTemplate));
			return true;
		});
		playerTemplatesObj["templates"] = std::move(templates_array);
		auto playerKey = "player_" + WzString::number(player);
		mRoot[playerKey.toUtf8()] = std::move(playerTemplatesObj);
	}
	nlohmann::json localtemplates_array = nlohmann::json::array();
	for (auto &psCurr : localTemplates)
	{
		localtemplates_array.push_back(convGameTemplateToJSON(&psCurr));
	}
	mRoot["localTemplates"] = std::move(localtemplates_array);

	saveJSONToFile(mRoot, pFileName);
	return true;
}

// -----------------------------------------------------------------------------------------
// load up a terrain tile type map file
bool loadTerrainTypeMap(const char *pFilePath)
{
	ASSERT_OR_RETURN(false, pFilePath, "Null pFilePath");
	WzMapDebugLogger logger;
	WzMapPhysFSIO mapIO;
	auto result = WzMap::loadTerrainTypes(pFilePath, mapIO, &logger);
	if (!result)
	{
		// Failed to load terrain type map data
		return false;
	}

	// reset the terrain table
	memset(terrainTypes, 0, sizeof(terrainTypes));

	size_t quantity = result->terrainTypes.size();
	if (quantity >= MAX_TILE_TEXTURES)
	{
		// Workaround for fugly map editor bug, since we can't fix the map editor
		quantity = MAX_TILE_TEXTURES - 1;
	}
	for (size_t i = 0; i < quantity; i++)
	{
		auto& type = result->terrainTypes[i];
		if (type > TER_MAX)
		{
			debug(LOG_ERROR, "loadTerrainTypeMap: terrain type out of range");
			return false;
		}

		terrainTypes[i] = static_cast<UBYTE>(type);
	}

	return true;
}

bool loadTerrainTypeMapOverride(unsigned int tileSet)
{
	resForceBaseDir("/data/base/");
	WzString iniName = "tileset/tileTypes.json";
	if (!PHYSFS_exists(iniName.toUtf8().c_str()))
	{
		return false;
	}

	WzConfig ini(iniName, WzConfig::ReadOnly);
	WzString tileTypeKey;

	if (tileSet == ARIZONA)
	{
		tileTypeKey = "Arizona";
	}
	else if (tileSet == URBAN)
	{
		tileTypeKey = "Urban";
	}
	else if (tileSet == ROCKIE)
	{
		tileTypeKey = "Rockies";
	}
	else
	{
		debug(LOG_ERROR, "Unknown tile type");
		resForceBaseDir("");
		return false;
	}

	std::vector<WzString> list = ini.childGroups();
	for (size_t i = 0; i < list.size(); ++i)
	{
		if (list[i].compare(tileTypeKey) == 0)
		{
			ini.beginGroup(list[i]);

			debug(LOG_TERRAIN, "Looking at tileset type: %s", tileTypeKey.toUtf8().c_str());
			unsigned int counter = 0;

			std::vector<WzString> keys = ini.childKeys();
			for (size_t j = 0; j < keys.size(); ++j)
			{
				unsigned int tileType = ini.value(keys.at(j)).toUInt();

				if (tileType > TER_MAX)
				{
					debug(LOG_ERROR, "loadTerrainTypeMapOverride: terrain type out of range");
					resForceBaseDir("");
					return false;
				}
				// Workaround for fugly map editor bug, since we can't fix the map editor
				if (counter > (MAX_TILE_TEXTURES - 1))
				{
					debug(LOG_ERROR, "loadTerrainTypeMapOverride: too many textures!");
					resForceBaseDir("");
					return false;
				}
				// Log the output for the override value.
				if (terrainTypes[counter] != tileType)
				{
					debug(LOG_TERRAIN, "Upgrading map tile %d (type %d) to type %d", counter, terrainTypes[counter], tileType);
				}
				terrainTypes[counter] = tileType;
				++counter;
				debug(LOG_TERRAIN, "Tile %d at value: %d", counter - 1, tileType);
			}
			ini.endGroup();
		}
	}

	resForceBaseDir("");

	return true;
}

// -----------------------------------------------------------------------------------------
// Write out the terrain type map
static bool writeTerrainTypeMapFile(char *pFileName)
{
	ASSERT_OR_RETURN(false, pFileName != nullptr, "pFileName is null");

	WzMap::TerrainTypeData ttypeData;
	ttypeData.terrainTypes.reserve(MAX_TILE_TEXTURES);
	for (size_t i = 0; i < MAX_TILE_TEXTURES; i++)
	{
		UBYTE &tType = terrainTypes[i];
		if (tType > TER_MAX)
		{
			debug(LOG_ERROR, "Terrain type exceeds TER_MAX: %" PRIu8 "", tType);
		}
		ttypeData.terrainTypes.push_back(static_cast<TYPE_OF_TERRAIN>(tType));
	}

	/* Write out the map data */
	WzMapPhysFSIO mapIO;
	WzMapDebugLogger debugLoggerInstance;
	return WzMap::writeTerrainTypes(ttypeData, pFileName, mapIO, WzMap::LatestOutputFormat, &debugLoggerInstance);
}

// -----------------------------------------------------------------------------------------
bool loadSaveCompList(const char *pFileName)
{
	WzConfig ini(WzString::fromUtf8(pFileName), WzConfig::ReadOnly);

	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		ini.beginGroup("player_" + WzString::number(player));
		std::vector<WzString> list = ini.childKeys();
		for (size_t i = 0; i < list.size(); ++i)
		{
			WzString name = list[i];
			int state = ini.value(name, UNAVAILABLE).toInt();
			COMPONENT_STATS *psComp = getCompStatsFromName(name);
			ASSERT_OR_RETURN(false, psComp, "Bad component %s", name.toUtf8().c_str());
			ASSERT_OR_RETURN(false, psComp->compType >= 0 && psComp->compType != COMP_NUMCOMPONENTS, "Bad type %d", psComp->compType);
			ASSERT_OR_RETURN(false, state == UNAVAILABLE || state == AVAILABLE || state == FOUND || state == REDUNDANT,
			                 "Bad state %d for %s", state, name.toUtf8().c_str());
			apCompLists[player][psComp->compType][psComp->index] = state;
		}
		ini.endGroup();
	}
	return true;
}

// -----------------------------------------------------------------------------------------
// Write out the current state of the Comp lists per player
static bool writeCompListFile(const char *pFileName)
{
	WzConfig ini(WzString::fromUtf8(pFileName), WzConfig::ReadAndWrite);

	// Save each type of struct type
	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		ini.beginGroup("player_" + WzString::number(player));
		for (int i = 0; i < numBodyStats; i++)
		{
			COMPONENT_STATS *psStats = (COMPONENT_STATS *)(asBodyStats + i);
			const int state = apCompLists[player][COMP_BODY][i];
			if (state != UNAVAILABLE)
			{
				ini.setValue(psStats->id, state);
			}
		}
		for (int i = 0; i < numWeaponStats; i++)
		{
			COMPONENT_STATS *psStats = (COMPONENT_STATS *)(asWeaponStats + i);
			const int state = apCompLists[player][COMP_WEAPON][i];
			if (state != UNAVAILABLE)
			{
				ini.setValue(psStats->id, state);
			}
		}
		for (int i = 0; i < numConstructStats; i++)
		{
			COMPONENT_STATS *psStats = (COMPONENT_STATS *)(asConstructStats + i);
			const int state = apCompLists[player][COMP_CONSTRUCT][i];
			if (state != UNAVAILABLE)
			{
				ini.setValue(psStats->id, state);
			}
		}
		for (int i = 0; i < numECMStats; i++)
		{
			COMPONENT_STATS *psStats = (COMPONENT_STATS *)(asECMStats + i);
			const int state = apCompLists[player][COMP_ECM][i];
			if (state != UNAVAILABLE)
			{
				ini.setValue(psStats->id, state);
			}
		}
		for (int i = 0; i < numPropulsionStats; i++)
		{
			COMPONENT_STATS *psStats = (COMPONENT_STATS *)(asPropulsionStats + i);
			const int state = apCompLists[player][COMP_PROPULSION][i];
			if (state != UNAVAILABLE)
			{
				ini.setValue(psStats->id, state);
			}
		}
		for (int i = 0; i < numSensorStats; i++)
		{
			COMPONENT_STATS *psStats = (COMPONENT_STATS *)(asSensorStats + i);
			const int state = apCompLists[player][COMP_SENSOR][i];
			if (state != UNAVAILABLE)
			{
				ini.setValue(psStats->id, state);
			}
		}
		for (int i = 0; i < numRepairStats; i++)
		{
			COMPONENT_STATS *psStats = (COMPONENT_STATS *)(asRepairStats + i);
			const int state = apCompLists[player][COMP_REPAIRUNIT][i];
			if (state != UNAVAILABLE)
			{
				ini.setValue(psStats->id, state);
			}
		}
		for (int i = 0; i < numBrainStats; i++)
		{
			COMPONENT_STATS *psStats = (COMPONENT_STATS *)(asBrainStats + i);
			const int state = apCompLists[player][COMP_BRAIN][i];
			if (state != UNAVAILABLE)
			{
				ini.setValue(psStats->id, state);
			}
		}
		ini.endGroup();
	}
	return true;
}

// -----------------------------------------------------------------------------------------
// load up structure type list file
static bool loadSaveStructTypeList(const char *pFileName)
{
	WzConfig ini(pFileName, WzConfig::ReadOnly);

	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		ini.beginGroup("player_" + WzString::number(player));
		std::vector<WzString> list = ini.childKeys();
		for (size_t i = 0; i < list.size(); ++i)
		{
			WzString name = list[i];
			int state = ini.value(name, UNAVAILABLE).toInt();
			int statInc;

			ASSERT_OR_RETURN(false, state == UNAVAILABLE || state == AVAILABLE || state == FOUND || state == REDUNDANT,
			                 "Bad state %d for %s", state, name.toUtf8().c_str());
			for (statInc = 0; statInc < numStructureStats; statInc++) // loop until find the same name
			{
				STRUCTURE_STATS *psStats = asStructureStats + statInc;

				if (name.compare(psStats->id) == 0)
				{
					apStructTypeLists[player][statInc] = state;
					break;
				}
			}
			ASSERT_OR_RETURN(false, statInc != numStructureStats, "Did not find structure %s", name.toUtf8().c_str());
		}
		ini.endGroup();
	}
	return true;
}

// -----------------------------------------------------------------------------------------
// Write out the current state of the Struct Type List per player
static bool writeStructTypeListFile(const char *pFileName)
{
	WzConfig ini(pFileName, WzConfig::ReadAndWrite);

	// Save each type of struct type
	for (int player = 0; player < MAX_PLAYERS; player++)
	{
		ini.beginGroup("player_" + WzString::number(player));
		STRUCTURE_STATS *psStats = asStructureStats;
		for (int i = 0; i < numStructureStats; i++, psStats++)
		{
			if (apStructTypeLists[player][i] != UNAVAILABLE)
			{
				ini.setValue(psStats->id, apStructTypeLists[player][i]);
			}
		}
		ini.endGroup();
	}
	return true;
}

// load up saved research file
bool loadSaveResearch(const char *pFileName)
{
	WzConfig ini(pFileName, WzConfig::ReadOnly);
	const int players = game.maxPlayers;
	std::vector<WzString> list = ini.childGroups();
	for (size_t i = 0; i < list.size(); ++i)
	{
		ini.beginGroup(list[i]);
		bool found = false;
		WzString name = ini.value("name").toWzString();
		int statInc;
		for (statInc = 0; statInc < asResearch.size(); statInc++)
		{
			RESEARCH *psStats = &asResearch[statInc];
			//loop until find the same name
			if (psStats->id.compare(name) == 0)

			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			//ignore this record
			debug(LOG_SAVE, "Skipping unknown research named '%s'", name.toStdString().c_str());
			ini.endGroup();
			continue;
		}
		auto researchedList = ini.value("researched").jsonValue();
		auto possiblesList = ini.value("possible").jsonValue();
		auto pointsList = ini.value("currentPoints").jsonValue();
		ASSERT(researchedList.is_array(), "Bad (non-array) researched list for %s", name.toUtf8().c_str());
		ASSERT(possiblesList.is_array(), "Bad (non-array) possible list for %s", name.toUtf8().c_str());
		ASSERT(pointsList.is_array(), "Bad (non-array) points list for %s", name.toUtf8().c_str());
		ASSERT(researchedList.size() == players, "Bad researched list for %s", name.toUtf8().c_str());
		ASSERT(possiblesList.size() == players, "Bad possible list for %s", name.toUtf8().c_str());
		ASSERT(pointsList.size() == players, "Bad points list for %s", name.toUtf8().c_str());
		for (int plr = 0; plr < players; plr++)
		{
			PLAYER_RESEARCH *psPlRes;
			int researched = json_getValue(researchedList, plr).toInt();
			int possible = json_getValue(possiblesList, plr).toInt();
			int points = json_getValue(pointsList, plr).toInt();

			psPlRes = &asPlayerResList[plr][statInc];
			// Copy the research status
			psPlRes->ResearchStatus = (researched & RESBITS);
			SetResearchPossible(psPlRes, possible);
			psPlRes->currentPoints = points;
			//for any research that has been completed - perform so that upgrade values are set up
			if (researched == RESEARCHED)
			{
				researchResult(statInc, plr, false, nullptr, false);
			}
		}
		ini.endGroup();
	}
	return true;
}

// -----------------------------------------------------------------------------------------
// Write out the current state of the Research per player
static bool writeResearchFile(char *pFileName)
{
	WzConfig ini(WzString::fromUtf8(pFileName), WzConfig::ReadAndWrite);

	for (size_t i = 0; i < asResearch.size(); ++i)
	{
		RESEARCH *psStats = &asResearch[i];
		bool valid = false;
		std::vector<WzString> possibles, researched, points;
		for (int player = 0; player < game.maxPlayers; player++)
		{
			possibles.push_back(WzString::number(GetResearchPossible(&asPlayerResList[player][i])));
			researched.push_back(WzString::number(asPlayerResList[player][i].ResearchStatus & RESBITS));
			points.push_back(WzString::number(asPlayerResList[player][i].currentPoints));
			if (IsResearchPossible(&asPlayerResList[player][i]) || (asPlayerResList[player][i].ResearchStatus & RESBITS) || asPlayerResList[player][i].currentPoints)
			{
				valid = true;	// write this entry
			}
		}
		if (valid)
		{
			ini.beginGroup("research_" + WzString::number(i));
			ini.setValue("name", psStats->id);
			ini.setValue("possible", possibles);
			ini.setValue("researched", researched);
			ini.setValue("currentPoints", points);
			ini.endGroup();
		}
	}
	return true;
}


// -----------------------------------------------------------------------------------------
// load up saved message file
bool loadSaveMessage(const char* pFileName, LEVEL_TYPE levelType)
{
	// Only clear the messages if its a mid save game
	if (gameType == GTYPE_SAVE_MIDMISSION)
	{
		freeMessages();
	}
	else if (gameType == GTYPE_SAVE_START)
	{
		// If we are loading in a CamStart or a CamChange then we are not interested in any saved messages
		if (levelType == LEVEL_TYPE::LDS_CAMSTART || levelType == LEVEL_TYPE::LDS_CAMCHANGE)
		{
			return true;
		}
	}

	WzConfig ini(pFileName, WzConfig::ReadOnly);
	std::vector<WzString> list = ini.childGroups();
	for (size_t i = 0; i < list.size(); ++i)
	{
		ini.beginGroup(list[i]);
		MESSAGE_TYPE type = (MESSAGE_TYPE)ini.value("type").toInt();
		bool bObj = ini.contains("obj/id");
		int player = ini.value("player").toInt();
		int id = ini.value("id").toInt();
		int dataType = ini.value("dataType").toInt();

		if (type == MSG_PROXIMITY)
		{
			//only load proximity if a mid-mission save game
			if (gameType == GTYPE_SAVE_MIDMISSION)
			{
				if (bObj)
				{
					// Proximity object so create get the obj from saved idy
					int objId = ini.value("obj/id").toInt();
					int objPlayer = ini.value("obj/player").toInt();
					OBJECT_TYPE objType = (OBJECT_TYPE)ini.value("obj/type").toInt();
					auto psMessage = addMessage(type, true, player);
					if (psMessage)
					{
						psMessage->psObj = getBaseObjFromData(objId, objPlayer, objType);
						ASSERT(psMessage->psObj, "Viewdata object id %d not found for message %d", objId, id);
					}
					else
					{
						debug(LOG_ERROR, "Proximity object could not be created (type=%d, player=%d, message=%d)", type, player, id);
					}
				}
				else
				{
					VIEWDATA* psViewData = nullptr;

					// Proximity position so get viewdata pointer from the name
					auto psMessage = addMessage(type, false, player);

					if (psMessage)
					{
						if (dataType == MSG_DATA_BEACON)
						{
							//See addBeaconMessage(). psMessage->dataType is wrong here because
							//addMessage() calls createMessage() which defaults dataType to MSG_DATA_DEFAULT.
							//Later when findBeaconMsg() attempts to find a placed beacon it can't because
							//the dataType is wrong.
							psMessage->dataType = MSG_DATA_BEACON;
							Vector2i pos = ini.vector2i("position");
							int sender = ini.value("sender").toInt();
							psViewData = CreateBeaconViewData(sender, pos.x, pos.y);
							ASSERT(psViewData, "Could not create view data for message %d", id);
							if (psViewData == nullptr)
							{
								// Skip this message
								removeMessage(psMessage.get(), player);
								continue;
							}
						}
						else if (ini.contains("name"))
						{
							psViewData = getViewData(ini.value("name").toWzString());
							ASSERT(psViewData, "Failed to find view data for proximity position - skipping message %d", id);
							if (psViewData == nullptr)
							{
								// Skip this message
								removeMessage(psMessage.get(), player);
								continue;
							}
						}
						else
						{
							debug(LOG_ERROR, "Proximity position with empty name skipped (message %d)", id);
							removeMessage(psMessage.get(), player);
							continue;
						}

						psMessage->pViewData = psViewData;
						// Check the z value is at least the height of the terrain
						const int terrainHeight = map_Height(((VIEW_PROXIMITY*)psViewData->pData)->x, ((VIEW_PROXIMITY*)psViewData->pData)->y);
						if (((VIEW_PROXIMITY*)psViewData->pData)->z < terrainHeight)
						{
							((VIEW_PROXIMITY*)psViewData->pData)->z = terrainHeight;
						}
					}
					else
					{
						debug(LOG_ERROR, "Proximity position could not be created (type=%d, player=%d, message=%d)", type, player, id);
					}
				}
			}
		}
		else
		{
			// Only load Campaign/Mission messages if a mid-mission save game; always load research messages
			if (type == MSG_RESEARCH || gameType == GTYPE_SAVE_MIDMISSION)
			{
				auto psMessage = addMessage(type, false, player);
				ASSERT(psMessage, "Could not create message %d", id);
				if (psMessage)
				{
					psMessage->pViewData = getViewData(ini.value("name").toWzString());
					ASSERT(psMessage->pViewData, "Failed to find view data for message %d", id);
				}
			}
		}
		ini.endGroup();
	}
	jsDebugMessageUpdate();
	return true;
}

// Write out the current messages per player
static bool writeMessageFile(const char *pFileName)
{
	WzConfig ini(pFileName, WzConfig::ReadAndWrite);
	int numMessages = 0;

	// save each type of research
	for (int player = 0; player < game.maxPlayers; player++)
	{
		ASSERT(player < MAX_PLAYERS, "player out of bounds: %d", player);
		for (auto& psMessage : apsMessages[player])
		{
			ini.beginGroup("message_" + WzString::number(numMessages++));
			ini.setValue("id", numMessages - 1);	// for future use
			ini.setValue("player", player);
			ini.setValue("type", psMessage.type);
			ini.setValue("dataType", psMessage.dataType);
			if (psMessage.type == MSG_PROXIMITY)
			{
				//get the matching proximity message
				for (auto& psProx : apsProxDisp[player])
				{
					//compare the pointers
					if (psProx.psMessage == &psMessage)
					{
            if (psProx.type == POS_PROXDATA)
            {
              //message has viewdata so store the name
              VIEWDATA *pViewData = psMessage.pViewData;
              ini.setValue("name", pViewData->name);

              // save beacon data
              if (psMessage.dataType == MSG_DATA_BEACON)
              {
                VIEW_PROXIMITY *viewData = (VIEW_PROXIMITY *)psMessage.pViewData->pData;
                ini.setVector2i("position", Vector2i(viewData->x, viewData->y));
                ini.setValue("sender", viewData->sender);
              }
            }
						break;
					}

          // message has object so store Object Id
          const BASE_OBJECT *psObj = psMessage.psObj;
          if (psObj)
          {
            ini.setValue("obj/id", psObj->id);
            ini.setValue("obj/player", psObj->player);
            ini.setValue("obj/type", psObj->type);
          }
          else
          {
            ASSERT(false, "Message type has no object data to save ?");
          }
				}
			}
			else
			{
				const VIEWDATA *pViewData = psMessage.pViewData;
				ini.setValue("name", pViewData != nullptr ? pViewData->name : "NULL");
			}
			ini.setValue("read", psMessage.read);	// flag to indicate whether message has been read; not that this is/was _not_ read by loading code!?
			ASSERT(player == psMessage.player, "Bad player number (%d == %d)", player, psMessage.player);
			ini.endGroup();
		}
	}
	return true;
}

bool loadSaveStructLimits(const char *pFileName)
{
	WzConfig ini(pFileName, WzConfig::ReadOnly);

	for (int player = 0; player < game.maxPlayers; player++)
	{
		ini.beginGroup("player_" + WzString::number(player));
		std::vector<WzString> list = ini.childKeys();
		for (size_t i = 0; i < list.size(); ++i)
		{
			WzString name = list[i];
			int limit = ini.value(name, 0).toInt();

			if (name.compare("@Droid") == 0)
			{
				setMaxDroids(player, limit);
			}
			else if (name.compare("@Commander") == 0)
			{
				setMaxCommanders(player, limit);
			}
			else if (name.compare("@Constructor") == 0)
			{
				setMaxConstructors(player, limit);
			}
			else
			{
				unsigned statInc;
				for (statInc = 0; statInc < numStructureStats; ++statInc)
				{
					STRUCTURE_STATS *psStats = asStructureStats + statInc;
					if (name.compare(psStats->id) == 0)
					{
						asStructureStats[statInc].upgrade[player].limit = limit != 255 ? limit : LOTS_OF;
						break;
					}
				}
				ASSERT_OR_RETURN(false, statInc != numStructureStats, "Did not find structure %s", name.toUtf8().c_str());
			}
		}
		ini.endGroup();
	}
	return true;
}

/*
Writes the list of structure limits to a file
*/
bool writeStructLimitsFile(const char *pFileName)
{
	WzConfig ini(pFileName, WzConfig::ReadAndWrite);

	// Save each type of struct type
	for (int player = 0; player < game.maxPlayers; player++)
	{
		ini.beginGroup("player_" + WzString::number(player));

		ini.setValue("@Droid", getMaxDroids(player));
		ini.setValue("@Commander", getMaxCommanders(player));
		ini.setValue("@Constructor", getMaxConstructors(player));

		STRUCTURE_STATS *psStats = asStructureStats;
		for (int i = 0; i < numStructureStats; i++, psStats++)
		{
			const int limit = MIN(asStructureStats[i].upgrade[player].limit, 255);
			if (limit != 255)
			{
				ini.setValue(psStats->id, limit);
			}
		}
		ini.endGroup();
	}
	return true;
}

/*!
 * Load the current fire-support designated commanders (the one who has fire-support enabled)
 */
bool readFiresupportDesignators(const char *pFileName)
{
	WzConfig ini(pFileName, WzConfig::ReadOnly);
	std::vector<WzString> list = ini.childGroups();

	for (size_t i = 0; i < list.size(); ++i)
	{
		uint32_t id = ini.value("Player_" + WzString::number(i) + "/id", NULL_ID).toInt();
		if (id != NULL_ID)
		{
			cmdDroidSetDesignator((DROID *)getBaseObjFromId(id, OBJ_DROID));
		}
	}
	return true;
}

/*!
 * Save the current fire-support designated commanders (the one who has fire-support enabled)
 */
bool writeFiresupportDesignators(const char *pFileName)
{
	int player;
	WzConfig ini(pFileName, WzConfig::ReadAndWrite);

	for (player = 0; player < MAX_PLAYERS; player++)
	{
		DROID *psDroid = cmdDroidGetDesignator(player);
		if (psDroid != nullptr)
		{
			ini.setValue("Player_" + WzString::number(player) + "/id", psDroid->id);
		}
	}
	return true;
}

// write the event state to a file on disk
static bool	writeScriptState(const char *pFileName)
{
	char	jsFilename[PATH_MAX], *ext;

	// The below belongs to the new javascript stuff
	sstrcpy(jsFilename, pFileName);
	ext = strrchr(jsFilename, '/');
	*ext = '\0';
	strcat(jsFilename, "/scriptstate.json");
	saveScriptStates(jsFilename);

	return true;
}

// load the script state given a .gam name
bool loadScriptState(char *pFileName)
{
	char	jsFilename[PATH_MAX];

	pFileName[strlen(pFileName) - 4] = '\0';

	// The below belongs to the new javascript stuff
	sstrcpy(jsFilename, pFileName);
	strcat(jsFilename, "/scriptstate.json");
	loadScriptStates(jsFilename);

	// change the file extension
	strcat(pFileName, "/scriptstate.es");

	return true;
}


// -----------------------------------------------------------------------------------------
/* set the global scroll values to use for the save game */
static void setMapScroll()
{
	//if loading in a pre version5 then scroll values will not have been set up so set to max poss
	if (width == 0 && height == 0)
	{
		scrollMinX = 0;
		scrollMaxX = mapWidth;
		scrollMinY = 0;
		scrollMaxY = mapHeight;
		return;
	}
	scrollMinX = startX;
	scrollMinY = startY;
	scrollMaxX = startX + width;
	scrollMaxY = startY + height;
	//check not going beyond width/height of map
	if (scrollMaxX > (SDWORD)mapWidth)
	{
		scrollMaxX = mapWidth;
		debug(LOG_NEVER, "scrollMaxX was too big It has been set to map width");
	}
	if (scrollMaxY > (SDWORD)mapHeight)
	{
		scrollMaxY = mapHeight;
		debug(LOG_NEVER, "scrollMaxY was too big It has been set to map height");
	}
}

/*returns the current type of save game being loaded*/
GAME_TYPE getSaveGameType()
{
	return gameType;
}
