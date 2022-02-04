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
 * @file activity.cpp
 */

#include <SQLiteCpp/SQLiteCpp.h>

#include "activity.h"
#include "ai.h"
#include "modding.h"
#include "multiint.h"
#include "scores.h"

bool challengeActive;
bool Cheated;
std::string currentChallengeName();
std::string getCampaignName();


std::string SkirmishGameInfo::gameName() const
{
  return game.name;
}

std::string SkirmishGameInfo::mapName() const
{
  return game.map;
}

uint8_t SkirmishGameInfo::numberOfPlayers() const
{
  return numAIBotPlayers + 1;
}

bool SkirmishGameInfo::hasLimits() const
{
  return limit_no_tanks || limit_no_cyborgs ||
         limit_no_vtols || limit_no_uplink ||
         limit_no_lassat || force_structure_limits;
}

//std::string ActivitySink::getTeamDescription(const ActivitySink::SkirmishGameInfo& info)
//{
//	if (!alliancesSetTeamsBeforeGame(info.game.alliance))
//	{
//		return "";
//	}
//	std::map<int32_t, size_t> teamIdToCountOfPlayers;
//	for (size_t index = 0; index < std::min<size_t>(info.players.size(), (size_t)game.maxPlayers); ++index)
//	{
//		PLAYER const& p = NetPlay.players[index];
//		if (p.ai == AI_CLOSED)
//		{
//			// closed slot - skip
//			continue;
//		}
//		else if (p.ai == AI_OPEN)
//		{
//			if (p.isSpectator)
//			{
//				// spectator slot - skip
//				continue;
//			}
//			else if (!p.allocated)
//			{
//				// available slot - count team association
//				// (since available slots can have assigned teams)
//				teamIdToCountOfPlayers[p.team]++;
//			}
//			else
//			{
//				// human player
//				teamIdToCountOfPlayers[p.team]++;
//			}
//		}
//		else
//		{
//			// bot player
//			teamIdToCountOfPlayers[p.team]++;
//		}
//	}
//	if (teamIdToCountOfPlayers.size() <= 1)
//	{
//		// does not have multiple teams
//		return "";
//	}
//	std::string teamDescription;
//	for (const auto& team : teamIdToCountOfPlayers)
//	{
//		if (!teamDescription.empty())
//		{
//			teamDescription += "v";
//		}
//		teamDescription += std::to_string(team.second);
//	}
//	return teamDescription;
//}

std::string to_string(const GameEndReason& reason)
{
	switch (reason) {
	case GameEndReason::WON:
		return "Won";
	case GameEndReason::LOST:
		return "Lost";
	case GameEndReason::QUIT:
		return "Quit";
	}
}

std::string to_string(const END_GAME_STATS_DATA& stats)
{
	return astringf("numUnits: %u, missionStartedTime: %u, unitsBuilt: %u, unitsLost: %u, unitsKilled: %u",
	                stats.numUnits, stats.missionData.missionStarted, stats.missionData.unitsBuilt,
	                stats.missionData.unitsLost, stats.missionData.unitsKilled);
}

class LoggingActivitySink : public ActivitySink
{
public:
	// navigating main menus
	void navigatedToMenu(const std::string& menuName) override
	{
		debug(LOG_ACTIVITY, "- navigatedToMenu: %s", menuName.c_str());
	}

	// campaign games
	void startedCampaignMission(const std::string& campaign, const std::string& levelName) override
	{
		debug(LOG_ACTIVITY, "- startedCampaignMission: %s:%s", campaign.c_str(), levelName.c_str());
	}

	void endedCampaignMission(const std::string& campaign, const std::string& levelName, GameEndReason result,
	                                  END_GAME_STATS_DATA stats, bool cheatsUsed) override
	{
		debug(LOG_ACTIVITY, "- endedCampaignMission: %s:%s; result: %s; stats: (%s)", campaign.c_str(),
		      levelName.c_str(), to_string(result).c_str(), to_string(stats).c_str());
	}

	// challenges
	void startedChallenge(const std::string& challengeName) override
	{
		debug(LOG_ACTIVITY, "- startedChallenge: %s", challengeName.c_str());
	}

	void endedChallenge(const std::string& challengeName, GameEndReason result,
	                            const END_GAME_STATS_DATA& stats, bool cheatsUsed) override
	{
		debug(LOG_ACTIVITY, "- endedChallenge: %s; result: %s; stats: (%s)", challengeName.c_str(),
		      to_string(result).c_str(), to_string(stats).c_str());
	}

	void startedSkirmishGame(const SkirmishGameInfo& info) override
	{
		debug(LOG_ACTIVITY, "- startedSkirmishGame: %s", info.game.name);
	}

	void endedSkirmishGame(const SkirmishGameInfo& info, GameEndReason result,
	                               const END_GAME_STATS_DATA& stats) override
	{
		debug(LOG_ACTIVITY, "- endedSkirmishGame: %s; result: %s; stats: (%s)", info.game.name,
		      to_string(result).c_str(), to_string(stats).c_str());
	}

	// multiplayer
	void hostingMultiplayerGame(const MultiplayerGameInfo& info) override
	{
		debug(LOG_ACTIVITY, "- hostingMultiplayerGame: %s; isLobbyGame: %s", info.game.name,
		      (info.lobbyGameId != 0) ? "true" : "false");
	}

	void joinedMultiplayerGame(const MultiplayerGameInfo& info) override
	{
		debug(LOG_ACTIVITY, "- joinedMultiplayerGame: %s", info.game.name);
	}

	void updateMultiplayerGameInfo(const MultiplayerGameInfo& info) override
	{
		debug(LOG_ACTIVITY,
		      "- updateMultiplayerGameInfo: (name: %s), (map: %s), maxPlayers: %u, numAvailableSlots: %zu, numHumanPlayers: %u, numAIBotPlayers: %u",
		      info.game.name, info.game.map, info.maxPlayers, (size_t)info.numAvailableSlots, info.numHumanPlayers,
		      info.numAIBotPlayers);
	}

	void startedMultiplayerGame(const MultiplayerGameInfo& info) override
	{
		debug(LOG_ACTIVITY, "- startedMultiplayerGame: %s", info.game.name);
	}

	void endedMultiplayerGame(const MultiplayerGameInfo& info, GameEndReason result,
	                                  const END_GAME_STATS_DATA& stats) override
	{
		debug(LOG_ACTIVITY, "- endedMultiplayerGame: %s; result: %s; stats: (%s)", info.game.name,
		      to_string(result).c_str(), to_string(stats).c_str());
	}

	// changing settings
	void changedSetting(const std::string& settingKey, const std::string& settingValue) override
	{
		debug(LOG_ACTIVITY, "- changedSetting: %s = %s", settingKey.c_str(), settingValue.c_str());
	}

	// cheats used
	void cheatUsed(const std::string& cheatName) override
	{
		debug(LOG_ACTIVITY, "- cheatUsed: %s", cheatName.c_str());
	}

	// loaded mods changed
	void loadedModsChanged(const std::vector<Sha256>& loadedModHashes) override
	{
		debug(LOG_ACTIVITY, "- loadedModsChanged: %s", modListToStr(loadedModHashes).c_str());
	}

private:
	[[nodiscard]] static std::string modListToStr(const std::vector<Sha256>& modHashes)
	{
		if (modHashes.empty())
		{
			return "[no mods]";
		}
		std::string result = "[" + std::to_string(modHashes.size()) + " mods]:";
		for (auto& modHash : modHashes)
		{
			result += std::string(" ") + modHash.toString();
		}
		return result;
	}
};

ActivityDBProtocol::~ActivityDBProtocol() = default;

// Should be thread-safe
class ActivityDatabase : public ActivityDBProtocol
{
private:
#define FIRST_LAUNCH_DATE_KEY "first_launch"
public:
	// Caller is expected to handle thrown exceptions
	explicit ActivityDatabase(const std::string& activityDatabasePath)
	{
		db = std::make_unique<SQLite::Database>(
			activityDatabasePath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
		db->exec("PRAGMA journal_mode=WAL");
		createTables();
		query_findValueByName = std::make_unique<SQLite::Statement>(
			*db, "SELECT value FROM general_kv_storage WHERE name = ?");
		query_insertValueForName = std::make_unique<SQLite::Statement>(
			*db, "INSERT OR IGNORE INTO general_kv_storage(name, value) VALUES(?, ?)");
		query_updateValueForName = std::make_unique<SQLite::Statement>(
			*db, "UPDATE general_kv_storage SET value = ? WHERE name = ?");
	}

public:
	// Must be thread-safe
	std::string getFirstLaunchDate() const override
	{
		auto result = getValue(FIRST_LAUNCH_DATE_KEY);
		ASSERT_OR_RETURN("", result.has_value(), "Should always be initialized");
		return result.value();
	}

private:
	// Must be thread-safe
	optional<std::string> getValue(const std::string& name) const
	{
		if (name.empty())
		{
			return nullopt;
		}

		std::lock_guard<std::mutex> guard(db_mutex);

		optional<std::string> result;
		try
		{
			query_findValueByName->bind(1, name);
			if (query_findValueByName->executeStep())
			{
				result = query_findValueByName->getColumn(0).getString();
			}
		}
		catch (const std::exception& e)
		{
			debug(LOG_ERROR, "Failure to query database for key; error: %s", e.what());
			result = nullopt;
		}
		try
		{
			query_findValueByName->reset();
		}
		catch (const std::exception& e)
		{
			debug(LOG_ERROR, "Failed to reset prepared statement; error: %s", e.what());
		}
		return result;
	}

	// Must be thread-safe
	bool setValue(std::string const& name, std::string const& value)
	{
		if (name.empty())
		{
			return false;
		}

		std::lock_guard<std::mutex> guard(db_mutex);

		try
		{
			// Begin transaction
			SQLite::Transaction transaction(*db);

			query_insertValueForName->bind(1, name);
			query_insertValueForName->bind(2, value);
			if (query_insertValueForName->exec() == 0)
			{
				query_updateValueForName->bind(1, value);
				query_updateValueForName->bind(2, name);
				if (query_updateValueForName->exec() == 0)
				{
					debug(LOG_WARNING, "Failed to update value for key (%s)", name.c_str());
				}
				query_updateValueForName->reset();
			}
			query_insertValueForName->reset();

			// Commit transaction
			transaction.commit();
			return true;
		}
		catch (const std::exception& e)
		{
			debug(LOG_ERROR, "Update / insert failed; error: %s", e.what());
			// continue on to try to reset prepared statements
		}

		try
		{
			query_updateValueForName->reset();
			query_insertValueForName->reset();
		}
		catch (const std::exception& e)
		{
			debug(LOG_ERROR, "Failed to reset prepared statement; error: %s", e.what());
		}
		return false;
	}

private:
	void createTables()
	{
		SQLite::Transaction transaction(*db);
		if (!db->tableExists("general_kv_storage"))
		{
			db->exec("CREATE TABLE general_kv_storage (local_id INTEGER PRIMARY KEY, name TEXT UNIQUE, value TEXT)");
		}
		// initialize first launch date if it doesn't exist
		db->exec(
			"INSERT OR IGNORE INTO general_kv_storage(name, value) VALUES(\"" FIRST_LAUNCH_DATE_KEY "\", date('now'))");
		transaction.commit();
	}

private:
	mutable std::mutex db_mutex;
	std::unique_ptr<SQLite::Database> db; // Must be the first-listed SQLite member variable so it is destructed last
	std::unique_ptr<SQLite::Statement> query_findValueByName;
	std::unique_ptr<SQLite::Statement> query_insertValueForName;
	std::unique_ptr<SQLite::Statement> query_updateValueForName;
};

ActivityManager::ActivityManager()
{
	ASSERT_OR_RETURN(, PHYSFS_isInit() != 0, "PHYSFS must be initialized before the ActivityManager is created");
	// init ActivityDatabase
	const char* pWriteDir = PHYSFS_getWriteDir();
	ASSERT(pWriteDir != nullptr, "PHYSFS_getWriteDir returned null");
	if (pWriteDir)
	{
		std::string statsDBPath = std::string(pWriteDir) + PHYSFS_getDirSeparator() + "stats.db";
		try
		{
			activityDatabase = std::make_shared<ActivityDatabase>(statsDBPath);
		}
		catch (std::exception& e)
		{
			// error loading SQLite database
			debug(LOG_ERROR, "Unable to load or initialize SQLite3 database (%s); error: %s", statsDBPath.c_str(),
			      e.what());
		}
	}
}

ActivityManager& ActivityManager::instance()
{
	static ActivityManager sharedInstance = ActivityManager();
	return sharedInstance;
}

bool ActivityManager::initialize()
{
	addActivitySink(std::make_shared<LoggingActivitySink>());
	return true;
}

void ActivityManager::shutdown()
{
	// Free up the activity sinks
	activitySinks.clear();

	// Close activityDatabase
	activityDatabase.reset();
}

void ActivityManager::addActivitySink(const std::shared_ptr<ActivitySink>& sink)
{
	activitySinks.push_back(sink);
}

void ActivityManager::removeActivitySink(const std::shared_ptr<ActivitySink>& sink)
{
	activitySinks.erase(std::remove(activitySinks.begin(), activitySinks.end(), sink));
}

GAME_MODE ActivityManager::getCurrentGameMode() const
{
	return currentMode;
}

GAME_MODE currentGameTypeToMode()
{
	GAME_MODE mode = GAME_MODE::CAMPAIGN;
	if (challengeActive)
	{
		mode = GAME_MODE::CHALLENGE;
	}
	else if (game.type == LEVEL_TYPE::SKIRMISH)
	{
		mode = (NetPlay.bComms) ? GAME_MODE::MULTIPLAYER : GAME_MODE::SKIRMISH;
	}
	else if (game.type == LEVEL_TYPE::CAMPAIGN)
	{
		mode = GAME_MODE::CAMPAIGN;
	}
	return mode;
}

void ActivityManager::startingGame()
{
	GAME_MODE mode = currentGameTypeToMode();
	bEndedCurrentMission = false;

	currentMode = mode;
}

void ActivityManager::startingSavedGame()
{
	GAME_MODE mode = currentGameTypeToMode();
	bEndedCurrentMission = false;

	if (mode == GAME_MODE::SKIRMISH || (mode == GAME_MODE::MULTIPLAYER && NETisReplay()))
	{
		// synthesize an "update multiplay game data" call on skirmish save game load (or loading MP replay)
		ActivityManager::instance().updateMultiplayGameData(game, ingame, false);
	}

	currentMode = mode;

	if (cachedLoadedLevelEvent)
	{
		// process a (delayed) loaded level event
		loadedLevel(cachedLoadedLevelEvent->type, cachedLoadedLevelEvent->levelName);
		delete cachedLoadedLevelEvent;
		cachedLoadedLevelEvent = nullptr;
	}
}

void ActivityManager::loadedLevel(LEVEL_TYPE type, const std::string& levelName)
{
	bEndedCurrentMission = false;

	if (currentMode == GAME_MODE::MENUS)
	{
		// hit a case where startedGameMode is called *after* loadedLevel, so cache the loadedLevel call
		// (for example, on save game load, the game mode isn't set until the save is loaded)
		ASSERT(cachedLoadedLevelEvent == nullptr, "Missed a cached loaded level event?");
		if (cachedLoadedLevelEvent) delete cachedLoadedLevelEvent;
		cachedLoadedLevelEvent = new LoadedLevelEvent{type, levelName};
		return;
	}

	lastLoadedLevelEvent.type = type;
	lastLoadedLevelEvent.levelName = levelName;

	switch (currentMode)
	{
	case GAME_MODE::CAMPAIGN:
		for (const auto& sink : activitySinks) { sink->startedCampaignMission(getCampaignName(), levelName); }
		break;
	case GAME_MODE::CHALLENGE:
		for (const auto& sink : activitySinks) { sink->startedChallenge(currentChallengeName()); }
		break;
	case GAME_MODE::SKIRMISH:
		for (const auto& sink : activitySinks) { sink->startedSkirmishGame(currentMultiplayGameInfo); }
		break;
	case GAME_MODE::MULTIPLAYER:
		for (const auto& sink : activitySinks) { sink->startedMultiplayerGame(currentMultiplayGameInfo); }
		break;
	default:
		debug(LOG_ACTIVITY, "loadedLevel: %s; Unhandled case: %u", levelName.c_str(), (unsigned int)currentMode);
	}
}

void ActivityManager::_endedMission(GameEndReason result, const END_GAME_STATS_DATA& stats, bool cheatsUsed)
{
	if (bEndedCurrentMission) return;

	lastLobbyGameJoinAttempt.clear();

	switch (currentMode)
	{
	case GAME_MODE::CAMPAIGN:
		for (const auto& sink : activitySinks)
		{
			sink->endedCampaignMission(getCampaignName(), lastLoadedLevelEvent.levelName, result, stats, cheatsUsed);
		}
		break;
	case GAME_MODE::CHALLENGE:
		for (const auto& sink : activitySinks) { sink->endedChallenge(currentChallengeName(), result, stats, cheatsUsed); }
		break;
	case GAME_MODE::SKIRMISH:
		for (const auto& sink : activitySinks) { sink->endedSkirmishGame(currentMultiplayGameInfo, result, stats); }
		break;
	case GAME_MODE::MULTIPLAYER:
		for (const auto& sink : activitySinks) { sink->endedMultiplayerGame(currentMultiplayGameInfo, result, stats); }
		break;
	default:
		debug(LOG_ACTIVITY, "endedMission: Unhandled case: %u", (unsigned int)currentMode);
	}
	bEndedCurrentMission = true;
}

void ActivityManager::completedMission(bool result, const END_GAME_STATS_DATA& stats, bool cheatsUsed)
{
	_endedMission(result ? GameEndReason::WON : GameEndReason::LOST, stats, cheatsUsed);
}

void ActivityManager::quitGame(const END_GAME_STATS_DATA& stats, bool cheatsUsed)
{
	if (currentMode != GAME_MODE::MENUS)
	{
		_endedMission(GameEndReason::QUIT, stats, cheatsUsed);
	}

	currentMode = GAME_MODE::MENUS;
}

void ActivityManager::preSystemShutdown()
{
	// Synthesize appropriate events, as needed
	// For example, may need to synthesize a "quitGame" event if the user quit directly from window menus, etc
	if (currentMode != GAME_MODE::MENUS)
	{
		// quitGame was never generated - synthesize it
		ActivityManager::instance().quitGame(collectEndGameStatsData(), Cheated);
	}
}

void ActivityManager::navigateToMenu(const std::string& menuName)
{
	for (const auto& sink : activitySinks) { sink->navigatedToMenu(menuName); }
}

void ActivityManager::beginLoadingSettings()
{
	bIsLoadingConfiguration = true;
}

void ActivityManager::changedSetting(const std::string& settingKey, const std::string& settingValue)
{
	if (bIsLoadingConfiguration) return;

	for (const auto& sink : activitySinks) { sink->changedSetting(settingKey, settingValue); }
}

void ActivityManager::endLoadingSettings()
{
	bIsLoadingConfiguration = false;
}

// cheats used
void ActivityManager::cheatUsed(const std::string& cheatName)
{
	for (const auto& sink : activitySinks) { sink->cheatUsed(cheatName); }
}

// mods reloaded / possibly changed
void ActivityManager::rebuiltSearchPath()
{
	auto newLoadedModHashes = getModHashList();
	if (!lastLoadedMods.has_value() || newLoadedModHashes != lastLoadedMods.value())
	{
		// list of loaded mods changed!
		for (const auto& sink : activitySinks) { sink->loadedModsChanged(newLoadedModHashes); }
		lastLoadedMods = newLoadedModHashes;
	}
}

// called when a joinable multiplayer game is hosted
// lobbyGameId is 0 if the lobby can't be contacted / the game is not registered with the lobby
void ActivityManager::hostGame(const char* SessionName, const char* PlayerName, const char* lobbyAddress,
                               unsigned int lobbyPort, const ListeningInterfaces& listeningInterfaces,
                               uint32_t lobbyGameId /*= 0*/)
{
	currentMode = GAME_MODE::HOSTING_IN_LOBBY;

	// updateMultiplayGameData should have already been called with the main details before this function is called

	currentMultiplayGameInfo.hostName = PlayerName;
	currentMultiplayGameInfo.listeningInterfaces = listeningInterfaces;
	currentMultiplayGameInfo.lobbyAddress = (lobbyAddress != nullptr) ? lobbyAddress : std::string();
	currentMultiplayGameInfo.lobbyPort = lobbyPort;
	currentMultiplayGameInfo.lobbyGameId = lobbyGameId;
	currentMultiplayGameInfo.isHost = true;

	for (const auto& sink : activitySinks) { sink->hostingMultiplayerGame(currentMultiplayGameInfo); }
}

void ActivityManager::hostGameLobbyServerDisconnect()
{
	if (currentMode != GAME_MODE::HOSTING_IN_LOBBY)
	{
		debug(LOG_ACTIVITY, "Unexpected call to hostGameLobbyServerDisconnect - currentMode (%u) - ignoring",
		      (unsigned int)currentMode);
		return;
	}

	if (currentMultiplayGameInfo.lobbyGameId == 0)
	{
		debug(LOG_ACTIVITY, "Unexpected call to hostGameLobbyServerDisconnect - prior lobbyGameId is %u - ignoring",
		      currentMultiplayGameInfo.lobbyGameId);
		return;
	}

	// The lobby server has disconnected the host (us)
	// Hence any prior lobbyGameId, etc is now invalid
	currentMultiplayGameInfo.lobbyAddress.clear();
	currentMultiplayGameInfo.lobbyPort = 0;
	currentMultiplayGameInfo.lobbyGameId = 0;

	// Inform the ActivitySinks
	// Trigger a new hostingMultiplayerGame event
	for (const auto& sink : activitySinks) { sink->hostingMultiplayerGame(currentMultiplayGameInfo); }
}

void ActivityManager::hostLobbyQuit()
{
	if (currentMode != GAME_MODE::HOSTING_IN_LOBBY)
	{
		debug(LOG_ACTIVITY, "Unexpected call to hostLobbyQuit - currentMode (%u) - ignoring",
		      (unsigned int)currentMode);
		return;
	}
	currentMode = GAME_MODE::MENUS;

	// Notify the ActivitySink that we've left the game lobby
	for (const auto& sink : activitySinks) { sink->leftMultiplayerGameLobby(true, getLobbyError()); }
}

// called when attempting to join a lobby game
void ActivityManager::willAttemptToJoinLobbyGame(const std::string& lobbyAddress, unsigned int lobbyPort,
                                                 uint32_t lobbyGameId,
                                                 const std::vector<JoinConnectionDescription>& connections)
{
	lastLobbyGameJoinAttempt.lobbyAddress = lobbyAddress;
	lastLobbyGameJoinAttempt.lobbyPort = lobbyPort;
	lastLobbyGameJoinAttempt.lobbyGameId = lobbyGameId;
	lastLobbyGameJoinAttempt.connections = connections;
}

// called when an attempt to join fails
void ActivityManager::joinGameFailed(const std::vector<JoinConnectionDescription>& connection_list)
{
	lastLobbyGameJoinAttempt.clear();
}

// called when joining a multiplayer game
void ActivityManager::joinGameSucceeded(const char* host, uint32_t port)
{
	currentMode = GAME_MODE::JOINING_IN_PROGRESS;
	currentMultiplayGameInfo.isHost = false;

	// If the host and port match information in the lastLobbyGameJoinAttempt.connections,
	// store the lastLobbyGameJoinAttempt lookup info in currentMultiplayGameInfo
	bool joinedLobbyGame = false;
	for (const auto& connection : lastLobbyGameJoinAttempt.connections)
	{
		if ((connection.host == host) && (connection.port == port))
		{
			joinedLobbyGame = true;
			break;
		}
	}
	if (joinedLobbyGame)
	{
		currentMultiplayGameInfo.lobbyAddress = lastLobbyGameJoinAttempt.lobbyAddress;
		currentMultiplayGameInfo.lobbyPort = lastLobbyGameJoinAttempt.lobbyPort;
		currentMultiplayGameInfo.lobbyGameId = lastLobbyGameJoinAttempt.lobbyGameId;
	}
	lastLobbyGameJoinAttempt.clear();

	// NOTE: This is called once the join is accepted, but before all game information has been received from the host
	// Therefore, delay ActivitySink::joinedMultiplayerGame until after we receive the initial game data
}

void ActivityManager::joinedLobbyQuit()
{
	if (currentMode != GAME_MODE::JOINING_IN_LOBBY)
	{
		if (currentMode != GAME_MODE::MENUS)
		{
			debug(LOG_ACTIVITY, "Unexpected call to joinedLobbyQuit - currentMode (%u) - ignoring",
			      (unsigned int)currentMode);
		}
		return;
	}
	currentMode = GAME_MODE::MENUS;

	// Notify the ActivitySink that we've left the game lobby
	for (const auto& sink : activitySinks)
  {
    sink->leftMultiplayerGameLobby(false, getLobbyError());
  }
}

// for skirmish / multiplayer, provide additional data / state
void ActivityManager::updateMultiplayGameData(const MULTIPLAYERGAME& multiGame, const MULTIPLAYERINGAME& multiInGame,
                                              optional<bool> privateGame)
{
	uint8_t maxPlayers = multiGame.maxPlayers;
	uint8_t numAIBotPlayers = 0;
	uint8_t numHumanPlayers = 0;
	uint8_t numAvailableSlots = 0;
	uint8_t numSpectators = 0;
	uint8_t numOpenSpectatorSlots = 0;

	for (size_t index = 0; index < std::min<size_t>(MAX_PLAYERS, (size_t)multiGame.maxPlayers); ++index)
	{
		PLAYER const& p = NetPlay.players[index];
		if (p.ai == AI_CLOSED || p.isSpectator)
		{
			--maxPlayers;
		}
		else if (p.ai == AI_OPEN)
		{
			if (!p.allocated)
			{
				++numAvailableSlots;
			}
			else
			{
				++numHumanPlayers;
			}
		}
		else
		{
			if (!p.allocated)
			{
				++numAIBotPlayers;
			}
			else
			{
				++numHumanPlayers;
			}
		}
	}

	for (const auto& slot : NetPlay.players)
	{
		if (slot.isSpectator)
		{
			if (!slot.allocated)
			{
				++numOpenSpectatorSlots;
			}
			else
			{
				++numSpectators;
			}
		}
	}
	currentMultiplayGameInfo.maxPlayers = maxPlayers; // accounts for closed slots
	currentMultiplayGameInfo.numHumanPlayers = numHumanPlayers;
	currentMultiplayGameInfo.numAvailableSlots = numAvailableSlots;
	currentMultiplayGameInfo.numSpectators = numSpectators;
	currentMultiplayGameInfo.numOpenSpectatorSlots = numOpenSpectatorSlots;
	// NOTE: privateGame will currently only be up-to-date for the host
	// for a joined client, it will reflect the passworded state at the time of join
	if (privateGame.has_value())
	{
		currentMultiplayGameInfo.privateGame = privateGame.value();
	}

	currentMultiplayGameInfo.game = multiGame;
	currentMultiplayGameInfo.numAIBotPlayers = numAIBotPlayers;
	currentMultiplayGameInfo.currentPlayerIdx = selectedPlayer;
	currentMultiplayGameInfo.players = NetPlay.players;
	currentMultiplayGameInfo.players.resize(multiGame.maxPlayers);

	currentMultiplayGameInfo.limit_no_tanks = (multiInGame.flags & MPFLAGS_NO_TANKS) != 0;
	currentMultiplayGameInfo.limit_no_cyborgs = (multiInGame.flags & MPFLAGS_NO_CYBORGS) != 0;
	currentMultiplayGameInfo.limit_no_vtols = (multiInGame.flags & MPFLAGS_NO_VTOLS) != 0;
	currentMultiplayGameInfo.limit_no_uplink = (multiInGame.flags & MPFLAGS_NO_UPLINK) != 0;
	currentMultiplayGameInfo.limit_no_lassat = (multiInGame.flags & MPFLAGS_NO_LASSAT) != 0;
	currentMultiplayGameInfo.force_structure_limits = (multiInGame.flags & MPFLAGS_FORCELIMITS) != 0;

	currentMultiplayGameInfo.structureLimits = multiInGame.structureLimits;

	currentMultiplayGameInfo.isReplay = NETisReplay();

	if (currentMode == GAME_MODE::JOINING_IN_PROGRESS || currentMode ==
		GAME_MODE::JOINING_IN_LOBBY)
	{
		currentMultiplayGameInfo.hostName = currentMultiplayGameInfo.players[0].name; // host is always player index 0?
	}

	if (currentMode == GAME_MODE::HOSTING_IN_LOBBY || currentMode ==
		GAME_MODE::JOINING_IN_LOBBY)
	{
		for (const auto& sink : activitySinks) { sink->updateMultiplayerGameInfo(currentMultiplayGameInfo); }
	}
	else if (currentMode == GAME_MODE::JOINING_IN_PROGRESS)
	{
		// Have now received the initial game data, so trigger ActivitySink::joinedMultiplayerGame
		currentMode = GAME_MODE::JOINING_IN_LOBBY;
		for (const auto& sink : activitySinks) { sink->joinedMultiplayerGame(currentMultiplayGameInfo); }
	}
}

// called on the host when the host kicks a player
void ActivityManager::hostKickPlayer(const PLAYER& player, LOBBY_ERROR_TYPES kick_type, const std::string& reason)
{
	/* currently, no-op */
}

// called on the kicked player when they are kicked by another player
void ActivityManager::wasKickedByPlayer(const PLAYER& kicker, LOBBY_ERROR_TYPES kick_type, const std::string& reason)
{
	/* currently, no-op */
}
