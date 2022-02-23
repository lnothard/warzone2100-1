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
/*
 * Levels.c
 *
 * Control the data loading for game levels
 *
 */

#include <ctype.h>
#include <string.h>

#include "lib/framework/frame.h"
#include "lib/framework/frameresource.h"
#include "lib/framework/file.h"
#include "lib/framework/crc.h"
#include "lib/framework/physfs_ext.h"
#include "lib/gamelib/gtime.h"
#include "lib/exceptionhandler/dumpinfo.h"
#include "clparse.h"
#include "init.h"
#include "objects.h"
#include "hci.h"
#include "levels.h"
#include "levelint.h"
#include "game.h"
#include "lib/ivis_opengl/piestate.h"
#include "data.h"
#include "research.h"
#include "lib/framework/lexer_input.h"
#include "effects.h"
#include "main.h"
#include "multiint.h"
#include "qtscript.h"
#include "wrappers.h"
#include "activity.h"

#include <unordered_set>

#include "3rdparty/gsl_finally.h"

extern int lev_get_lineno();
extern char *lev_get_text();
extern int lev_lex();
extern void lev_set_extra(YY_EXTRA_TYPE user_defined);
extern int lev_lex_destroy();

// block ID number start for the current level data (as opposed to a dataset)
#define CURRENT_DATAID		LEVEL_MAXFILES

static	char	currentLevelName[32] = { "main" };

// the current level descriptions
LEVEL_LIST psLevels;

// the currently loaded data set
static LEVEL_DATASET	*psBaseData = nullptr;
static LEVEL_DATASET	*psCurrLevel = nullptr;

// dummy level data for single WRF loads
static LEVEL_DATASET	sSingleWRF = { LEVEL_TYPE::LDS_COMPLETE, 0, 0, nullptr, mod_clean, {nullptr}, nullptr, nullptr, nullptr, {{0}}};

// return values from the lexer
char *pLevToken;
LEVEL_TYPE levVal;
static GAME_TYPE levelLoadType;

// modes for the parser
enum LEVELPARSER_STATE
{
	LP_START,		// no input received
	LP_LEVEL,		// level token received
	LP_LEVELDONE,	// defined a level waiting for players/type/data
	LP_PLAYERS,		// players token received
	LP_TYPE,		// type token received
	LP_DATASET,		// dataset token received
	LP_WAITDATA,	// defining level data, waiting for data token
	LP_DATA,		// data token received
	LP_GAME,		// game token received
};


// initialise the level system
bool levInitialise()
{
	psLevels.clear();
	psBaseData = nullptr;
	psCurrLevel = nullptr;

	return true;
}

SDWORD getLevelLoadType()
{
	return levelLoadType;
}

static inline void freeLevel(LEVEL_DATASET* toDelete)
{
	for (auto &apDataFile : toDelete->apDataFiles)
	{
		if (apDataFile != nullptr)
		{
			free(apDataFile);
		}
	}

	free(toDelete->pName);
	free(toDelete->realFileName);
	free(toDelete);
}

// shutdown the level system
void levShutDown()
{
	for (auto toDelete : psLevels)
	{
		freeLevel(toDelete);
	}
	psLevels.clear();
}

// error report function for the level parser
void lev_error(const char *msg)
{
	debug(LOG_ERROR, "Level File parse error: `%s` at line `%d` text `%s`", msg, lev_get_lineno(), lev_get_text());
}

/** Find a level dataset with the given name.
 *  @param name the name of the dataset to search for.
 *  @return a dataset with associated with the given @c name, or NULL if none
 *          could be found.
 */
LEVEL_DATASET *levFindDataSet(char const *name, Sha256 const *hash)
{
	if (hash != nullptr && hash->isZero())
	{
		hash = nullptr;  // Don't check hash if it's just 0000000000000000000000000000000000000000000000000000000000000000. Assuming real map files probably won't have that particular SHA-256 hash.
	}

	for (auto psNewLevel : psLevels)
	{
		if (psNewLevel->pName && strcmp(psNewLevel->pName, name) == 0)
		{
			if (hash == nullptr || levGetFileHash(psNewLevel) == *hash)
			{
				return psNewLevel;
			}
		}
	}

	return nullptr;
}

LEVEL_DATASET *levFindDataSetByRealFileName(char const *realFileName, Sha256 const *hash)
{
	if (hash != nullptr && hash->isZero())
	{
		hash = nullptr;  // Don't check hash if it's just 0000000000000000000000000000000000000000000000000000000000000000. Assuming real map files probably won't have that particular SHA-256 hash.
	}

	for (auto psNewLevel : psLevels)
	{
		if (psNewLevel->realFileName && strcmp(psNewLevel->realFileName, realFileName) == 0)
		{
			if (hash == nullptr || levGetFileHash(psNewLevel) == *hash)
			{
				return psNewLevel;
			}
		}
	}

	return nullptr;
}

bool levRemoveDataSetByRealFileName(char const *realFileName, Sha256 const *hash)
{
	if (hash != nullptr && hash->isZero())
	{
		hash = nullptr;  // Don't check hash if it's just 0000000000000000000000000000000000000000000000000000000000000000. Assuming real map files probably won't have that particular SHA-256 hash.
	}

	size_t numRemoved = 0;
	for (auto it = psLevels.begin(); it != psLevels.end();)
	{
		LEVEL_DATASET *level = *it;
		if (level && level->realFileName && strcmp(level->realFileName, realFileName) == 0)
		{
			if (hash == nullptr || levGetFileHash(level) == *hash)
			{
				if (psCurrLevel == *it)
				{
					ASSERT(false, "Trying to remove what is still the current level");
					continue;
				}

				LEVEL_DATASET* toDelete = *it;
				it = psLevels.erase(it);
				freeLevel(toDelete);
				++numRemoved;
				continue;
			}
		}
		++it;
	}

	return numRemoved > 0;
}

Sha256 levGetFileHash(LEVEL_DATASET *level)
{
	if (level->realFileName != nullptr && level->realFileHash.isZero())
	{
		level->realFileHash = findHashOfFile(level->realFileName);
		debug(LOG_WZ, "Hash of file \"%s\" is %s.", level->realFileName, level->realFileHash.toString().c_str());
	}
	return level->realFileHash;
}

bool levSetFileHashByRealFileName(char const *realFileName, Sha256 const &hash)
{
	size_t numAffected = 0;
	for (auto psNewLevel : psLevels)
	{
		if (psNewLevel && psNewLevel->realFileName && strcmp(psNewLevel->realFileName, realFileName) == 0)
		{
			ASSERT(psNewLevel->realFileHash.isZero(), "Level already has a hash??");
			psNewLevel->realFileHash = hash;
			++numAffected;
		}
	}
	return numAffected > 0;
}

Sha256 levGetMapNameHash(char const *mapName)
{
	LEVEL_DATASET *level = levFindDataSet(mapName, nullptr);
	if (level == nullptr)
	{
		debug(LOG_WARNING, "Couldn't find map \"%s\" to hash.", mapName);
		Sha256 zero;
		zero.setZero();
		return zero;
	}
	return levGetFileHash(level);
}

// parse a level description data file
// the ignoreWrf hack is for compatibility with old maps that try to link in various
// data files that we have removed
bool levParse(const char *buffer, size_t size, searchPathMode pathMode, bool ignoreWrf, char const *realFileName)
{
	lexerinput_t input;
	LEVELPARSER_STATE state;
	int token, currData = -1;
	LEVEL_DATASET	*psDataSet = nullptr;

	input.type = LEXINPUT_BUFFER;
	input.input.buffer.begin = buffer;
	input.input.buffer.end = &buffer[size];

	lev_set_extra(&input);

	state = LP_START;
	auto always_destroy_on_return = gsl::finally([] { lev_lex_destroy(); });

	for (token = lev_lex(); token != 0; token = lev_lex())
	{
		switch (token)
		{
		case LTK_LEVEL:
		case LTK_CAMPAIGN:
		case LTK_CAMSTART:
		case LTK_CAMCHANGE:
		case LTK_EXPAND:
		case LTK_BETWEEN:
		case LTK_MKEEP:
		case LTK_MCLEAR:
		case LTK_EXPAND_LIMBO:
		case LTK_MKEEP_LIMBO:
			if (state == LP_START || state == LP_WAITDATA)
			{
				// start a new level data set
				psDataSet = (LEVEL_DATASET *)malloc(sizeof(LEVEL_DATASET));
				if (!psDataSet)
				{
					debug(LOG_FATAL, "Out of memory");
					abort();
					return false;
				}
				memset(psDataSet, 0, sizeof(LEVEL_DATASET));
				psDataSet->players = 1;
				psDataSet->game = -1;
				psDataSet->dataDir = pathMode;
				psDataSet->realFileName = realFileName != nullptr ? strdup(realFileName) : nullptr;
				psDataSet->realFileHash.setZero();  // The hash is only calculated on demand; for example, if the map name matches.
				psLevels.push_back(psDataSet);
				currData = 0;

				// set the dataset type
				switch (token)
				{
				case LTK_LEVEL:
					psDataSet->type = LEVEL_TYPE::LDS_COMPLETE;
					break;
				case LTK_CAMPAIGN:
					psDataSet->type = LEVEL_TYPE::LDS_CAMPAIGN;
					break;
				case LTK_CAMSTART:
					psDataSet->type = LEVEL_TYPE::LDS_CAMSTART;
					break;
				case LTK_BETWEEN:
					psDataSet->type = LEVEL_TYPE::LDS_BETWEEN;
					break;
				case LTK_MKEEP:
					psDataSet->type = LEVEL_TYPE::LDS_MKEEP;
					break;
				case LTK_CAMCHANGE:
					psDataSet->type = LEVEL_TYPE::LDS_CAMCHANGE;
					break;
				case LTK_EXPAND:
					psDataSet->type = LEVEL_TYPE::LDS_EXPAND;
					break;
				case LTK_MCLEAR:
					psDataSet->type = LEVEL_TYPE::LDS_MCLEAR;
					break;
				case LTK_EXPAND_LIMBO:
					psDataSet->type = LEVEL_TYPE::LDS_EXPAND_LIMBO;
					break;
				case LTK_MKEEP_LIMBO:
					psDataSet->type = LEVEL_TYPE::LDS_MKEEP_LIMBO;
					break;
				default:
					ASSERT(false, "eh?");
					break;
				}
			}
			else
			{
				lev_error("Syntax Error");
				return false;
			}
			state = LP_LEVEL;
			break;
		case LTK_PLAYERS:
			if (state == LP_LEVELDONE &&
			    (psDataSet->type == LEVEL_TYPE::LDS_COMPLETE || psDataSet->type >= LEVEL_TYPE::LDS_MULTI_TYPE_START))
			{
				state = LP_PLAYERS;
			}
			else
			{
				lev_error("Syntax Error");
				return false;
			}
			break;
		case LTK_TYPE:
			if (state == LP_LEVELDONE && psDataSet->type == LEVEL_TYPE::LDS_COMPLETE)
			{
				state = LP_TYPE;
			}
			else
			{
				lev_error("Syntax Error");
				return false;
			}
			break;
		case LTK_INTEGER:
			if (state == LP_PLAYERS)
			{
				psDataSet->players = (SWORD)levVal;
			}
			else if (state == LP_TYPE)
			{
				if (levVal < LEVEL_TYPE::LDS_MULTI_TYPE_START)
				{
					lev_error("invalid type number");
					return false;
				}

				psDataSet->type = levVal;
			}
			else
			{
				lev_error("Syntax Error");
				return false;
			}
			state = LP_LEVELDONE;
			break;
		case LTK_DATASET:
			if (state == LP_LEVELDONE && psDataSet->type != LEVEL_TYPE::LDS_COMPLETE)
			{
				state = LP_DATASET;
			}
			else
			{
				lev_error("Syntax Error");
				return false;
			}
			break;
		case LTK_DATA:
			if (state == LP_WAITDATA)
			{
				state = LP_DATA;
			}
			else if (state == LP_LEVELDONE)
			{
				if (psDataSet->type == LEVEL_TYPE::LDS_CAMSTART ||
				    psDataSet->type == LEVEL_TYPE::LDS_MKEEP
				    || psDataSet->type == LEVEL_TYPE::LDS_CAMCHANGE ||
				    psDataSet->type == LEVEL_TYPE::LDS_EXPAND ||
				    psDataSet->type == LEVEL_TYPE::LDS_MCLEAR ||
				    psDataSet->type == LEVEL_TYPE::LDS_EXPAND_LIMBO ||
				    psDataSet->type == LEVEL_TYPE::LDS_MKEEP_LIMBO
				   )
				{
					lev_error("Missing dataset command");
					return false;
				}
				state = LP_DATA;
			}
			else
			{
				lev_error("Syntax Error");
				return false;
			}
			break;
		case LTK_GAME:
			if ((state == LP_WAITDATA || state == LP_LEVELDONE) &&
			    psDataSet->game == -1 && psDataSet->type != LEVEL_TYPE::LDS_CAMPAIGN)
			{
				state = LP_GAME;
			}
			else
			{
				lev_error("Syntax Error");
				return false;
			}
			break;
		case LTK_IDENT:
			if (state == LP_LEVEL)
			{
				if (psDataSet->type == LEVEL_TYPE::LDS_CAMCHANGE)
				{
					// This is a campaign change dataset, we need to find the full data set.
					LEVEL_DATASET *const psFoundData = levFindDataSet(pLevToken);

					if (psFoundData == nullptr)
					{
						lev_error("Cannot find full data set for camchange");
						return false;
					}

					if (psFoundData->type != LEVEL_TYPE::LDS_CAMSTART)
					{
						lev_error("Invalid data set name for cam change");
						return false;
					}
					psFoundData->psChange = psDataSet;
				}
				// store the level name
				psDataSet->pName = strdup(pLevToken);
				if (psDataSet->pName == nullptr)
				{
					debug(LOG_FATAL, "Out of memory!");
					abort();
					return false;
				}

				state = LP_LEVELDONE;
			}
			else if (state == LP_DATASET)
			{
				// find the dataset
				psDataSet->psBaseData = levFindDataSet(pLevToken);

				if (psDataSet->psBaseData == nullptr)
				{
					lev_error("Unknown dataset");
					return false;
				}
				state = LP_WAITDATA;
			}
			else
			{
				lev_error("Syntax Error");
				return false;
			}
			break;
		case LTK_STRING:
			if (state == LP_DATA || state == LP_GAME)
			{
				if (currData >= LEVEL_MAXFILES)
				{
					lev_error("Too many data files");
					return false;
				}

				// note the game index if necessary
				if (state == LP_GAME)
				{
					psDataSet->game = (SWORD)currData;
				}
				else if (ignoreWrf)
				{
					state = LP_WAITDATA;
					break;	// ignore this wrf line
				}

				// store the data name
				psDataSet->apDataFiles[currData] = strdup(pLevToken);

				resToLower(pLevToken);

				currData += 1;
				state = LP_WAITDATA;
			}
			else
			{
				lev_error("Syntax Error");
				return false;
			}
			break;
		default:
			lev_error("Unexpected token");
			break;
		}
	}

	// Accept empty files when parsing (indicated by currData < 0)
	if (currData >= 0
	    && (state != LP_WAITDATA
	        || currData == 0))
	{
		lev_error("Unexpected end of file");
		return false;
	}

	return true;
}


// free the data for the current mission
bool levReleaseMissionData()
{
	// release old data if any was loaded
	if (psCurrLevel != nullptr)
	{
		if (!stageThreeShutDown())
		{
			return false;
		}

		// free up the old data
		for (int i = LEVEL_MAXFILES - 1; i >= 0; i--)
		{
			if (i == psCurrLevel->game)
			{
				if (psCurrLevel->psBaseData == nullptr)
				{
					if (!stageTwoShutDown())
					{
						return false;
					}
				}
			}
			else// if (psCurrLevel->apDataFiles[i])
			{
				resReleaseBlockData(i + CURRENT_DATAID);
			}
		}
	}
	releaseObjectives = true; // allow releasing mission objectives after quitting / saveload
	return true;
}


// free the currently loaded dataset
bool levReleaseAll()
{
	// clear out old effect data first
	initEffectsSystem();

	// release old data if any was loaded
	if (psCurrLevel != nullptr)
	{
		if (!levReleaseMissionData())
		{
			debug(LOG_ERROR, "Failed to unload mission data");
			return false;
		}

		// release the game data
		if (psCurrLevel->psBaseData != nullptr)
		{
			if (!stageTwoShutDown())
			{
				debug(LOG_ERROR, "Failed stage two shutdown");
				return false;
			}
		}


		if (psCurrLevel->psBaseData)
		{
			for (int i = LEVEL_MAXFILES - 1; i >= 0; i--)
			{
				if (psCurrLevel->psBaseData->apDataFiles[i])
				{
					resReleaseBlockData(i);
				}
			}
		}

		psCurrLevel = nullptr;

		if (!stageOneShutDown())
		{
			debug(LOG_ERROR, "Failed stage one shutdown");
			return false;
		}
	}

	return true;
}

// load up a single wrf file
static bool levLoadSingleWRF(const char *name)
{
	// free the old data
	if (!levReleaseAll())
	{
		return false;
	}

	// create the dummy level data
	if (sSingleWRF.pName)
	{
		free(sSingleWRF.pName);
	}

	memset(&sSingleWRF, 0, sizeof(LEVEL_DATASET));
	sSingleWRF.pName = strdup(name);

	// load up the WRF
	if (!stageOneInitialise())
	{
		return false;
	}

	// load the data
	debug(LOG_WZ, "Loading %s ...", name);
	if (!resLoad(name, 0))
	{
		return false;
	}

	if (!stageThreeInitialise())
	{
		return false;
	}

	psCurrLevel = &sSingleWRF;

	return true;
}

const char *getLevelName()
{
	return currentLevelName;
}

// load up the data for a level
bool levLoadData(char const *name, Sha256 const *hash, char *pSaveName, GAME_TYPE saveType)
{
	debug(LOG_WZ, "Loading level %s hash %s (%s, type %d)", name, hash == nullptr ? "builtin" : hash->toString().c_str(), pSaveName, (int)saveType);
	if (saveType == GTYPE_SAVE_START || saveType == GTYPE_SAVE_MIDMISSION)
	{
		if (!levReleaseAll())
		{
			debug(LOG_ERROR, "Failed to unload old data");
			return false;
		}
	}

	// Ensure that the LC_NUMERIC locale setting is "C"
	ASSERT(strcmp(setlocale(LC_NUMERIC, NULL), "C") == 0, "The LC_NUMERIC locale is not \"C\" - this may break level-data parsing depending on the user's system locale settings");

	levelLoadType = saveType;

	// find the level dataset
	LEVEL_DATASET* psNewLevel = levFindDataSet(name, hash);
	if (psNewLevel == nullptr)
	{
		debug(LOG_INFO, "Dataset %s not found - trying to load as WRF", name);
		return levLoadSingleWRF(name);
	}
	debug(LOG_WZ, "** Data set found is %s type %d", psNewLevel->pName, (int)psNewLevel->type);

	/* Keep a copy of the present level name */
	sstrcpy(currentLevelName, name);

	const bool bCamChangeSaveGame = pSaveName && saveType == GTYPE_SAVE_START && psNewLevel->psChange != nullptr;
	if (bCamChangeSaveGame)
	{
		debug(LOG_WZ, "** CAMCHANGE FOUND");
	}

	// select the change dataset if there is one
	LEVEL_DATASET* psChangeLevel = nullptr;
	if (((psNewLevel->psChange != nullptr) && (psCurrLevel != nullptr)) || bCamChangeSaveGame)
	{
		//store the level name
		debug(LOG_WZ, "Found CAMCHANGE dataset");
		psChangeLevel = psNewLevel;
		psNewLevel = psNewLevel->psChange;
	}

	// ensure the correct dataset is loaded
	if (psNewLevel->type == LEVEL_TYPE::LDS_CAMPAIGN)
	{
		debug(LOG_ERROR, "Cannot load a campaign dataset (%s)", psNewLevel->pName);
		return false;
	}
	else
	{
		if (psCurrLevel != nullptr)
		{
			if ((psCurrLevel->psBaseData != psNewLevel->psBaseData) ||
			    (psCurrLevel->type < LEVEL_TYPE::LDS_NONE && psNewLevel->type  >= LEVEL_TYPE::LDS_NONE) ||
			    (psCurrLevel->type >= LEVEL_TYPE::LDS_NONE && psNewLevel->type  < LEVEL_TYPE::LDS_NONE))
			{
				// there is a dataset loaded but it isn't the correct one
				debug(LOG_WZ, "Incorrect base dataset loaded (%p != %p, %d - %d)",
				      static_cast<void *>(psCurrLevel->psBaseData), static_cast<void *>(psNewLevel->psBaseData), (int)psCurrLevel->type, (int)psNewLevel->type);
				if (!levReleaseAll())	// this sets psCurrLevel to NULL
				{
					debug(LOG_ERROR, "Failed to release old data");
					return false;
				}
			}
			else
			{
				debug(LOG_WZ, "Correct base dataset already loaded.");
			}
		}

		// setup the correct dataset to load if necessary
		if (psCurrLevel == nullptr)
		{
			if (psNewLevel->psBaseData != nullptr)
			{
				debug(LOG_WZ, "Setting base dataset to load: %s", psNewLevel->psBaseData->pName);
			}
			psBaseData = psNewLevel->psBaseData;
		}
		else
		{
			debug(LOG_WZ, "No base dataset to load");
			psBaseData = nullptr;
		}
	}

	if (!rebuildSearchPath(psNewLevel->dataDir, true, psNewLevel->realFileName))
	{
		debug(LOG_ERROR, "Failed to rebuild search path");
		return false;
	}

	// reset the old mission data if necessary
	if (psCurrLevel != nullptr)
	{
		debug(LOG_WZ, "Reseting old mission data");
		if (!levReleaseMissionData())
		{
			debug(LOG_ERROR, "Failed to unload old mission data");
			return false;
		}
	}

	// need to free the current map and droids etc for a save game
	if (psBaseData == nullptr && pSaveName != nullptr)
	{
		if (!saveGameReset())
		{
			debug(LOG_ERROR, "Failed to saveGameReset()!");
			return false;
		}
	}

	// initialise if necessary
	if (psNewLevel->type == LEVEL_TYPE::LDS_COMPLETE || psBaseData != nullptr)
	{
		debug(LOG_WZ, "Calling stageOneInitialise!");
		if (!stageOneInitialise())
		{
			debug(LOG_ERROR, "Failed stageOneInitialise!");
			return false;
		}
	}

	// load up a base dataset if necessary
	if (psBaseData != nullptr)
	{
		debug(LOG_WZ, "Loading base dataset %s", psBaseData->pName);
		for (int i = 0; i < LEVEL_MAXFILES; i++)
		{
			if (psBaseData->apDataFiles[i])
			{
				// load the data
				debug(LOG_WZ, "Loading [directory: %s] %s ...", WZ_PHYSFS_getRealDir_String(psBaseData->apDataFiles[i]).c_str(), psBaseData->apDataFiles[i]);
				if (!resLoad(psBaseData->apDataFiles[i], i))
				{
					debug(LOG_ERROR, "Failed resLoad(%s)!", psBaseData->apDataFiles[i]);
					return false;
				}
			}
		}
	}
	// preload faction IMDs
	std::unordered_set<FactionID> enabledNonNormalFactions = getEnabledFactions(true);
	if (!enabledNonNormalFactions.empty())
	{
		enumerateLoadedModels([enabledNonNormalFactions](const std::string &modelName, iIMDShape &){
			for (const auto& faction : enabledNonNormalFactions)
			{
				auto factionModel = getFactionModelName(faction, WzString::fromUtf8(modelName));
				if (factionModel.has_value())
				{
					iIMDShape *retval = modelGet(factionModel.value());
					ASSERT(retval != nullptr, "Cannot find the faction PIE model %s (for normal model: %s)",
						   factionModel.value().toUtf8().c_str(), modelName.c_str());
				}
			}
		});
		resDoResLoadCallback();		// do callback.
	}

	if (psNewLevel->type == LEVEL_TYPE::LDS_CAMCHANGE)
	{
		if (!campaignReset())
		{
			debug(LOG_ERROR, "Failed campaignReset()!");
			return false;
		}
	}
	if (psNewLevel->game == -1)  //no .gam file to load - BETWEEN missions (for Editor games only)
	{
		ASSERT(psNewLevel->type == LEVEL_TYPE::LDS_BETWEEN, "Only BETWEEN missions do not need a .gam file");
		debug(LOG_WZ, "No .gam file for level: BETWEEN mission");
		if (pSaveName != nullptr)
		{
			if (psBaseData != nullptr)
			{
				if (!stageTwoInitialise())
				{
					debug(LOG_ERROR, "Failed stageTwoInitialise()!");
					return false;
				}
			}

			//set the mission type before the saveGame data is loaded
			if (saveType == GTYPE_SAVE_MIDMISSION)
			{
				debug(LOG_WZ, "Init mission stuff");
				debug(LOG_NEVER, "dataSetSaveFlag");
				dataSetSaveFlag();
			}

			debug(LOG_NEVER, "Loading savegame: %s", pSaveName);
			if (!loadGame(pSaveName, false, true, true))
			{
				debug(LOG_ERROR, "Failed loadGame(%s)!", pSaveName);
				return false;
			}
		}

		if (pSaveName == nullptr || saveType == GTYPE_SAVE_START)
		{
			debug(LOG_NEVER, "Start mission - no .gam");
		}
	}

	//we need to load up the save game data here for a camchange
  if (bCamChangeSaveGame && pSaveName != nullptr) {
    if (psBaseData != nullptr && !stageTwoInitialise()) {
      debug(LOG_ERROR, "Failed stageTwoInitialise() [camchange]!");
      return false;
    }

    debug(LOG_NEVER, "loading savegame: %s", pSaveName);
    if (!loadGame(pSaveName, false, true, true)) {
      debug(LOG_ERROR, "Failed loadGame(%s)!", pSaveName);
      return false;
    }

    campaignReset();
  }


	// load the new data
	debug(LOG_NEVER, "Loading mission dataset: %s", psNewLevel->pName);
	for (int i = 0; i < LEVEL_MAXFILES; i++)
	{
		if (psNewLevel->game == i)
		{
			// do some more initialising if necessary
			if ((psNewLevel->type == LEVEL_TYPE::LDS_COMPLETE || psNewLevel->type >= LEVEL_TYPE::LDS_MULTI_TYPE_START ||
           psBaseData != nullptr && !bCamChangeSaveGame) && !stageTwoInitialise()) {
        debug(LOG_ERROR, "Failed stageTwoInitialise() [newdata]!");
        return false;
      }

			// load a savegame if there is one - but not if already done so
			if (pSaveName != nullptr && !bCamChangeSaveGame)
			{
				//set the mission type before the saveGame data is loaded
				if (saveType == GTYPE_SAVE_MIDMISSION)
				{
					debug(LOG_WZ, "Init mission stuff");


					debug(LOG_NEVER, "dataSetSaveFlag");
					dataSetSaveFlag();
				}

				debug(LOG_NEVER, "Loading save game %s", pSaveName);
				if (!loadGame(pSaveName, false, true, true))
				{
					debug(LOG_ERROR, "Failed loadGame(%s)!", pSaveName);
					return false;
				}
			}

			if (pSaveName == nullptr || saveType == GTYPE_SAVE_START)
			{
				// load the game
				debug(LOG_WZ, "Loading scenario file %s", psNewLevel->apDataFiles[i]);
				switch (psNewLevel->type)
				{
				case LEVEL_TYPE::LDS_COMPLETE:
				case LEVEL_TYPE::LDS_CAMSTART:
					debug(LOG_WZ, "LDS_COMPLETE / LDS_CAMSTART");

					break;
				case LEVEL_TYPE::LDS_BETWEEN:
					debug(LOG_WZ, "LDS_BETWEEN");

					break;

				case LEVEL_TYPE::LDS_MKEEP:
					debug(LOG_WZ, "LDS_MKEEP");

					break;
				case LEVEL_TYPE::LDS_CAMCHANGE:
					debug(LOG_WZ, "LDS_CAMCHANGE");

					break;

				case LEVEL_TYPE::LDS_EXPAND:
					debug(LOG_WZ, "LDS_EXPAND");

					break;
				case LEVEL_TYPE::LDS_EXPAND_LIMBO:
					debug(LOG_WZ, "LDS_LIMBO");

					break;

				case LEVEL_TYPE::LDS_MCLEAR:
					debug(LOG_WZ, "LDS_MCLEAR");

					break;
				case LEVEL_TYPE::LDS_MKEEP_LIMBO:
					debug(LOG_WZ, "LDS_MKEEP_LIMBO");

					break;
				default:
					ASSERT(psNewLevel->type >= LEVEL_TYPE::LDS_MULTI_TYPE_START, "Unexpected mission type");
					debug(LOG_WZ, "default (MULTIPLAYER)");

					break;
				}
			}
		}
		else if (psNewLevel->apDataFiles[i])
		{
			// load the data
			debug(LOG_WZ, "Loading %s", psNewLevel->apDataFiles[i]);
			if (!resLoad(psNewLevel->apDataFiles[i], i + CURRENT_DATAID))
			{
				debug(LOG_ERROR, "Failed resLoad(%s, %d) (default)!", psNewLevel->apDataFiles[i], i + CURRENT_DATAID);
				return false;
			}
		}
	}

	if (bMultiPlayer)
	{
		// This calls resLoadFile("SMSG", "multiplay.txt"). Must be before loadMissionExtras, which calls loadSaveMessage, which calls getViewData.
		loadMultiScripts();
	}

	if (pSaveName != nullptr)
	{
		//load MidMission Extras
		if (!loadMissionExtras(pSaveName, psNewLevel->type))
		{
			debug(LOG_ERROR, "Failed loadMissionExtras(%s, %d)!", pSaveName, static_cast<int8_t>(psNewLevel->type));
			return false;
		}
	}

	if (pSaveName != nullptr && saveType == GTYPE_SAVE_MIDMISSION)
	{
		//load script stuff
		// load the event system state here for a save game
		debug(LOG_SAVE, "Loading script system state");
		if (!loadScriptState(pSaveName))
		{
			debug(LOG_ERROR, "Failed loadScriptState(%s)!", pSaveName);
			return false;
		}
	}
	// this will trigger upgrades
	if (!stageThreeInitialise())
	{
		debug(LOG_ERROR, "Failed stageThreeInitialise()!");
		return false;
	}

	dataClearSaveFlag();

	//restore the level name for comparisons on next mission load up
	if (psChangeLevel == nullptr)
	{
		psCurrLevel = psNewLevel;
	}
	else
	{
		psCurrLevel = psChangeLevel;
	}

	// Copy this info to be used by the crash handler for the dump file
	char buf[256];

	ssprintf(buf, "Current Level/map is %s", psCurrLevel->pName);
	addDumpInfo(buf);

	if (autogame_enabled())
	{
		gameTimeSetMod(Rational(500));
		if (getHostLaunch() != HostLaunch::Skirmish) // tests will specify the AI manually
		{
			if (selectedPlayer < MAX_PLAYERS && !NetPlay.players[selectedPlayer].isSpectator)
			{
				jsAutogameSpecific("multiplay/skirmish/semperfi.js", selectedPlayer);
			}
			else
			{
				debug(LOG_INFO, "Skipping autogame auto-AI for selectedPlayer %" PRIu32 "", selectedPlayer);
			}
		}
	}

	ActivityManager::instance().loadedLevel(psCurrLevel->type, mapNameWithoutTechlevel(getLevelName()));

	return true;
}

std::string mapNameWithoutTechlevel(const char *mapName)
{
	ASSERT_OR_RETURN("", mapName != nullptr, "null mapName provided");
	std::string result(mapName);
	size_t len = result.length();
	if (len > 2 && result[len - 3] == '-' && result[len - 2] == 'T' && isdigit(result[len - 1]))
	{
		result.resize(len - 3);
	}
	return result;
}

/// returns maps of the right 'type'
LEVEL_LIST enumerateMultiMaps(int camToUse, int numPlayers)
{
	LEVEL_LIST list;

	if (game.type == LEVEL_TYPE::SKIRMISH)
	{
		// Add maps with exact match to type
		for (auto lev : psLevels)
		{
			int cam = 1;
			if (lev->type == LEVEL_TYPE::MULTI_SKIRMISH2)
			{
				cam = 2;
			}
			else if (lev->type == LEVEL_TYPE::MULTI_SKIRMISH3)
			{
				cam = 3;
			}
			else if (lev->type == LEVEL_TYPE::MULTI_SKIRMISH4)
			{
				cam = 4;
			}

			if ((lev->type == LEVEL_TYPE::SKIRMISH || lev->type == LEVEL_TYPE::MULTI_SKIRMISH2 || lev->type == LEVEL_TYPE::MULTI_SKIRMISH3 || lev->type == LEVEL_TYPE::MULTI_SKIRMISH4)
			    && (numPlayers == 0 || numPlayers == lev->players)
			    && cam == camToUse)
			{
				list.push_back(lev);
			}
		}
		// Also add maps where only the tech level is different, if a more specific map has not been added
		for (auto lev : psLevels)
		{
			if ((lev->type == LEVEL_TYPE::SKIRMISH || lev->type == LEVEL_TYPE::MULTI_SKIRMISH2 || lev->type == LEVEL_TYPE::MULTI_SKIRMISH3 || lev->type == LEVEL_TYPE::MULTI_SKIRMISH4)
			    && (numPlayers == 0 || numPlayers == lev->players)
			    && lev->pName)
			{
				bool already_added = false;
				for (auto map : list)
				{
					if (!map->pName)
					{
						continue;
					}
					std::string levelBaseName = mapNameWithoutTechlevel(lev->pName);
					std::string mapBaseName = mapNameWithoutTechlevel(map->pName);
					if (strcmp(levelBaseName.c_str(), mapBaseName.c_str()) == 0)
					{
						already_added = true;
						break;
					}
				}
				if (!already_added)
				{
					list.push_back(lev);
				}
			}
		}
	}

	return list;
}
