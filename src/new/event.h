//
// Created by Luna Nothard on 31/12/2021.
//

#ifndef WARZONE2100_EVENT_H
#define WARZONE2100_EVENT_H

#include <string>

#include "lib/netplay/netplay.h"

/// Bit flag used if a player slot is closed to AI
static constexpr auto AI_CLOSED = -1;

/// Bit flag used if a player slot is available
static constexpr auto AI_OPEN = -2;

enum class GAME_MODE
{
    MENU,
    TUTORIAL,
    CAMPAIGN,
    CHALLENGE,
    SKIRMISH,
    HOSTING_IN_LOBBY,
    JOINING_IN_PROGRESS,

    /// Joined but waiting on game information from host
    JOINING_IN_LOBBY,

    MULTIPLAYER
};

enum class GAME_END_REASON
{
    WON,
    LOST,
    QUIT
};

enum class ALLIANCE_SETTING
{
    /// FFA
    NO_ALLIANCES,

    /// Players may make and break alliances mid-game
    ALLIANCES,

    /// Alliances are set before the game
    ALLIANCES_TEAMS,

    /**
     * Alliances are set before the game. Allied players
     * do not share research progress
     */
    ALLIANCES_UNSHARED,
};

/**
 * Subclass `EventHandler` to implement a custom handler for
 * higher-level game-state event callbacks.
 */
class EventHandler
{
    virtual ~EventHandler() = default;
};

struct SkirmishGame
{
    ALLIANCE_SETTING alliance_setting;
    std::vector<PLAYER> players;
};

enum class NET_PROTOCOL
{
    IPv4,
    IPv6
};

struct MultiplayerGame : public SkirmishGame
{
    using enum NET_PROTOCOL;
    NET_PROTOCOL protocol = IPv4;
    std::string hostname;
    std::string lobby_address;
    unsigned lobby_port = 0;
    unsigned id = 0;
    bool private_lobby = false;
    bool host = false;
};

/**
 *
 * @param info
 * @return
 */
[[nodiscard]] std::string get_team_description(const SkirmishGame& info);

void get_team_counts(std::map<int, std::size_t>& team_player_count);

#endif //WARZONE2100_EVENT_H
