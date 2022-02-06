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
#include "scores.h"

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
  JOINING_IN_LOBBY, ///< Joined but waiting on game information from host
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

  [[nodiscard]] unsigned numberOfPlayers() const;
  [[nodiscard]] std::string gameName() const;
  [[nodiscard]] std::string mapName() const;
  [[nodiscard]] bool hasLimits() const;

  MULTIPLAYERGAME game;
  unsigned numAIBotPlayers = 0;
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
  unsigned ipv4_port = 0;
  unsigned ipv6_port = 0;
};

struct MultiplayerGameInfo : public SkirmishGameInfo
{
  bool isHost;
  bool privateGame;
  std::string hostName;
  std::string lobbyAddress;
  ListeningInterfaces listeningInterfaces;
  unsigned lobbyPort;
  unsigned lobbyGameId = 0;
  unsigned maxPlayers = 0;
  unsigned numHumanPlayers = 0;
  unsigned numAvailableSlots = 0;
  unsigned numSpectators = 0;
  unsigned numOpenSpectatorSlots = 0;
};

/**
 * Subclass ActivitySink to implement a custom handler for higher-level
 * game-state event callbacks
 */
class ActivitySink
{
public:
	virtual ~ActivitySink() = default;

	virtual void navigatedToMenu(std::string const& menuName) const = 0;
	virtual void startedCampaignMission(std::string const& campaign, std::string const& levelName) const = 0;
	virtual void endedCampaignMission(std::string const& campaign, std::string const& levelName,
                                    GAME_END_REASON result, END_GAME_STATS_DATA const& stats, bool cheatsUsed) const = 0;
	virtual void startedChallenge(std::string const& challengeName) const = 0;
	virtual void endedChallenge(std::string const& challengeName, GAME_END_REASON result,
                              END_GAME_STATS_DATA const& stats, bool cheatsUsed) const = 0;
	virtual void startedSkirmishGame(SkirmishGameInfo const& info) const = 0;
	virtual void endedSkirmishGame(SkirmishGameInfo const& info, GAME_END_REASON result,
                                 END_GAME_STATS_DATA const& stats) const = 0;
	virtual void hostingMultiplayerGame(MultiplayerGameInfo const& info) const = 0;
	virtual void joinedMultiplayerGame(MultiplayerGameInfo const& info) const = 0;
	virtual void updateMultiplayerGameInfo(MultiplayerGameInfo const& info) const = 0;
	virtual void startedMultiplayerGame(MultiplayerGameInfo const& info) const = 0;
	virtual void endedMultiplayerGame(MultiplayerGameInfo const& info, GAME_END_REASON result,
                                    END_GAME_STATS_DATA const& stats) const = 0;
	virtual void changedSetting(std::string const& settingKey, std::string const& settingValue) const = 0;
	virtual void cheatUsed(std::string const& cheatName) const = 0;
	virtual void loadedModsChanged( std::vector<Sha256> const& loadedModHashes) const = 0;

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
  [[nodiscard]] ActivityDBProtocol const* getRecord() const;
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

	/**
	 * Called when a join-able multiplayer game is hosted. \c lobbyGameId is 0 if
	 * the lobby can't be contacted or the game is not registered with the lobby
	 */
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
	static ActivityManager& instance();
	void initialize();
	void shutdown();
	void addActivitySink(std::unique_ptr<ActivitySink>&& sink);
	void removeActivitySink(std::shared_ptr<ActivitySink> const& sink);
private:
	ActivityManager();
	void endedMission(GAME_END_REASON result, END_GAME_STATS_DATA const& stats, bool cheatsUsed);
private:
	std::vector<std::unique_ptr<ActivitySink> > activitySinks;
	std::unique_ptr<ActivityDBProtocol> activityDatabase;

	// storing current game state, to aid in synthesizing events
	bool bIsLoadingConfiguration = false;
	GAME_MODE currentMode = GAME_MODE::MENUS;
	bool bEndedCurrentMission = false;
	MultiplayerGameInfo currentMultiplayGameInfo;

	struct LoadedLevelEvent
	{
    LoadedLevelEvent() = default;
    LoadedLevelEvent(LEVEL_TYPE type, std::string levelName);

		LEVEL_TYPE type = LEVEL_TYPE::LDS_NONE;
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

  std::unique_ptr<LoadedLevelEvent> cachedLoadedLevelEvent = nullptr;
  LoadedLevelEvent lastLoadedLevelEvent;
	FoundLobbyGameDetails lastLobbyGameJoinAttempt;
	optional<std::vector<Sha256>> lastLoadedMods;
};

class LoggingActivitySink : public virtual ActivitySink
{
public:
  void navigatedToMenu(const std::string& menuName) const override;
  void startedCampaignMission(const std::string& campaign, const std::string& levelName) const override;
  void endedCampaignMission(const std::string& campaign, const std::string& levelName, GAME_END_REASON result,
                            END_GAME_STATS_DATA const& stats, bool cheatsUsed) const override;
  void startedChallenge(const std::string& challengeName) const override;
  void endedChallenge(const std::string& challengeName, GAME_END_REASON result,
                      const END_GAME_STATS_DATA& stats, bool cheatsUsed) const override;
  void startedSkirmishGame(const SkirmishGameInfo& info) const override;
  void endedSkirmishGame(const SkirmishGameInfo& info, GAME_END_REASON result,
                         const END_GAME_STATS_DATA& stats) const override;
  void hostingMultiplayerGame(const MultiplayerGameInfo& info) const override;
  void joinedMultiplayerGame(const MultiplayerGameInfo& info) const override;
  void updateMultiplayerGameInfo(const MultiplayerGameInfo& info) const override;
  void startedMultiplayerGame(const MultiplayerGameInfo& info) const override;
  void endedMultiplayerGame(const MultiplayerGameInfo& info, GAME_END_REASON result,
                            const END_GAME_STATS_DATA& stats) const override;
  void changedSetting(const std::string& settingKey, const std::string& settingValue) const override;
  void cheatUsed(const std::string& cheatName) const override;
  void loadedModsChanged(const std::vector<Sha256>& loadedModHashes) const override;
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

#endif // __INCLUDED_SRC_ACTIVITY_H__
