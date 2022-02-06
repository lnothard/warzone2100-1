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
 * @file activity.h
 *
 */

#ifndef __INCLUDED_SRC_ACTIVITY_H__
#define __INCLUDED_SRC_ACTIVITY_H__

#include <vector>
#include "multiplay.h"

enum class ALLIANCE_TYPE;
enum class LOBBY_ERROR_TYPES;
struct END_GAME_STATS_DATA;
struct PLAYER;


enum class GAME_MODE
{
  MENUS,
  TUTORIAL,
  CAMPAIGN,
  CHALLENGE,
  SKIRMISH,
  HOSTING_IN_LOBBY,
  JOINING_IN_PROGRESS,
  /// Joined but waiting on game information from host
  JOINING_IN_LOBBY,
  MULTIPLAYER,
  COUNT
};

enum class GAME_END_REASON
{
  WON,
  LOST,
  QUIT
};

struct SkirmishGameInfo
{
  virtual ~SkirmishGameInfo() = default;

  [[nodiscard]] virtual uint8_t numberOfPlayers() const;
  [[nodiscard]] std::string gameName() const;
  [[nodiscard]] std::string mapName() const;
  [[nodiscard]] bool hasLimits() const;

  MULTIPLAYERGAME game;
  uint8_t numAIBotPlayers = 0;
  size_t currentPlayerIdx = 0;

  // = the selectedPlayer global for the current client
  // (points to currently controlled player in the players array)
  std::vector<PLAYER> players;

  /// Flag for tanks disabled
  bool limit_no_tanks;
  /// Flag for cyborgs disabled
  bool limit_no_cyborgs;
  /// Flag for VTOLs disabled
  bool limit_no_vtols;
  /// Flag for Satellite Uplink disabled
  bool limit_no_uplink;
  /// Flag for Laser Satellite Command Post disabled
  bool limit_no_lassat;
  /// Flag to force structure limits
  bool force_structure_limits;

  std::vector<MULTISTRUCTLIMITS> structureLimits;
  ALLIANCE_TYPE alliances;
  bool isReplay = false;
};

struct ListeningInterfaces
{
  bool IPv4 = false;
  bool IPv6 = false;
  unsigned ipv4_port;
  unsigned ipv6_port;
};

struct MultiplayerGameInfo : public SkirmishGameInfo
{
  std::string hostName;
  std::string lobbyAddress;
  ListeningInterfaces listeningInterfaces;
  unsigned lobbyPort;
  unsigned lobbyGameId = 0;
  bool isHost;
  bool privateGame;
  uint8_t maxPlayers = 0;
  uint8_t numHumanPlayers = 0;
  uint8_t numAvailableSlots = 0;
  uint8_t numSpectators = 0;
  uint8_t numOpenSpectatorSlots = 0;
};

/**
 * Subclass ActivitySink to implement a custom handler for higher-level
 * game-state event callbacks
 */
class ActivitySink
{
public:
	virtual ~ActivitySink() = default;

	virtual void navigatedToMenu(std::string const& menuName) = 0;
	virtual void startedCampaignMission(std::string const& campaign, std::string const& levelName) = 0;
	virtual void endedCampaignMission(std::string const& campaign, std::string const& levelName,
                                    GAME_END_REASON result, END_GAME_STATS_DATA stats, bool cheatsUsed) = 0;
	virtual void startedChallenge(std::string const& challengeName) = 0;
	virtual void endedChallenge(std::string const& challengeName, GAME_END_REASON result,
                              END_GAME_STATS_DATA const& stats, bool cheatsUsed) = 0;
	virtual void startedSkirmishGame(SkirmishGameInfo const& info) = 0;
	virtual void endedSkirmishGame(SkirmishGameInfo const& info, GAME_END_REASON result,
                                 END_GAME_STATS_DATA const& stats) = 0;
	virtual void hostingMultiplayerGame(MultiplayerGameInfo const& info) = 0;
	virtual void joinedMultiplayerGame(MultiplayerGameInfo const& info) = 0;
	virtual void updateMultiplayerGameInfo(MultiplayerGameInfo const& info) = 0;
	virtual void leftMultiplayerGameLobby(bool wasHost, LOBBY_ERROR_TYPES type) = 0;
	virtual void startedMultiplayerGame(MultiplayerGameInfo const& info) = 0;
	virtual void endedMultiplayerGame(MultiplayerGameInfo const& info, GAME_END_REASON result,
                                    END_GAME_STATS_DATA const& stats) = 0;
	virtual void changedSetting(std::string const& settingKey, std::string const& settingValue) = 0;
	virtual void cheatUsed(std::string const& cheatName) = 0;
	virtual void loadedModsChanged( std::vector<Sha256> const& loadedModHashes) = 0;

	static std::string getTeamDescription(SkirmishGameInfo const& info);
};

std::string to_string(GAME_END_REASON const& reason);
std::string to_string(END_GAME_STATS_DATA const& stats);

// Thread-safe class for retrieving and setting \c ActivityRecord data
class ActivityDBProtocol
{
public:
	virtual ~ActivityDBProtocol();

	[[nodiscard]] virtual std::string getFirstLaunchDate() const = 0;
};

/**
 * \c ActivityManager accepts numerous event callbacks from the core game and synthesizes
 * a (more) sensible stream of higher-level event callbacks to subscribed \c ActivitySinks.
 * To access the single, global instance of \c ActivityManager, use \c ActivityManager::instance()
 */
class ActivityManager
{
public:
  [[nodiscard]] GAME_MODE getCurrentGameMode() const;
  [[nodiscard]] std::shared_ptr<ActivityDBProtocol> getRecord() const;
  void startingGame();
	void startingSavedGame();
	void loadedLevel(LEVEL_TYPE type, std::string const& levelName);
	void completedMission(bool result, END_GAME_STATS_DATA const& stats, bool cheatsUsed);
	void quitGame(END_GAME_STATS_DATA const& stats, bool cheatsUsed);
	void preSystemShutdown();
	void navigateToMenu(std::string const& menuName);
	void beginLoadingSettings();
	void changedSetting(std::string const& settingKey, std::string const& settingValue);
	void endLoadingSettings();
	void cheatUsed(std::string const& cheatName);
	void rebuiltSearchPath();

	// called when a joinable multiplayer game is hosted
	// lobbyGameId is 0 if the lobby can't be contacted / the game is not registered with the lobby
	void hostGame(char const* SessionName, char const* PlayerName, char const* lobbyAddress,
                unsigned lobbyPort, ListeningInterfaces const& listeningInterfaces, unsigned lobbyGameId = 0);

	void hostGameLobbyServerDisconnect();
	void hostLobbyQuit();
	void willAttemptToJoinLobbyGame(std::string const& lobbyAddress, unsigned lobbyPort, unsigned lobbyGameId,
	                                std::vector<JoinConnectionDescription> const& connections);
	void joinGameFailed( std::vector<JoinConnectionDescription> const& connection_list);
	void joinGameSucceeded(char const* host, unsigned port);
	void joinedLobbyQuit();
	void updateMultiplayGameData(MULTIPLAYERGAME const& game, MULTIPLAYERINGAME const& ingame,
	                             optional<bool> privateGame);
	void hostKickPlayer(PLAYER const& player, LOBBY_ERROR_TYPES kick_type, std::string const& reason);
	void wasKickedByPlayer(PLAYER const& kicker, LOBBY_ERROR_TYPES kick_type, std::string const& reason);

	static ActivityManager& instance();
	bool initialize();
	void shutdown();
	void addActivitySink(std::shared_ptr<ActivitySink> const& sink);
	void removeActivitySink(std::shared_ptr<ActivitySink> const& sink);
private:
	ActivityManager();
	void _endedMission(GAME_END_REASON result, END_GAME_STATS_DATA const& stats, bool cheatsUsed);
private:
	std::vector<std::shared_ptr<ActivitySink>> activitySinks;
	std::shared_ptr<ActivityDBProtocol> activityDatabase;

	// storing current game state, to aid in synthesizing events
	bool bIsLoadingConfiguration = false;
	GAME_MODE currentMode = GAME_MODE::MENUS;
	bool bEndedCurrentMission = false;
	MultiplayerGameInfo currentMultiplayGameInfo;

	struct LoadedLevelEvent
	{
		LEVEL_TYPE type;
		std::string levelName;
	};

	struct FoundLobbyGameDetails
	{
		std::string lobbyAddress;
		unsigned lobbyPort;
		unsigned lobbyGameId;
		std::vector<JoinConnectionDescription> connections;

		void clear()
		{
			lobbyAddress.clear();
			lobbyPort = 0;
			lobbyGameId = 0;
			connections.clear();
		}
	};

  LoadedLevelEvent* cachedLoadedLevelEvent = nullptr;
  LoadedLevelEvent lastLoadedLevelEvent;
	FoundLobbyGameDetails lastLobbyGameJoinAttempt;
	optional<std::vector<Sha256>> lastLoadedMods;
};

#endif // __INCLUDED_SRC_ACTIVITY_H__
