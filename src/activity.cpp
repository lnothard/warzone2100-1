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


ActivityManager::LoadedLevelEvent::LoadedLevelEvent(LEVEL_TYPE type, std::string levelName)
  : type{type}, levelName{std::move(levelName)}
{
}

std::string SkirmishGameInfo::gameName() const
{
  return game.name;
}

std::string SkirmishGameInfo::mapName() const
{
  return game.map;
}

unsigned SkirmishGameInfo::numberOfPlayers() const
{
  return numAIBotPlayers + 1;
}

bool SkirmishGameInfo::hasLimits() const
{
  return limit_no_tanks || limit_no_cyborgs || limit_no_vtols ||
         limit_no_uplink || limit_no_lassat || force_structure_limits;
}

ActivityDBProtocol const* ActivityManager::getRecord() const
{
  return activityDatabase.get();
}

std::string ActivitySink::getTeamDescription(SkirmishGameInfo const& info)
{
	if (!alliancesSetTeamsBeforeGame(info.game.alliance)) {
		return "";
	}

	std::map<int, size_t> teamIdToCountOfPlayers;
	for (auto index = 0; index < std::min<size_t>(info.players.size(), game.maxPlayers); ++index)
	{
		auto const& p = NetPlay.players[index];
		if (p.ai == AI_CLOSED || p.ai == AI_OPEN && p.isSpectator) {
			// closed slot or spectator slot - skip
			continue;
		}
    // bot player
    teamIdToCountOfPlayers[p.team]++;
	}

	if (teamIdToCountOfPlayers.size() <= 1) {
		// does not have multiple teams
		return "";
	}

	std::string teamDescription;
	for (const auto& team : teamIdToCountOfPlayers)
	{
		if (!teamDescription.empty()) {
			teamDescription += "v";
		}
		teamDescription += std::to_string(team.second);
	}
	return teamDescription;
}

std::string to_string(GAME_END_REASON const& reason)
{
  switch (reason) {
    case GAME_END_REASON::WON:
      return "Won";
    case GAME_END_REASON::LOST:
      return "Lost";
    case GAME_END_REASON::QUIT:
      return "Quit";
  }
}

std::string to_string(END_GAME_STATS_DATA const& stats)
{
	return astringf("numUnits: %u, missionStartedTime: %u, unitsBuilt: %u, unitsLost: %u, unitsKilled: %u",
	                stats.numUnits, stats.missionData.missionStarted, stats.missionData.unitsBuilt,
	                stats.missionData.unitsLost, stats.missionData.unitsKilled);
}

void LoggingActivitySink::navigatedToMenu(std::string const& menuName) const
{
  debug(LOG_ACTIVITY, "- navigatedToMenu: %s", menuName.c_str());
}

void LoggingActivitySink::startedCampaignMission(std::string const& campaign, std::string const& levelName) const
{
  debug(LOG_ACTIVITY, "- startedCampaignMission: %s:%s", campaign.c_str(), levelName.c_str());
}

void LoggingActivitySink::endedCampaignMission(const std::string& campaign, const std::string& levelName,
                                               GAME_END_REASON result, const END_GAME_STATS_DATA& stats, bool cheatsUsed) const
{
  debug(LOG_ACTIVITY, "- endedCampaignMission: %s:%s; result: %s; stats: (%s)", campaign.c_str(),
        levelName.c_str(), to_string(result).c_str(), to_string(stats).c_str());
}

void LoggingActivitySink::startedChallenge(const std::string& challengeName) const
{
  debug(LOG_ACTIVITY, "- startedChallenge: %s", challengeName.c_str());
}

void LoggingActivitySink::endedChallenge(const std::string& challengeName, GAME_END_REASON result,
                    const END_GAME_STATS_DATA& stats, bool cheatsUsed) const
{
  debug(LOG_ACTIVITY, "- endedChallenge: %s; result: %s; stats: (%s)", challengeName.c_str(),
        to_string(result).c_str(), to_string(stats).c_str());
}

void LoggingActivitySink::startedSkirmishGame(const SkirmishGameInfo& info) const
{
  debug(LOG_ACTIVITY, "- startedSkirmishGame: %s", info.game.name);
}

void LoggingActivitySink::endedSkirmishGame(const SkirmishGameInfo& info, GAME_END_REASON result,
                       const END_GAME_STATS_DATA& stats) const
{
  debug(LOG_ACTIVITY, "- endedSkirmishGame: %s; result: %s; stats: (%s)", info.game.name,
        to_string(result).c_str(), to_string(stats).c_str());
}

void LoggingActivitySink::hostingMultiplayerGame(const MultiplayerGameInfo& info) const
{
  debug(LOG_ACTIVITY, "- hostingMultiplayerGame: %s; isLobbyGame: %s", info.game.name,
      (info.lobbyGameId != 0) ? "true" : "false");
}

void LoggingActivitySink::joinedMultiplayerGame(const MultiplayerGameInfo& info) const
{
  debug(LOG_ACTIVITY, "- joinedMultiplayerGame: %s", info.game.name);
}

void LoggingActivitySink::updateMultiplayerGameInfo(const MultiplayerGameInfo& info) const
{
  debug(LOG_ACTIVITY,
        "- updateMultiplayerGameInfo: (name: %s), (map: %s), maxPlayers: %u, numAvailableSlots: %zu,"
        " numHumanPlayers: %u, numAIBotPlayers: %u", info.game.name, info.game.map, info.maxPlayers,
        (size_t)info.numAvailableSlots, info.numHumanPlayers, info.numAIBotPlayers);
}

void LoggingActivitySink::startedMultiplayerGame(const MultiplayerGameInfo& info) const
{
  debug(LOG_ACTIVITY, "- startedMultiplayerGame: %s", info.game.name);
}

void LoggingActivitySink::endedMultiplayerGame(const MultiplayerGameInfo& info, GAME_END_REASON result,
                          const END_GAME_STATS_DATA& stats) const
{
  debug(LOG_ACTIVITY, "- endedMultiplayerGame: %s; result: %s; stats: (%s)", info.game.name,
        to_string(result).c_str(), to_string(stats).c_str());
}

void LoggingActivitySink::changedSetting(const std::string& settingKey, const std::string& settingValue) const
{
  debug(LOG_ACTIVITY, "- changedSetting: %s = %s", settingKey.c_str(), settingValue.c_str());
}

void LoggingActivitySink::cheatUsed(const std::string& cheatName) const
{
  debug(LOG_ACTIVITY, "- cheatUsed: %s", cheatName.c_str());
}

void LoggingActivitySink::loadedModsChanged(const std::vector<Sha256>& loadedModHashes) const
{
  debug(LOG_ACTIVITY, "- loadedModsChanged: %s", modListToStr(loadedModHashes).c_str());
}

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

	// Must be thread-safe
	[[nodiscard]] std::string getFirstLaunchDate() const override
	{
		auto result = getValue(FIRST_LAUNCH_DATE_KEY);
		ASSERT_OR_RETURN("", result.has_value(), "Should always be initialized");
		return result.value();
	}

private:
	// Must be thread-safe
	optional<std::string> getValue(const std::string& name) const
	{
		if (name.empty()) {
			return nullopt;
		}

		std::lock_guard<std::mutex> guard(db_mutex);
		optional<std::string> result;
		try {
			query_findValueByName->bind(1, name);
			if (query_findValueByName->executeStep()) {
				result = query_findValueByName->getColumn(0).getString();
			}
		}
		catch (const std::exception& e) {
			debug(LOG_ERROR, "Failure to query database for key; error: %s", e.what());
			result = nullopt;
		}

		try {
			query_findValueByName->reset();
		}
		catch (const std::exception& e) {
			debug(LOG_ERROR, "Failed to reset prepared statement; error: %s", e.what());
		}
		return result;
	}

	// Must be thread-safe
	bool setValue(std::string const& name, std::string const& value)
	{
		if (name.empty()) {
			return false;
		}

		std::lock_guard<std::mutex> guard(db_mutex);
		try {
			// Begin transaction
			SQLite::Transaction transaction(*db);

			query_insertValueForName->bind(1, name);
			query_insertValueForName->bind(2, value);

      if (query_insertValueForName->exec() != 0) {
        query_insertValueForName->reset();
        // Commit transaction
        transaction.commit();
        return true;
      }

      query_updateValueForName->bind(1, value);
      query_updateValueForName->bind(2, name);
      if (query_updateValueForName->exec() == 0) {
        debug(LOG_WARNING, "Failed to update value for key (%s)", name.c_str());
      }
      query_updateValueForName->reset();
    }
		catch (const std::exception& e) {
			debug(LOG_ERROR, "Update / insert failed; error: %s", e.what());
			// continue on to try to reset prepared statements
		}

		try {
			query_updateValueForName->reset();
			query_insertValueForName->reset();
		}
		catch (const std::exception& e) {
			debug(LOG_ERROR, "Failed to reset prepared statement; error: %s", e.what());
		}
		return false;
	}
private:
	void createTables()
	{
		SQLite::Transaction transaction(*db);
		if (!db->tableExists("general_kv_storage")) {
			db->exec("CREATE TABLE general_kv_storage (local_id INTEGER PRIMARY "
               "KEY, name TEXT UNIQUE, value TEXT)");
		}
		// initialize first launch date if it doesn't exist
		db->exec("INSERT OR IGNORE INTO general_kv_storage(name, value) VALUES(\""
             FIRST_LAUNCH_DATE_KEY "\", date('now'))");
		transaction.commit();
	}

private:
	mutable std::mutex db_mutex;
	std::unique_ptr<SQLite::Database> db; // Must be the first-listed SQLite member variable, so it is destructed last
	std::unique_ptr<SQLite::Statement> query_findValueByName;
	std::unique_ptr<SQLite::Statement> query_insertValueForName;
	std::unique_ptr<SQLite::Statement> query_updateValueForName;
};

ActivityManager::ActivityManager()
{
	ASSERT_OR_RETURN(, PHYSFS_isInit() != 0, "PHYSFS must be initialized before the ActivityManager is created");

	const char* pWriteDir = PHYSFS_getWriteDir();
	ASSERT_OR_RETURN(, pWriteDir != nullptr, "PHYSFS_getWriteDir returned null");

  auto statsDBPath = std::string(pWriteDir) + PHYSFS_getDirSeparator() + "stats.db";
  try {
    activityDatabase = std::make_unique<ActivityDatabase>(statsDBPath);
  }
  catch (std::exception &e) {
    // error loading SQLite database
    debug(LOG_ERROR, "Unable to load or initialize SQLite3 database (%s); error: %s",
          statsDBPath.c_str(), e.what());
  }
}

ActivityManager& ActivityManager::instance()
{
	static ActivityManager sharedInstance = ActivityManager();
	return sharedInstance;
}

void ActivityManager::initialize()
{
	addActivitySink(std::make_unique<LoggingActivitySink>());
}

void ActivityManager::shutdown()
{
	activitySinks.clear();
	activityDatabase.reset();
}

void ActivityManager::addActivitySink(std::unique_ptr<ActivitySink>&& sink)
{
	activitySinks.push_back(sink);
}

void ActivityManager::removeActivitySink(std::shared_ptr<ActivitySink> const& sink)
{
  std::erase(activitySinks, sink);
}

GAME_MODE ActivityManager::getCurrentGameMode() const
{
	return currentMode;
}

GAME_MODE currentGameTypeToMode()
{
	if (challengeActive) {
		return GAME_MODE::CHALLENGE;
	}
	if (game.type == LEVEL_TYPE::SKIRMISH) {
		return NetPlay.bComms ? GAME_MODE::MULTIPLAYER : GAME_MODE::SKIRMISH;
	}
	if (game.type == LEVEL_TYPE::CAMPAIGN) {
		return GAME_MODE::CAMPAIGN;
	}
  return GAME_MODE::CAMPAIGN;
}

void ActivityManager::startingGame()
{
	auto mode = currentGameTypeToMode();
	bEndedCurrentMission = false;
	currentMode = mode;
}

void ActivityManager::startingSavedGame()
{
	auto mode = currentGameTypeToMode();
	bEndedCurrentMission = false;

	if (mode == GAME_MODE::SKIRMISH || mode == GAME_MODE::MULTIPLAYER && NETisReplay()) {
		// synthesize an "update multiplay game data" call on skirmish save game load (or loading MP replay)
		ActivityManager::instance().updateMultiplayGameData(game, ingame, false);
	}

	currentMode = mode;
	if (cachedLoadedLevelEvent) {
		// process a (delayed) loaded level event
		loadedLevel(cachedLoadedLevelEvent->type, cachedLoadedLevelEvent->levelName);
		cachedLoadedLevelEvent = nullptr;
	}
}

void ActivityManager::loadedLevel(LEVEL_TYPE type, const std::string& levelName)
{
	bEndedCurrentMission = false;
	if (currentMode == GAME_MODE::MENUS) {
		// hit a case where startedGameMode is called *after* loadedLevel, so cache the loadedLevel call
		// (for example, on save game load, the game mode isn't set until the save is loaded)
		ASSERT(cachedLoadedLevelEvent == nullptr, "Missed a cached loaded level event?");
		cachedLoadedLevelEvent = std::make_unique<LoadedLevelEvent>(type, levelName);
		return;
	}

	lastLoadedLevelEvent.type = type;
	lastLoadedLevelEvent.levelName = levelName;
	switch (currentMode) {
	  case GAME_MODE::CAMPAIGN:
	  	for (auto const& sink : activitySinks)
      {
        sink->startedCampaignMission(getCampaignName(), levelName);
      }
	  	break;
	  case GAME_MODE::CHALLENGE:
	  	for (auto const& sink : activitySinks)
      {
        sink->startedChallenge(currentChallengeName());
      }
	  	break;
	  case GAME_MODE::SKIRMISH:
	  	for (auto const& sink : activitySinks)
      {
        sink->startedSkirmishGame(currentMultiplayGameInfo);
      }
	  	break;
	  case GAME_MODE::MULTIPLAYER:
	  	for (auto const& sink : activitySinks)
      {
        sink->startedMultiplayerGame(currentMultiplayGameInfo);
      }
	  	break;
	  default:
	  	debug(LOG_ACTIVITY, "loadedLevel: %s; Unhandled case: %u", levelName.c_str(), (unsigned int)currentMode);
	}
}

void ActivityManager::endedMission(GAME_END_REASON result, END_GAME_STATS_DATA const& stats, bool cheatsUsed)
{
	if (bEndedCurrentMission)
    return;

  lastLobbyGameJoinAttempt.clear();
  switch (currentMode) {
    case GAME_MODE::CAMPAIGN:
      for (const auto& sink : activitySinks)
      {
        sink->endedCampaignMission(getCampaignName(), lastLoadedLevelEvent.levelName, result, stats, cheatsUsed);
      }
      break;
    case GAME_MODE::CHALLENGE:
      for (const auto& sink : activitySinks)
      {
        sink->endedChallenge(currentChallengeName(), result, stats, cheatsUsed);
      }
      break;
    case GAME_MODE::SKIRMISH:
      for (const auto& sink : activitySinks)
      {
        sink->endedSkirmishGame(currentMultiplayGameInfo, result, stats);
      }
      break;
    case GAME_MODE::MULTIPLAYER:
      for (const auto& sink : activitySinks)
      {
        sink->endedMultiplayerGame(currentMultiplayGameInfo, result, stats);
      }
      break;
    default:
      debug(LOG_ACTIVITY, "endedMission: Unhandled case: %u", (unsigned int)currentMode);
  }
	bEndedCurrentMission = true;
}

void ActivityManager::completedMission(bool result, END_GAME_STATS_DATA const& stats, bool cheatsUsed)
{
  endedMission(result ? GAME_END_REASON::WON : GAME_END_REASON::LOST, stats, cheatsUsed);
}

void ActivityManager::quitGame(END_GAME_STATS_DATA const& stats, bool cheatsUsed)
{
	if (currentMode != GAME_MODE::MENUS) {
    endedMission(GAME_END_REASON::QUIT, stats, cheatsUsed);
	}
	currentMode = GAME_MODE::MENUS;
}

void ActivityManager::preSystemShutdown()
{
	// Synthesize appropriate events, as needed
	// For example, may need to synthesize a "quitGame" event if the user quit directly from window menus, etc
	if (currentMode != GAME_MODE::MENUS) {
		// quitGame was never generated - synthesize it
		ActivityManager::instance().quitGame(collectEndGameStatsData(), Cheated);
	}
}

void ActivityManager::navigateToMenu(std::string const& menuName)
{
	for (auto const& sink : activitySinks)
  {
    sink->navigatedToMenu(menuName);
  }
}

void ActivityManager::beginLoadingSettings()
{
	bIsLoadingConfiguration = true;
}

void ActivityManager::changedSetting(std::string const& settingKey, std::string const& settingValue)
{
	if (bIsLoadingConfiguration)
    return;

	for (auto const& sink : activitySinks)
  {
    sink->changedSetting(settingKey, settingValue);
  }
}

void ActivityManager::endLoadingSettings()
{
	bIsLoadingConfiguration = false;
}

void ActivityManager::cheatUsed(std::string const& cheatName)
{
	for (auto const& sink : activitySinks)
  {
    sink->cheatUsed(cheatName);
  }
}

void ActivityManager::rebuiltSearchPath()
{
	auto newLoadedModHashes = getModHashList();
  if (lastLoadedMods.has_value() && newLoadedModHashes == lastLoadedMods.value())
    return;

  for (auto const& sink : activitySinks)
  {
    sink->loadedModsChanged(newLoadedModHashes);
  }
  lastLoadedMods = newLoadedModHashes;
}

// called when a joinable multiplayer game is hosted
// lobbyGameId is 0 if the lobby can't be contacted / the game is not registered with the lobby
void ActivityManager::hostGame(const char* SessionName, const char* PlayerName, const char* lobbyAddress,
                               unsigned lobbyPort, ListeningInterfaces const& listeningInterfaces,
                               unsigned lobbyGameId)
{
	currentMode = GAME_MODE::HOSTING_IN_LOBBY;
	currentMultiplayGameInfo.hostName = PlayerName;
	currentMultiplayGameInfo.listeningInterfaces = listeningInterfaces;
	currentMultiplayGameInfo.lobbyAddress = (lobbyAddress != nullptr) ? lobbyAddress : std::string();
	currentMultiplayGameInfo.lobbyPort = lobbyPort;
	currentMultiplayGameInfo.lobbyGameId = lobbyGameId;
	currentMultiplayGameInfo.isHost = true;

	for (auto const& sink : activitySinks)
  {
    sink->hostingMultiplayerGame(currentMultiplayGameInfo);
  }
}

void ActivityManager::hostGameLobbyServerDisconnect()
{
	if (currentMode != GAME_MODE::HOSTING_IN_LOBBY) {
		debug(LOG_ACTIVITY, "Unexpected call to hostGameLobbyServerDisconnect - currentMode (%u) - ignoring",
		      (unsigned)currentMode);
		return;
	}

	if (currentMultiplayGameInfo.lobbyGameId == 0) {
		debug(LOG_ACTIVITY, "Unexpected call to hostGameLobbyServerDisconnect - prior lobbyGameId is %u - ignoring",
		      currentMultiplayGameInfo.lobbyGameId);
		return;
	}

	// The lobby server has disconnected the host (us)
	// Hence any prior lobbyGameId, etc. is now invalid
	currentMultiplayGameInfo.lobbyAddress.clear();
	currentMultiplayGameInfo.lobbyPort = 0;
	currentMultiplayGameInfo.lobbyGameId = 0;

	// Inform the ActivitySinks
	// Trigger a new hostingMultiplayerGame event
	for (const auto& sink : activitySinks)
  {
    sink->hostingMultiplayerGame(currentMultiplayGameInfo);
  }
}

void ActivityManager::hostLobbyQuit()
{
  if (currentMode == GAME_MODE::HOSTING_IN_LOBBY) {
    currentMode = GAME_MODE::MENUS;
    return;
  }
  debug(LOG_ACTIVITY, "Unexpected call to hostLobbyQuit - currentMode (%u) - ignoring",
        (unsigned) currentMode);
}

void ActivityManager::willAttemptToJoinLobbyGame(std::string const& lobbyAddress,
                                                 unsigned lobbyPort, unsigned lobbyGameId,
                                                 std::vector<JoinConnectionDescription> const& connections)
{
	lastLobbyGameJoinAttempt.lobbyAddress = lobbyAddress;
	lastLobbyGameJoinAttempt.lobbyPort = lobbyPort;
	lastLobbyGameJoinAttempt.lobbyGameId = lobbyGameId;
	lastLobbyGameJoinAttempt.connections = connections;
}

void ActivityManager::joinGameFailed(const std::vector<JoinConnectionDescription>& connection_list)
{
	lastLobbyGameJoinAttempt.clear();
}

void ActivityManager::joinGameSucceeded(const char* host, unsigned port)
{
	currentMode = GAME_MODE::JOINING_IN_PROGRESS;
	currentMultiplayGameInfo.isHost = false;

	// If the host and port match information in the lastLobbyGameJoinAttempt.connections,
	// store the lastLobbyGameJoinAttempt lookup info in currentMultiplayGameInfo
  auto joined = std::any_of(lastLobbyGameJoinAttempt.connections.begin(),
                  lastLobbyGameJoinAttempt.connections.end(),
                  [&host, &port](auto const& conn) {
    return conn.host == host && conn.port == port;
  });

  if (joined) {
    currentMultiplayGameInfo.lobbyAddress = lastLobbyGameJoinAttempt.lobbyAddress;
    currentMultiplayGameInfo.lobbyPort = lastLobbyGameJoinAttempt.lobbyPort;
    currentMultiplayGameInfo.lobbyGameId = lastLobbyGameJoinAttempt.lobbyGameId;
  }
	lastLobbyGameJoinAttempt.clear();
}

void ActivityManager::joinedLobbyQuit()
{
  if (currentMode == GAME_MODE::JOINING_IN_LOBBY) {
    currentMode = GAME_MODE::MENUS;
    return;
  }
  if (currentMode != GAME_MODE::MENUS) {
    debug(LOG_ACTIVITY, "Unexpected call to joinedLobbyQuit - currentMode (%u) - ignoring",
          (unsigned) currentMode);
  }
}

// for skirmish / multiplayer, provide additional data / state
void ActivityManager::updateMultiplayGameData(MULTIPLAYERGAME const& multiGame,
                                              MULTIPLAYERINGAME const& multiInGame,
                                              optional<bool> privateGame)
{
	unsigned maxPlayers = multiGame.maxPlayers;
	unsigned numAIBotPlayers = 0;
	unsigned numHumanPlayers = 0;
	unsigned numAvailableSlots = 0;
	unsigned numSpectators = 0;
	unsigned numOpenSpectatorSlots = 0;

	for (auto index = 0; index < std::min<size_t>(MAX_PLAYERS, multiGame.maxPlayers); ++index)
	{
		auto const& p = NetPlay.players[index];
		if (p.ai == AI_CLOSED || p.isSpectator) {
			--maxPlayers;
      continue;
		}
    if (p.allocated) {
      ++numHumanPlayers;
      continue;
    }
    ++(p.ai == AI_OPEN ? numAvailableSlots : numAIBotPlayers);
  }

	for (auto const& slot : NetPlay.players)
	{
    if (!slot.isSpectator)
      continue;

    if (!slot.allocated) {
      ++numOpenSpectatorSlots;
      continue;
    }
    ++numSpectators;
  }
	currentMultiplayGameInfo.maxPlayers = maxPlayers; // accounts for closed slots
	currentMultiplayGameInfo.numHumanPlayers = numHumanPlayers;
	currentMultiplayGameInfo.numAvailableSlots = numAvailableSlots;
	currentMultiplayGameInfo.numSpectators = numSpectators;
	currentMultiplayGameInfo.numOpenSpectatorSlots = numOpenSpectatorSlots;

	// NOTE: privateGame will currently only be up-to-date for the host
	// for a joined client, it will reflect the passworded state at the time of join
	if (privateGame.has_value()) {
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

	if (currentMode == GAME_MODE::JOINING_IN_PROGRESS ||
      currentMode == GAME_MODE::JOINING_IN_LOBBY) {
    // host is always player index 0?
		currentMultiplayGameInfo.hostName = currentMultiplayGameInfo.players[0].name;
	}

	if (currentMode == GAME_MODE::HOSTING_IN_LOBBY ||
      currentMode == GAME_MODE::JOINING_IN_LOBBY) {
		for (auto const& sink : activitySinks)
    {
      sink->updateMultiplayerGameInfo(currentMultiplayGameInfo);
    }
    return;
  }

  if (currentMode != GAME_MODE::JOINING_IN_PROGRESS)
    return;

  // Have now received the initial game data, so trigger ActivitySink::joinedMultiplayerGame
  currentMode = GAME_MODE::JOINING_IN_LOBBY;
  for (auto const& sink1: activitySinks)
  {
    sink1->joinedMultiplayerGame(currentMultiplayGameInfo);
  }
}

std::string LoggingActivitySink::modListToStr(const std::vector<Sha256>& modHashes)
{
  if (modHashes.empty()) {
    return "[no mods]";
  }

  std::string result = "[" + std::to_string(modHashes.size()) + " mods]:";
  for (auto& modHash : modHashes)
  {
    result += std::string(" ") + modHash.toString();
  }
  return result;
}
